#include <gtest/gtest.h>
#include "data/tdx/tdx_provider.h"
#include "data/tdx/tdx_protocol.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace st;

namespace {

// ---- 合成 payload 辅助 ----
void putVar(std::vector<uint8_t>& v, int64_t val) {
    const uint64_t mag = static_cast<uint64_t>(val < 0 ? -val : val);
    uint8_t b0 = static_cast<uint8_t>(mag & 0x3F);
    if (val < 0) b0 |= 0x40;
    uint64_t rest = mag >> 6;
    if (rest > 0) b0 |= 0x80;
    v.push_back(b0);
    while (rest > 0) {
        uint8_t b = static_cast<uint8_t>(rest & 0x7F);
        rest >>= 7;
        if (rest > 0) b |= 0x80;
        v.push_back(b);
    }
}
void putU16(std::vector<uint8_t>& v, uint16_t n) {
    v.push_back(static_cast<uint8_t>(n & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
}
void putU32(std::vector<uint8_t>& v, uint32_t n) {
    v.push_back(static_cast<uint8_t>(n & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 24) & 0xFF));
}

// 构造合法响应帧（16B 头 + 未压缩 payload）
// 头布局：[0:4]前缀 [4]control [5]msgid [6:10]未用 [10:12]type [12:14]zipLen [14:16]length
std::vector<uint8_t> makeResponseFrame(uint16_t cmd, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f = {0xB1, 0xCB, 0x74, 0x00, 0x0C, 0x01, 0x00, 0x00,
                              0x00, 0x00};
    f.push_back(static_cast<uint8_t>(cmd & 0xFF));        // [10]
    f.push_back(static_cast<uint8_t>(cmd >> 8));          // [11]
    putU16(f, static_cast<uint16_t>(payload.size()));     // [12:14] zipLen
    putU16(f, static_cast<uint16_t>(payload.size()));     // [14:16] length
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

// K线 payload（2 根，最新在前，日线；价格单位厘 ×1000）
std::vector<uint8_t> makeKlinePayload2() {
    std::vector<uint8_t> p;
    putU16(p, 2);
    // 2026-08-04: open 1350.06 high 1350.94 low 1328.36 close 1328.36
    putU32(p, 20260804);
    putVar(p, 1350060);  // openOff (lastLi=0)
    putVar(p, -21700);   // closeOff = close - open = 1328360 - 1350060
    putVar(p, 880);      // highOff = high - open = 1350940 - 1350060
    putVar(p, -21700);   // lowOff = low - open
    putU32(p, 0);        // vol
    putU32(p, 0);        // amount
    // 2026-08-03: open 1340.00 high 1345.00 low 1330.00 close 1335.00
    putU32(p, 20260803);
    putVar(p, 11640);    // openOff = 1340000 - 1328360
    putVar(p, -5000);    // closeOff = 1335000 - 1340000
    putVar(p, 5000);     // highOff = 1345000 - 1340000
    putVar(p, -10000);   // lowOff = 1330000 - 1340000
    putU32(p, 0);
    putU32(p, 0);
    return p;
}

// 空 gbbq payload（无事件）
std::vector<uint8_t> makeEmptyGbbq() {
    std::vector<uint8_t> p(9, 0x00);
    putU16(p, 0);
    return p;
}

// 报价 payload（1 条）
std::vector<uint8_t> makeQuotePayload(const std::string& code, int mkt) {
    std::vector<uint8_t> p;
    putU16(p, 0);
    putU16(p, 1);
    p.push_back(static_cast<uint8_t>(mkt));
    p.insert(p.end(), code.begin(), code.end());
    putU16(p, 0);
    putVar(p, 132836);   // price
    putVar(p, 3062);     // lastDiff
    putVar(p, 2170);     // openDiff
    putVar(p, 2258);     // highDiff
    putVar(p, 0);        // lowDiff
    putVar(p, 0);
    putVar(p, 0);
    putVar(p, 37450);    // vol 手
    putVar(p, 661);
    putU32(p, 0);
    return p;
}

// 报价 payload（1 条，含完整五档尾部；price=10.00，bids 9.99..9.95 / asks 10.01..10.05）
std::vector<uint8_t> makeQuotePayloadFullDepth(const std::string& code, int mkt) {
    std::vector<uint8_t> p;
    putU16(p, 0);
    putU16(p, 1);
    p.push_back(static_cast<uint8_t>(mkt));
    p.insert(p.end(), code.begin(), code.end());
    putU16(p, 0);
    putVar(p, 1000);     // price 分 → 10.00
    putVar(p, 0);        // lastDiff → preClose 10.00
    putVar(p, -50);      // openDiff → 9.50
    putVar(p, 50);       // highDiff → 10.50
    putVar(p, -100);     // lowDiff → 9.00
    putVar(p, 0);
    putVar(p, 0);
    putVar(p, 500);      // vol 手
    putVar(p, 100);      // cur_vol
    putU32(p, 0);        // amount
    putVar(p, 0); putVar(p, 0); putVar(p, 0); putVar(p, 0);  // s_vol b_vol rev2 rev3
    const int bidDiff[5] = {-1, -2, -3, -4, -5};  // 相对现价差分（分）
    const int askDiff[5] = {1, 2, 3, 4, 5};
    const int bidVol[5] = {100, 200, 300, 400, 500};
    const int askVol[5] = {600, 700, 800, 900, 1000};
    for (int k = 0; k < 5; ++k) {
        putVar(p, bidDiff[k]);
        putVar(p, askDiff[k]);
        putVar(p, bidVol[k]);
        putVar(p, askVol[k]);
    }
    putU16(p, 0);        // rev4
    putVar(p, 0); putVar(p, 0); putVar(p, 0); putVar(p, 0);  // rev5-8
    putU16(p, 0);        // rev9
    putU16(p, 0);        // active2
    return p;
}

// 成交明细 payload（n 条，分钟 09:30 起递增；每条差分 +1 分，量 手，买卖交替）
std::vector<uint8_t> makeTransactionPayload(int n) {
    std::vector<uint8_t> p;
    putU16(p, static_cast<uint16_t>(n));
    for (int i = 0; i < n; ++i) {
        putU16(p, static_cast<uint16_t>(9 * 60 + 30 + i));  // minutes
        putVar(p, 1);          // priceDiff（累积 → 价格 = (i+1)/100）
        putVar(p, 10 + i);     // vol 手
        putVar(p, 1);          // num
        putVar(p, i % 2);      // buyorsell：0=买 1=卖
        putVar(p, 0);          // unknown
    }
    return p;
}

// ---- 假传输（共享状态）----
struct FakeState {
    bool failOpen = false;
    bool failNextSend = false;
    int sendCount = 0;
    std::vector<uint8_t> lastRequest;
    std::map<uint16_t, int> sendsByCmd;
    std::map<uint16_t, std::vector<uint8_t>> payloads;
    std::function<std::vector<uint8_t>(uint16_t cmd, const std::vector<uint8_t>& req)> dynamicPayload;
};

class FakeTdxTransport : public tdx::TdxTransport {
public:
    explicit FakeTdxTransport(std::shared_ptr<FakeState> s) : st(std::move(s)) {}
    std::shared_ptr<FakeState> st;

    bool open(const std::string&, int, int) override { return !st->failOpen; }

    bool sendAll(const uint8_t* data, size_t len) override {
        ++st->sendCount;
        st->lastRequest.assign(data, data + len);
        if (len >= 12) {
            const uint16_t cmd = static_cast<uint16_t>(data[10]) |
                                 (static_cast<uint16_t>(data[11]) << 8);
            ++st->sendsByCmd[cmd];
        }
        if (st->failNextSend) { st->failNextSend = false; return false; }
        return true;
    }

    bool recvFrame(std::vector<uint8_t>& out, int) override {
        uint16_t cmd = 0;
        if (st->lastRequest.size() >= 12) {
            cmd = static_cast<uint16_t>(st->lastRequest[10]) |
                  (static_cast<uint16_t>(st->lastRequest[11]) << 8);
        }
        std::vector<uint8_t> payload;
        if (st->dynamicPayload) payload = st->dynamicPayload(cmd, st->lastRequest);
        else if (st->payloads.count(cmd)) payload = st->payloads[cmd];
        out = makeResponseFrame(cmd, payload);
        return true;
    }

    void close() override {}
};

bool waitConnected(TdxProvider& p, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (p.isConnected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return p.isConnected();
}

void connectWithFake(TdxProvider& p, const std::shared_ptr<FakeState>& st) {
    p.setServers({"fake:7709"});
    p.setPollIntervalMs(60000);  // 避免轮询干扰
    p.setTransportFactory([st]() -> std::unique_ptr<tdx::TdxTransport> {
        return std::make_unique<FakeTdxTransport>(st);
    });
    p.connect();
    ASSERT_TRUE(waitConnected(p));
}

}  // namespace

// ============================================================
// getBars 快乐路径
// ============================================================
TEST(TdxProviderTest, GetBarsHappyPath) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Kline)] = makeKlinePayload2();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Gbbq)] = makeEmptyGbbq();
    connectWithFake(provider, st);

    auto bars = provider.getBars(StockCode(Market::SH, "600519"),
                                 BarPeriod::Daily, DateTime{}, utils::now());
    ASSERT_EQ(bars.size(), 2u);
    // 升序：2026-08-03 → 2026-08-04
    EXPECT_EQ(st::utils::toDateString(bars[0].time), "2026-08-03");
    EXPECT_NEAR(bars[0].close, 1335.0, 1e-3);
    EXPECT_EQ(st::utils::toDateString(bars[1].time), "2026-08-04");
    EXPECT_NEAR(bars[1].close, 1328.36, 1e-3);
    EXPECT_NEAR(bars[1].high, 1350.94, 1e-3);
    EXPECT_NEAR(bars[1].low, 1328.36, 1e-3);
    provider.disconnect();
}

