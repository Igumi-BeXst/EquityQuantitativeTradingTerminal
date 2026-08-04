// TDX 底层诊断 — 手动 open + 登录 + 打印原始帧
// 用途：count/code 市场字节对比、分时 0x051D 校准抓包落盘 fixture
#include "data/tdx/tdx_models.h"
#include "data/tdx/tdx_protocol.h"
#include "data/tdx/tdx_socket.h"
#include <QCoreApplication>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace st;

static void hexdump(const char* tag, const std::vector<uint8_t>& v, size_t limit = 64) {
    std::printf("%s (%zu): ", tag, v.size());
    for (size_t i = 0; i < v.size() && i < limit; ++i) std::printf("%02x ", v[i]);
    std::printf("\n");
}

static void saveTo(const std::string& path, const std::vector<uint8_t>& v) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
    std::printf("saved %zu bytes -> %s\n", v.size(), path.c_str());
}

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

static void probeMinute(tdx::TdxSocket& sock, const std::string& fixtureDir) {
    std::vector<uint8_t> pl;
    const std::vector<uint8_t> body = tdx::buildMinuteReq(1, "600519");
    std::printf("[minute] ");
    if (!transact(sock, tdx::Cmd::Minute, body, pl)) return;
    std::printf("payload=%zu\n", pl.size());
    hexdump("  header", pl, 16);
    hexdump("  data ", pl, 200);
    saveTo(fixtureDir + "/minute_600519.bin", pl);
    // 用当前 decodeMinute 解码看现状
    auto recs = tdx::decodeMinute(pl);
    std::printf("  decodeMinute recs=%zu first5:", recs.size());
    for (size_t i = 0; i < 5 && i < recs.size(); ++i)
        std::printf(" [%d]%.2f", recs[i].minute, recs[i].price);
    std::printf(" last: [%d]%.2f\n", recs.back().minute, recs.back().price);
}

static void probeQuote(tdx::TdxSocket& sock, const std::string& fixtureDir) {
    std::vector<uint8_t> pl;
    const auto req = tdx::buildQuoteReq({{1, "600519"}});
    std::printf("[quote] ");
    if (!transact(sock, tdx::Cmd::Quote, req, pl)) return;
    hexdump("  ", pl, 96);
    saveTo(fixtureDir + "/quote_600519.bin", pl);
    for (const auto& r : tdx::decodeQuote(pl)) {
        std::printf("  %s price=%.2f preClose=%.2f open=%.2f high=%.2f low=%.2f vol=%.0f amount=%.0f\n",
                    r.code.fullCode().c_str(), r.price, r.preClose, r.open, r.high, r.low,
                    r.volume, r.amount);
    }
}

static void probeKline(tdx::TdxSocket& sock, const std::string& fixtureDir) {
    std::vector<uint8_t> pl;
    const auto req = tdx::buildKlineReq(1, "600519", 9, 0, 5);
    std::printf("[kline] ");
    if (!transact(sock, tdx::Cmd::Kline, req, pl)) return;
    hexdump("  ", pl, 96);
    saveTo(fixtureDir + "/kline_600519_day.bin", pl);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::string host = "124.71.187.122";
    const int port = 7709;
    const std::string fixtureDir = "tests/fixtures/tdx";

    tdx::TdxSocket sock;
    if (!openLogged(sock, host.c_str(), port)) return 1;
    probeMinute(sock, fixtureDir);
    probeQuote(sock, fixtureDir);
    probeKline(sock, fixtureDir);
    sock.close();
    return 0;
}
