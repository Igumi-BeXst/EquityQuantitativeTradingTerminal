#include <gtest/gtest.h>
#include "core/config_manager.h"
#include "data/multi_provider.h"
#include "data/provider_factory.h"
#include "foundation/utils/datetime.h"

#include <memory>
#include <string>
#include <vector>

using namespace st;

namespace {

/// 可配置桩数据源
class StubProvider : public IDataProvider {
public:
    std::string name = "stub";
    bool connectOk = true;
    bool connected = true;
    bool connectMakesConnected = false;
    int connectCount = 0;
    int getBarsCount = 0;
    std::vector<Bar> bars;
    std::vector<Quote> quotes;
    std::vector<StockInfo> stocks;

    std::string providerName() const override { return name; }
    bool connect() override {
        ++connectCount;
        if (connectOk && connectMakesConnected) connected = true;
        return connectOk;
    }
    void disconnect() override { connected = false; }
    bool isConnected() const override { return connected; }
    std::optional<StockInfo> getStockInfo(const StockCode&) override {
        return stocks.empty() ? std::nullopt : std::make_optional(stocks.front());
    }
    std::vector<StockInfo> getStockList(Market) override { return stocks; }
    std::vector<Bar> getBars(const StockCode&, BarPeriod, DateTime, DateTime) override {
        ++getBarsCount;
        return bars;
    }
    void subscribeQuote(const StockCode&) override {}
    void unsubscribeQuote(const StockCode&) override {}
    std::vector<Quote> batchQuote(const std::vector<StockCode>&) override { return quotes; }
    std::optional<IntradayData> getIntraday(const StockCode&) override { return std::nullopt; }
    void refreshQuotes() override {}
};

Bar makeBar(double close) {
    Bar b;
    b.close = close;
    b.volume = 1;
    return b;
}

Quote makeQuote(const std::string& code) {
    Quote q;
    q.code = StockCode(code);
    q.lastPrice = 10.0;
    return q;
}

StockInfo makeStock(const std::string& code) {
    StockInfo info;
    info.code = StockCode(code);
    info.name = "test";
    info.valid = true;
    return info;
}

std::unique_ptr<MultiProvider> makePair(StubProvider*& a, StubProvider*& b) {
    auto pa = std::make_unique<StubProvider>();
    auto pb = std::make_unique<StubProvider>();
    a = pa.get();
    b = pb.get();
    return std::make_unique<MultiProvider>(std::move(pa), std::move(pb));
}

} // namespace

TEST(MultiProviderTest, PrimaryDataUsed) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->bars = {makeBar(1.0)};
    pb->bars = {makeBar(2.0)};

    auto bars = multi->getBars(StockCode("600519"), BarPeriod::Daily,
                               utils::parseDate("2020-01-01"), utils::parseDate("2020-12-31"));
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 1.0);  // 主源数据被采用
}

TEST(MultiProviderTest, PrimaryEmptyFallsBack) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->bars = {};                 // 主源空
    pb->bars = {makeBar(2.0)};
    pb->quotes = {makeQuote("600519")};
    pb->stocks = {makeStock("600519")};

    const auto start = utils::parseDate("2020-01-01");
    const auto end = utils::parseDate("2020-12-31");
    EXPECT_EQ(multi->getBars(StockCode("600519"), BarPeriod::Daily, start, end).size(), 1u);
    EXPECT_EQ(multi->batchQuote({StockCode("600519")}).size(), 1u);
    EXPECT_EQ(multi->getStockList(Market::SH).size(), 1u);
    EXPECT_TRUE(multi->getStockInfo(StockCode("600519")).has_value());
}

TEST(MultiProviderTest, PrimaryConnectFailsUsesFallback) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->connectOk = false;
    pa->connected = false;
    pb->connectOk = true;

    EXPECT_TRUE(multi->connect());
    EXPECT_TRUE(multi->isConnected());
}

TEST(MultiProviderTest, ConnectStartsStandbyForFailover) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);

    EXPECT_TRUE(multi->connect());
    EXPECT_EQ(pa->connectCount, 1);
    EXPECT_EQ(pb->connectCount, 1);
}