// ============================================================
// 断线重连：发送失败 → 断开 → 下次自动重连
// ============================================================
TEST(TdxProviderTest, GetBarsReconnectsAfterSendFailure) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Kline)] = makeKlinePayload2();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Gbbq)] = makeEmptyGbbq();
    connectWithFake(provider, st);

    auto bars = provider.getBars(StockCode(Market::SH, "600519"),
                                 BarPeriod::Daily, DateTime{}, utils::now());
    ASSERT_EQ(bars.size(), 2u);

    st->failNextSend = true;  // 下次发送失败 → 断线
    auto empty = provider.getBars(StockCode(Market::SH, "600519"),
                                  BarPeriod::Daily, DateTime{}, utils::now());
    EXPECT_TRUE(empty.empty());
    EXPECT_FALSE(provider.isConnected());  // 已断开

    // 下次 getBars 自动重连（工厂 new 新实例，同一共享状态，成功）
    auto again = provider.getBars(StockCode(Market::SH, "600519"),
                                  BarPeriod::Daily, DateTime{}, utils::now());
    ASSERT_EQ(again.size(), 2u);
    EXPECT_TRUE(provider.isConnected());
    provider.disconnect();
}

// ============================================================
// batchQuote 分块（61 码 + chunk=60 → 2 次请求）
// ============================================================
TEST(TdxProviderTest, BatchQuoteChunking) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Quote)] = makeQuotePayload("600519", 1);
    connectWithFake(provider, st);
    provider.setQuoteChunkSize(60);

    std::vector<StockCode> codes;
    for (int i = 0; i < 61; ++i) codes.emplace_back(Market::SH, "600001");
    auto quotes = provider.batchQuote(codes);

    // 2 次 Quote 请求（60 + 1）；connect 也算 1 次 sendAll
    EXPECT_EQ(st->sendsByCmd[static_cast<uint16_t>(tdx::Cmd::Quote)], 2);
    EXPECT_GE(st->sendCount, 2);
    provider.disconnect();
}

