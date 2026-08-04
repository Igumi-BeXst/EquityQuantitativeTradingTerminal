// TDX 底层诊断 — 手动 open + 登录 + 打印原始帧
// 专注：count/code 命令市场字节 0/1 的响应差异，定位股票列表市场映射
#include "data/tdx/tdx_models.h"
#include "data/tdx/tdx_protocol.h"
#include "data/tdx/tdx_socket.h"
#include <QCoreApplication>
#include <cstdio>
#include <vector>

using namespace st;

static void hexdump(const char* tag, const std::vector<uint8_t>& v, size_t limit = 64) {
    std::printf("%s (%zu): ", tag, v.size());
    for (size_t i = 0; i < v.size() && i < limit; ++i) std::printf("%02x ", v[i]);
    std::printf("\n");
}

// 打开连接 + 登录（每次实验用独立连接，避免坏请求断开后续）
static bool openLogged(tdx::TdxSocket& sock, const char* host, int port) {
    std::printf("open %s:%d ...\n", host, port);
    if (!sock.open(host, port, 5000)) { std::printf("open FAILED\n"); return false; }
    std::printf("open OK\n");
    auto req = tdx::encodeRequest(tdx::Cmd::Connect, tdx::buildConnectReq(), 1);
    if (!sock.sendAll(req.data(), req.size())) { std::printf("login send FAILED\n"); return false; }
    std::vector<uint8_t> frame;
    if (!sock.recvFrame(frame, 5000)) { std::printf("login recv FAILED\n"); return false; }
    auto resp = tdx::decodeResponse(frame);
    std::printf("login ok=%d\n", resp.ok ? 1 : 0);
    return resp.ok;
}

// 发送请求收响应（失败返回 false）
static bool transact(tdx::TdxSocket& sock, tdx::Cmd cmd, const std::vector<uint8_t>& body,
                     std::vector<uint8_t>& outPayload, int timeoutMs = 5000) {
    auto req = tdx::encodeRequest(cmd, body, 2);
    if (!sock.sendAll(req.data(), req.size())) { std::printf("  send FAILED\n"); return false; }
    std::vector<uint8_t> f;
    if (!sock.recvFrame(f, timeoutMs)) { std::printf("  recv FAILED\n"); return false; }
    auto r = tdx::decodeResponse(f);
    if (!r.ok) { std::printf("  decode FAILED\n"); return false; }
    outPayload = r.payload;
    return true;
}

static void probeCount(tdx::TdxSocket& sock, int mkt) {
    std::vector<uint8_t> pl;
    const std::vector<uint8_t> body{static_cast<uint8_t>(mkt), 0x00, 0x75, 0xC7, 0x33, 0x01};
    std::printf("[count mkt=%d] ", mkt);
    if (transact(sock, tdx::Cmd::Count, body, pl)) {
        std::printf("count=%u", tdx::decodeCount(pl));
        hexdump("", pl, 16);
    }
}

static void probeCode(tdx::TdxSocket& sock, int mkt) {
    std::vector<uint8_t> pl;
    const std::vector<uint8_t> body{static_cast<uint8_t>(mkt), 0x00, 0x00, 0x00};
    std::printf("[code mkt=%d] ", mkt);
    if (!transact(sock, tdx::Cmd::Code, body, pl)) return;
    std::printf("payload=%zu\n", pl.size());
    hexdump("  ", pl, 116);
    if (pl.size() >= 60) {
        auto recs = tdx::decodeCodeList(pl, tdx::marketFromTdx(static_cast<uint8_t>(mkt)));
        std::printf("  decodeCodeList recs=%zu", recs.size());
        for (size_t i = 0; i < 3 && i < recs.size(); ++i) {
            std::printf(" [%d]%s=%s", static_cast<int>(recs[i].code.market()),
                        recs[i].code.code().c_str(), recs[i].name.c_str());
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::string host = "124.71.187.122";
    const int port = 7709;

    {
        tdx::TdxSocket sock;
        if (!openLogged(sock, host.c_str(), port)) return 1;
        probeCount(sock, 1);
        probeCount(sock, 0);
        probeCode(sock, 1);   // SH（期望沪市代码 600xxx）
        probeCode(sock, 0);   // SZ（期望深市代码 000xxx/300xxx）
        sock.close();
    }
    return 0;
}
