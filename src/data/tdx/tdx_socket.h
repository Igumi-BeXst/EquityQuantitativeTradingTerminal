#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace st {
namespace tdx {

/// 传输抽象（测试可注入假实现）
class TdxTransport {
public:
    virtual ~TdxTransport() = default;
    virtual bool open(const std::string& host, int port, int timeoutMs) = 0;
    virtual bool sendAll(const uint8_t* data, size_t len) = 0;
    virtual bool recvFrame(std::vector<uint8_t>& out, int timeoutMs) = 0;
    virtual void close() = 0;
};

/// 通达信行情 TCP 传输（同步请求-响应，WinSock2）
///
/// 只在 IO 池线程使用（阻塞）。帧读取 recvFrame 先读 16 字节头，
/// 再从头部解析数据域长度，读到完整帧才返回。
class TdxSocket : public TdxTransport {
public:
    TdxSocket() = default;
    ~TdxSocket() { close(); }
    TdxSocket(const TdxSocket&) = delete;
    TdxSocket& operator=(const TdxSocket&) = delete;

    /// 建立 TCP 连接（含 WSAStartup 幂等初始化），失败返回 false
    bool open(const std::string& host, int port, int timeoutMs) override;

    /// 发送全部字节（内部循环 send）
    bool sendAll(const uint8_t* data, size_t len) override;

    /// 读取完整一帧（16 字节头 + 数据域），带总超时
    bool recvFrame(std::vector<uint8_t>& out, int timeoutMs) override;

    void close() override;
    bool isOpen() const { return fd_ != kInvalid; }

private:
    bool recvExact(uint8_t* buf, size_t len, int timeoutMs);

    static constexpr uintptr_t kInvalid = static_cast<uintptr_t>(-1);
    uintptr_t fd_ = kInvalid;
    bool wsaInit_ = false;
};

} // namespace tdx
} // namespace st