// ============================================================
// getMarketDepth 盘口五档
// ============================================================
TEST(TdxProviderTest, GetMarketDepth) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Quote)] =
        makeQuotePayloadFullDepth("600519", 1);
    connectWithFake(provider, st);

    auto md = provider.getMarketDepth(StockCode(Market::SH, "600519"));
    ASSERT_TRUE(md.has_value());
    EXPECT_NEAR(md->bestBid(), 9.99, 1e-3);
    EXPECT_NEAR(md->bestAsk(), 10.01, 1e-3);
    EXPECT_NEAR(md->spread(), 0.02, 1e-3);
    EXPECT_NEAR(md->bids[4].price, 9.95, 1e-3);   // 买五
    EXPECT_NEAR(md->asks[4].price, 10.05, 1e-3); // 卖五
    EXPECT_EQ(md->bids[1].volume, 20000);         // 200 手 → 20000 股
    EXPECT_EQ(md->asks[3].volume, 90000);
    provider.disconnect();
}

TEST(TdxProviderTest, GetMarketDepthEmptyResponseReturnsNull) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Quote)] = {};  // 空 payload
    connectWithFake(provider, st);

    auto md = provider.getMarketDepth(StockCode(Market::SH, "600519"));
    EXPECT_FALSE(md.has_value());
    provider.disconnect();
}

