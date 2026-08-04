// TDX 底层诊断 — 手动 open + 登录 + 打印原始帧
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

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::string host = "124.71.187.122";
    const int port = 7709;

    tdx::TdxSocket sock;
    std::printf("open %s:%d ...\n", host.c_str(), port);
    if (!sock.open(host, port, 5000)) {
        std::printf("open FAILED\n");
        return 1;
    }
    std::printf("open OK\n");

    // 登录 0x000D
    auto req = tdx::encodeRequest(tdx::Cmd::Connect, tdx::buildConnectReq(), 1);
    hexdump("connect req", req);
    if (!sock.sendAll(req.data(), req.size())) {
        std::printf("send FAILED\n");
        return 1;
    }
    std::printf("send OK\n");

    std::vector<uint8_t> frame;
    if (!sock.recvFrame(frame, 5000)) {
        std::printf("recv FAILED\n");
        return 1;
    }
    hexdump("connect resp", frame, 96);
    auto resp = tdx::decodeResponse(frame);
    std::printf("decode ok=%d control=%02x type=%04x payload=%zu\n",
                resp.ok ? 1 : 0, resp.control, resp.type, resp.payload.size());
    if (resp.ok) hexdump("payload", resp.payload, 80);

    // Count 股票数量
    auto cntReq = tdx::encodeRequest(tdx::Cmd::Count, tdx::buildCountReq(1), 2);
    hexdump("count req", cntReq);
    if (sock.sendAll(cntReq.data(), cntReq.size())) {
        std::vector<uint8_t> f2;
        if (sock.recvFrame(f2, 5000)) {
            auto r2 = tdx::decodeResponse(f2);
            std::printf("count decode ok=%d payload=%zu count=%u\n",
                        r2.ok ? 1 : 0, r2.payload.size(),
                        r2.ok ? tdx::decodeCount(r2.payload) : 0);
            hexdump("count payload", r2.payload, 32);
        } else {
            std::printf("count recv FAILED\n");
        }
    }

    // K线 日线请求（category=9, count=10）
    auto klReq = tdx::encodeRequest(tdx::Cmd::Kline,
                                    tdx::buildKlineReq(1, "600519", 9, 0, 10), 3);
    hexdump("kline req", klReq);
    if (sock.sendAll(klReq.data(), klReq.size())) {
        std::vector<uint8_t> f3;
        if (sock.recvFrame(f3, 5000)) {
            auto r3 = tdx::decodeResponse(f3);
            std::printf("kline decode ok=%d type=%04x payload=%zu\n",
                        r3.ok ? 1 : 0, r3.type, r3.payload.size());
            if (r3.ok) hexdump("kline payload", r3.payload, 140);
        } else {
            std::printf("kline recv FAILED\n");
        }
    }

    // 分时请求
    auto mnReq = tdx::encodeRequest(tdx::Cmd::Minute, tdx::buildMinuteReq(1, "600519"), 4);
    if (sock.sendAll(mnReq.data(), mnReq.size())) {
        std::vector<uint8_t> f4;
        if (sock.recvFrame(f4, 5000)) {
            auto r4 = tdx::decodeResponse(f4);
            std::printf("minute decode ok=%d payload=%zu\n", r4.ok ? 1 : 0, r4.payload.size());
            if (r4.ok) {
                hexdump("minute payload", r4.payload, 100);
                auto mrecs = tdx::decodeMinute(r4.payload);
                std::printf("minute recs=%zu first5: ", mrecs.size());
                for (size_t i = 0; i < 5 && i < mrecs.size(); ++i)
                    std::printf("[%d]%.2f ", mrecs[i].minute, mrecs[i].price);
                std::printf("\n");
                if (!mrecs.empty()) {
                    const auto& last = mrecs.back();
                    std::printf("last: minute=%d price=%.2f vol=%.0f\n",
                                last.minute, last.price, last.volume);
                }
            }
        } else { std::printf("minute recv FAILED\n"); }
    }

    // 报价请求
    auto qtReq = tdx::encodeRequest(tdx::Cmd::Quote,
                                    tdx::buildQuoteReq({{1, "600519"}}), 5);
    if (sock.sendAll(qtReq.data(), qtReq.size())) {
        std::vector<uint8_t> f5;
        if (sock.recvFrame(f5, 5000)) {
            auto r5 = tdx::decodeResponse(f5);
            std::printf("quote decode ok=%d payload=%zu\n", r5.ok ? 1 : 0, r5.payload.size());
            if (r5.ok) hexdump("quote payload", r5.payload, 120);
        } else { std::printf("quote recv FAILED\n"); }
    }

    // 股票列表（SH start=0）
    auto cdReq = tdx::encodeRequest(tdx::Cmd::Code, tdx::buildCodeReq(1, 0), 6);
    if (sock.sendAll(cdReq.data(), cdReq.size())) {
        std::vector<uint8_t> f6;
        if (sock.recvFrame(f6, 5000)) {
            auto r6 = tdx::decodeResponse(f6);
            std::printf("code decode ok=%d payload=%zu\n", r6.ok ? 1 : 0, r6.payload.size());
            if (r6.ok) hexdump("code payload", r6.payload, 100);
        } else { std::printf("code recv FAILED\n"); }
    }

    sock.close();
    return 0;
}
