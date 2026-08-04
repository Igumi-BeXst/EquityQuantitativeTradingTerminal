// TDX 底层诊断 — 手动 open + 登录 + 打印原始帧
// 用途：count/code 市场字节对比、分时 0x051D 校准抓包落盘 fixture
#include "data/tdx/tdx_models.h"
#include "data/tdx/tdx_protocol.h"
#include "data/tdx/tdx_socket.h"
#include "foundation/utils/datetime.h"
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

// 指数K线探测：验证指数记录比个股多 4 字节（涨跌家数）
static void probeIndexKline(tdx::TdxSocket& sock) {
    std::vector<uint8_t> pl;
    const auto req = tdx::buildKlineReq(1, "000001", 9, 0, 5);  // 上证指数
    std::printf("[index kline 000001] ");
    if (!transact(sock, tdx::Cmd::Kline, req, pl)) return;
    hexdump("  ", pl, 200);
    saveTo("tests/fixtures/tdx/kline_000001_day.bin", pl);
    const StockCode idx(Market::SH, "000001");
    auto bars = tdx::decodeKline(pl, tdx::KlineDay, tdx::isIndexCode(idx));
    std::printf("  decodeKline(跳4B) bars=%zu:", bars.size());
    for (size_t i = 0; i < bars.size(); ++i)
        std::printf(" [%s]开%.2f/收%.2f 量%.0f 额%.0f",
                    st::utils::toDateString(bars[i].time).c_str(),
                    bars[i].open, bars[i].close, bars[i].volume, bars[i].amount);
    std::printf("\n");
}

// 精选池批量报价探测：验证 change 是否合理
static void probeBatchQuote(tdx::TdxSocket& sock) {
    // 注：服务器单次报价批上限实测 80 只（200 只请求返回 80）→ quoteChunk_=80
    const std::vector<std::pair<uint8_t, std::string>> codes = {
        {1, "600519"}, {1, "601318"}, {1, "600036"},
        {0, "000001"}, {0, "300750"}, {0, "000858"}};
    const auto req = tdx::buildQuoteReq(codes);
    std::vector<uint8_t> pl;
    std::printf("[batchQuote %zu只] ", codes.size());
    if (!transact(sock, tdx::Cmd::Quote, req, pl)) return;
    std::printf("payload=%zu\n", pl.size());
    hexdump("  ", pl, 489);
    saveTo("tests/fixtures/tdx/quote_6codes.bin", pl);
    for (const auto& r : tdx::decodeQuote(pl)) {
        const double chg = r.preClose > 0 ? (r.price - r.preClose) / r.preClose * 100.0 : 0.0;
        std::printf("  %s 现价%.2f 昨收%.2f 涨跌%.2f%% 量%.0f\n",
                    r.code.fullCode().c_str(), r.price, r.preClose, chg, r.volume);
    }
}

// 分钟K线探测（5/15/30/60分 category 0/1/2/3）
static void probeMinuteKline(tdx::TdxSocket& sock) {
    for (int cat : {0, 1, 2, 3}) {
        const auto req = tdx::buildKlineReq(1, "600519", static_cast<uint8_t>(cat), 0, 320);
        std::vector<uint8_t> pl;
        std::printf("[mkline cat=%d] ", cat);
        if (!transact(sock, tdx::Cmd::Kline, req, pl)) continue;
        std::printf("payload=%zu ", pl.size());
        hexdump("", pl, 48);
        const auto bars = tdx::decodeKline(pl, static_cast<uint8_t>(cat));
        std::printf("  bars=%zu", bars.size());
        if (!bars.empty()) {
            std::printf(" first[%s] c=%.2f",
                        st::utils::toDateTimeString(bars[0].time).c_str(), bars[0].close);
        }
        std::printf("\n");
    }
}

// 逐笔成交探测（0x0FC5）：验证响应格式
static void probeTransaction(tdx::TdxSocket& sock) {
    const auto req = tdx::buildTransactionReq(1, "600519", 0, 1000);
    std::vector<uint8_t> pl;
    std::printf("[transaction] ");
    if (!transact(sock, tdx::Cmd::MinuteTrade, req, pl)) return;
    std::printf("payload=%zu ", pl.size());
    hexdump("", pl, 48);
    const auto ticks = tdx::decodeTransaction(pl);
    std::printf("  ticks=%zu\n", ticks.size());
    for (size_t i = 0; i < 3 && i < ticks.size(); ++i)
        std::printf("    [%02d:%02d] p=%.2f vol=%.0f num=%d bs=%d\n",
                    ticks[i].hour, ticks[i].minute, ticks[i].price,
                    ticks[i].volume, ticks[i].num, ticks[i].buyorsell);
    if (!ticks.empty()) {
        const auto& last = ticks.back();
        std::printf("    last[%02d:%02d] p=%.2f vol=%.0f\n",
                    last.hour, last.minute, last.price, last.volume);
        // 拉多页求和，与报价总量(37450手=3745000股)对比判定单位
        double totalRaw = 0;
        uint32_t start = 0;
        int pages = 0;
        while (start <= 10000 && pages < 20) {
            const auto preq = tdx::buildTransactionReq(1, "600519",
                                                       static_cast<uint16_t>(start), 1000);
            std::vector<uint8_t> ppl;
            if (!transact(sock, tdx::Cmd::MinuteTrade, preq, ppl)) break;
            auto pticks = tdx::decodeTransaction(ppl);
            if (pticks.empty()) break;
            for (const auto& t : pticks) totalRaw += t.volume;
            ++pages;
            if (pticks.size() < 1000) break;
            start += 1000;
        }
        std::printf("    全日记 %d 页, 原始量合计=%.0f (报价总量 37450手/3745000股)\n",
                    pages, totalRaw);
        // 打印各页首末时间，校准时间基准（末笔应 ~14:59/15:00）
        uint32_t s2 = 0;
        while (s2 <= 10000) {
            const auto preq2 = tdx::buildTransactionReq(1, "600519",
                                                        static_cast<uint16_t>(s2), 1000);
            std::vector<uint8_t> ppl2;
            if (!transact(sock, tdx::Cmd::MinuteTrade, preq2, ppl2)) break;
            auto pticks2 = tdx::decodeTransaction(ppl2);
            if (pticks2.empty()) break;
            std::printf("    页 start=%u: [%02d:%02d ~ %02d:%02d] 末价%.2f\n",
                        s2, pticks2.front().hour, pticks2.front().minute,
                        pticks2.back().hour, pticks2.back().minute,
                        pticks2.back().price);
            if (pticks2.size() < 1000) break;
            s2 += 1000;
        }
    }
    saveTo("tests/fixtures/tdx/transaction_600519.bin", pl);
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
    probeIndexKline(sock);
    probeBatchQuote(sock);
    probeMinuteKline(sock);
    probeTransaction(sock);
    sock.close();
    return 0;
}