// ============================================================
// getTransactions 最近成交明细（最新在前）
// ============================================================
TEST(TdxProviderTest, GetTransactions) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::MinuteTrade)] =
        makeTransactionPayload(30);
    connectWithFake(provider, st);

    auto ticks = provider.getTransactions(StockCode(Market::SH, "600519"), 30);
    ASSERT_EQ(ticks.size(), 30u);
    // 最新在前：第 30 条（分钟 09:59 = 570+29）排第一
    EXPECT_EQ(st::utils::toDateTimeString(ticks.front().time).substr(11, 5), "09:59");
    EXPECT_GT(ticks.front().volume, 0);
    // 方向：record 29 的 buyorsell=29%2=1 → Sell
    EXPECT_EQ(ticks.front().direction, Direction::Sell);
    EXPECT_EQ(ticks.front().code.code(), "600519");
    provider.disconnect();
}

TEST(TdxProviderTest, GetTransactionsEmptyResponse) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::MinuteTrade)] = {};
    connectWithFake(provider, st);

    auto ticks = provider.getTransactions(StockCode(Market::SH, "600519"), 30);
    EXPECT_TRUE(ticks.empty());
    provider.disconnect();
}

// ============================================================
// getStockList 分页（2500 只 → 3 页）
// ============================================================
TEST(TdxProviderTest, GetStockListPaging) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Count)] = {0xC4, 0x09};  // 2500 LE
    st->dynamicPayload = [](uint16_t cmd, const std::vector<uint8_t>& req) {
        if (cmd != static_cast<uint16_t>(tdx::Cmd::Code)) return std::vector<uint8_t>{};
        // 请求帧 = 12B 头 + body{market, 0x00, start_lo, start_hi}
        const uint16_t start = static_cast<uint16_t>(req[14]) |
                               (static_cast<uint16_t>(req[15]) << 8);
        const int total = 2500;
        const int n = std::min<int>(1000, total - start);
        std::vector<uint8_t> p;
        putU16(p, static_cast<uint16_t>(n));
        for (int i = 0; i < n; ++i) {
            char code[7];
            std::snprintf(code, sizeof(code), "60%04d", i);  // 6 位
            p.insert(p.end(), code, code + 6);
            p.push_back(0x64);
            p.push_back(0x00);
            p.insert(p.end(), 8, 0x00);   // 名称空
            p.insert(p.end(), 13, 0x00);
        }
        return p;
    };
    connectWithFake(provider, st);

    auto infos = provider.getStockList(Market::SH);
    ASSERT_EQ(infos.size(), 2500u);
    EXPECT_EQ(st->sendsByCmd[static_cast<uint16_t>(tdx::Cmd::Code)], 3);
    for (const auto& info : infos) {
        EXPECT_EQ(info.code.market(), Market::SH);
        EXPECT_TRUE(info.code.isValid());
    }
    provider.disconnect();
}

