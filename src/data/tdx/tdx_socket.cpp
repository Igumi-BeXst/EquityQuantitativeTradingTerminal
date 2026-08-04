#include "data/tdx/tdx_socket.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#endif

#include <chrono>

namespace st {
namespace tdx {

namespace {

constexpr int kFrameHeaderLen = 16;

bool socketWait(uintptr_t fd, int timeoutMs, bool writable) {
#ifdef _WIN32
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(static_cast<SOCKET>(fd), &fds);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    const int rc = ::select(0, writable ? nullptr : &fds,
                            writable ? &fds : nullptr, nullptr,
                            timeoutMs > 0 ? &tv : nullptr);
    return rc > 0;
#else
    pollfd pfd{};
    pfd.fd = static_cast<int>(fd);
    pfd.events = writable ? POLLOUT : POLLIN;
    return ::poll(&pfd, 1, timeoutMs) > 0;
#endif
}

}  // namespace

bool TdxSocket::open(const std::string& host, int port, int timeoutMs) {
    close();
#ifdef _WIN32
    if (!wsaInit_) {
        WSADATA wsa{};
        if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        wsaInit_ = true;
    }
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) return false;

    int sock = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        sock = static_cast<int>(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if (sock < 0) continue;
#ifdef _WIN32
        u_long mode = 1;
        ::ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &mode);  // 非阻塞用于超时 connect
#else
        int flags = ::fcntl(sock, F_GETFL, 0);
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
        const int rc = ::connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen));
        bool ok = (rc == 0);
        if (rc < 0) {
#ifdef _WIN32
            ok = (WSAGetLastError() == WSAEWOULDBLOCK) &&
                 socketWait(static_cast<uintptr_t>(sock), timeoutMs, true);
#else
            ok = (errno == EINPROGRESS) && socketWait(static_cast<uintptr_t>(sock), timeoutMs, true);
#endif
        }
#ifdef _WIN32
        u_long mode0 = 0;
        ::ioctlsocket(static_cast<SOCKET>(sock), FIONBIO, &mode0);  // 恢复阻塞
        if (ok) {
            int err = 0;
            int len = sizeof(err);
            ::getsockopt(static_cast<SOCKET>(sock), SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&err), &len);
            if (err != 0) ok = false;
        }
#else
        int flags2 = ::fcntl(sock, F_GETFL, 0);
        ::fcntl(sock, F_SETFL, flags2 & ~O_NONBLOCK);
        if (ok) {
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) ok = false;
        }
#endif
        if (ok) {
            fd_ = static_cast<uintptr_t>(sock);
            break;
        }
#ifdef _WIN32
        ::closesocket(static_cast<SOCKET>(sock));
#else
        ::close(sock);
#endif
        sock = -1;
    }
    ::freeaddrinfo(res);
    return fd_ != kInvalid;
}

bool TdxSocket::sendAll(const uint8_t* data, size_t len) {
    if (fd_ == kInvalid) return false;
    size_t sent = 0;
    while (sent < len) {
        const int n = ::send(static_cast<int>(fd_),
                             reinterpret_cast<const char*>(data + sent),
                             static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TdxSocket::recvExact(uint8_t* buf, size_t len, int timeoutMs) {
    size_t got = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (got < len) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return false;
        if (!socketWait(fd_, static_cast<int>(remaining), false)) return false;
        const int n = ::recv(static_cast<int>(fd_),
                             reinterpret_cast<char*>(buf + got),
                             static_cast<int>(len - got), 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool TdxSocket::recvFrame(std::vector<uint8_t>& out, int timeoutMs) {
    if (fd_ == kInvalid) return false;
    uint8_t header[kFrameHeaderLen];
    if (!recvExact(header, kFrameHeaderLen, timeoutMs)) return false;

    // 数据域长度 = 帧头 [12:14] ZipLength（小端）
    const size_t dataLen = static_cast<size_t>(header[12]) |
                           (static_cast<size_t>(header[13]) << 8);
    out.assign(header, header + kFrameHeaderLen);
    if (dataLen > 0) {
        out.resize(kFrameHeaderLen + dataLen);
        if (!recvExact(out.data() + kFrameHeaderLen, dataLen, timeoutMs)) return false;
    }
    return true;
}

void TdxSocket::close() {
    if (fd_ != kInvalid) {
#ifdef _WIN32
        ::closesocket(static_cast<SOCKET>(fd_));
#else
        ::close(static_cast<int>(fd_));
#endif
        fd_ = kInvalid;
    }
}

} // namespace tdx
} // namespace st