TEST(MultiProviderTest, AsyncPrimaryRetainsPriorityThenFailsOverAndRecovers) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->name = "tdx";
    pa->connected = false;  // connect() is accepted, but TDX completes asynchronously
    pb->name = "akshare";
    pb->bars = {makeBar(2.0)};

    ASSERT_TRUE(multi->connect());
    auto bars = multi->getBars(StockCode("600519"), BarPeriod::Daily, {}, {});
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(pa->getBarsCount, 1);  // First request still goes to the configured primary.
    EXPECT_EQ(pb->getBarsCount, 1);  // Empty primary response activates the standby.

    bars = multi->getBars(StockCode("600519"), BarPeriod::Daily, {}, {});
    EXPECT_EQ(pb->getBarsCount, 2);  // Keep using standby while primary is disconnected.
    EXPECT_EQ(pa->getBarsCount, 1);

    pa->connected = true;  // Primary health recovery restores its priority.
    pa->bars = {makeBar(1.0)};
    bars = multi->getBars(StockCode("600519"), BarPeriod::Daily, {}, {});
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 1.0);
    EXPECT_EQ(pa->getBarsCount, 2);
    EXPECT_EQ(pb->getBarsCount, 2);
}

TEST(MultiProviderTest, PreferredIsFallbackWhenPrimaryDisconnected) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->connected = false;         // 主源未连接
    pa->bars = {makeBar(1.0)};
    pb->connected = true;          // 备源已连接
    pb->bars = {makeBar(9.0)};

    auto bars = multi->getBars(StockCode("600519"), BarPeriod::Daily,
                               utils::parseDate("2020-01-01"), utils::parseDate("2020-12-31"));
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_DOUBLE_EQ(bars[0].close, 9.0);  // 首选备源
}

TEST(MultiProviderTest, ProviderNameContainsBoth) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->name = "tdx";
    pb->name = "tencent";

    const std::string n = multi->providerName();
    EXPECT_NE(n.find("tdx"), std::string::npos);
    EXPECT_NE(n.find("tencent"), std::string::npos);
    EXPECT_NE(n.find("multi"), std::string::npos);
}

TEST(MultiProviderTest, BothEmptyReturnsEmptyNoCrash) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);

    const auto start = utils::parseDate("2020-01-01");
    const auto end = utils::parseDate("2020-12-31");
    EXPECT_TRUE(multi->getBars(StockCode("600519"), BarPeriod::Daily, start, end).empty());
    EXPECT_TRUE(multi->batchQuote({StockCode("600519")}).empty());
    EXPECT_TRUE(multi->getStockList(Market::SH).empty());
    EXPECT_FALSE(multi->getStockInfo(StockCode("600519")).has_value());
    EXPECT_FALSE(multi->getIntraday(StockCode("600519")).has_value());
    EXPECT_FALSE(multi->getMarketDepth(StockCode("600519")).has_value());
}

TEST(MultiProviderTest, UnsupportedBarSemanticsDoNotReturnAdjustedBars) {
    StubProvider* pa = nullptr;
    StubProvider* pb = nullptr;
    auto multi = makePair(pa, pb);
    pa->bars = {makeBar(10.0)};
    pb->bars = {makeBar(20.0)};

    const auto start = utils::parseDate("2020-01-01");
    const auto end = utils::parseDate("2020-12-31");
    EXPECT_TRUE(multi->getRawBars(StockCode("600519"), BarPeriod::Daily,
                                  start, end).empty());
    EXPECT_TRUE(multi->getHfqBars(StockCode("600519"), BarPeriod::Daily,
                                  start, end).empty());
}

TEST(MultiProviderTest, DefaultProviderUsesTdxWithAkshareFailover) {
    auto* config = ConfigManager::instance();
    ASSERT_FALSE(config->load("missing-provider-factory-test-config.json"));

    auto provider = makeDataProvider();
    EXPECT_EQ(provider->providerName(), "multi(tdx→akshare)");
}