// ============================================================
// 前复权（10送10：事件前价格 ÷2）
// ============================================================
TEST(TdxProviderTest, QfqAdjustShareSplit) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();

    // 2 根日线，最新在前（价格单位厘 ×1000）：
    // 2026-08-04 close 1328.36（事件后，不复权），2025-01-05 close 20.0（事件前，÷2）
    std::vector<uint8_t> kline;
    putU16(kline, 2);
    putU32(kline, 20260804);
    putVar(kline, 1350060);  // open 1350.06
    putVar(kline, -21700);   // close = 1328.36
    putVar(kline, 880);      // high = 1350.94
    putVar(kline, -21700);   // low = 1328.36
    putU32(kline, 0);
    putU32(kline, 0);
    putU32(kline, 20250105);  // 事件前（2026-01-01 事件）
    putVar(kline, -1308360);  // openOff = 20000 - 1328360 (lastLi=事件后 bar close)
    putVar(kline, 0);         // close = open = 20.0
    putVar(kline, 1000);      // high = open + 1.0
    putVar(kline, -1000);     // low = open - 1.0
    putU32(kline, 0);
    putU32(kline, 0);
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Kline)] = kline;

    // gbbq：2026-01-01 事件，10送10（songZhuanGu=10 → m=2）
    std::vector<uint8_t> gbbq;
    gbbq.insert(gbbq.end(), 9, 0x00);
    putU16(gbbq, 1);                 // count
    gbbq.push_back(0x00);            // exchange
    gbbq.insert(gbbq.end(), {'6', '0', '0', '5', '1', '9'});
    gbbq.push_back(0x00);
    putU32(gbbq, 20260101);          // 事件日
    gbbq.push_back(0x01);            // category = 1（除权除息）
    // [13:29] 4×float32: fenHong, peiGuJia, songZhuanGu, peiGu
    // fenHong = 0.0f
    gbbq.insert(gbbq.end(), {0x00, 0x00, 0x00, 0x00});
    // peiGuJia = 0.0f
    gbbq.insert(gbbq.end(), {0x00, 0x00, 0x00, 0x00});
    // songZhuanGu = 10.0f
    gbbq.insert(gbbq.end(), {0x00, 0x00, 0x20, 0x41});
    // peiGu = 0.0f
    gbbq.insert(gbbq.end(), {0x00, 0x00, 0x00, 0x00});
    st->payloads[static_cast<uint16_t>(tdx::Cmd::Gbbq)] = gbbq;

    connectWithFake(provider, st);
    auto bars = provider.getBars(StockCode(Market::SH, "600519"),
                                 BarPeriod::Daily, DateTime{}, utils::now());
    ASSERT_EQ(bars.size(), 2u);
    // 2025-01-05（事件前）：10送10 → 价格 ÷2 → 20.0 → 10.0
    EXPECT_EQ(st::utils::toDateString(bars[0].time), "2025-01-05");
    EXPECT_NEAR(bars[0].close, 10.0, 1e-3);
    EXPECT_NEAR(bars[0].high, 10.5, 1e-3);
    // 2026-08-04（事件后）：不复权
    EXPECT_EQ(st::utils::toDateString(bars[1].time), "2026-08-04");
    EXPECT_NEAR(bars[1].close, 1328.36, 1e-3);
    provider.disconnect();
}

// ============================================================
// connect failover：首个服务器 open 失败，第二个成功
// ============================================================
TEST(TdxProviderTest, ConnectFailover) {
    TdxProvider provider;
    auto stA = std::make_shared<FakeState>();
    auto stB = std::make_shared<FakeState>();
    stA->failOpen = true;  // 首个服务器连接失败
    int calls = 0;
    provider.setServers({"serverA:7709", "serverB:7709"});
    provider.setPollIntervalMs(60000);
    provider.setTransportFactory([&]() -> std::unique_ptr<tdx::TdxTransport> {
        return std::make_unique<FakeTdxTransport>((calls++ == 0) ? stA : stB);
    });
    provider.connect();
    EXPECT_TRUE(waitConnected(provider));
    EXPECT_TRUE(provider.isConnected());
    EXPECT_EQ(calls, 2);  // 试了两个服务器
    provider.disconnect();
}

// ============================================================
// 订阅/退订去重 + poll 请求体观察
// ============================================================
TEST(TdxProviderTest, SubscribeUnsubscribeDedup) {
    TdxProvider provider;
    auto st = std::make_shared<FakeState>();
    connectWithFake(provider, st);

    const StockCode a(Market::SH, "600519");
    const StockCode b(Market::SZ, "000001");
    provider.subscribeQuote(a);
    provider.subscribeQuote(a);  // 重复订阅去重
    provider.subscribeQuote(b);
    provider.unsubscribeQuote(a);

    const int before = st->sendsByCmd[static_cast<uint16_t>(tdx::Cmd::Quote)];
    provider.refreshQuotes();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(2000);
    while (st->sendsByCmd[static_cast<uint16_t>(tdx::Cmd::Quote)] <= before &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // 只订阅了 b（a 已退订），请求体应含 000001 且不含 600519
    const auto& req = st->lastRequest;
    ASSERT_GE(req.size(), 12u);
    const std::string body(req.begin() + 12, req.end());
    EXPECT_NE(body.find("000001"), std::string::npos);
    EXPECT_EQ(body.find("600519"), std::string::npos);
    provider.disconnect();
}
