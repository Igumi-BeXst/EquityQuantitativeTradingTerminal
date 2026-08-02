#include <gtest/gtest.h>
#include "engine/backtest/backtest_engine.h"
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {

// 简单策略: 第一个Bar全仓买入，持有到结束
class BuyAndHoldStrategy : public IStrategy {
public:
    std::string name() const override { return "BuyAndHold"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        if (!bought_) {
            auto available = ctx.portfolio->cash;
            buyByAmount(available * 0.9);  // 90% 资金买入
            bought_ = true;
        }
    }
private:
    bool bought_ = false;
};

std::vector<Bar> makeRisingSeries(const StockCode& code, int n, double startPrice) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        bar.open = startPrice + i;
        bar.high = startPrice + i + 0.5;
        bar.low = startPrice + i - 0.5;
        bar.close = startPrice + i;
        bar.volume = 10000;
        bars.push_back(bar);
    }
    return bars;
}

} // namespace

TEST(BacktestEngineTest, BuyAndHoldRisingMarket) {
    StockCode code(Market::SH, "600519");
    auto bars = makeRisingSeries(code, 20, 100.0);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars.front().time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Daily;

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);
    engine.addStrategy(std::make_shared<BuyAndHoldStrategy>());

    auto result = engine.run();

    ASSERT_TRUE(result.success) << result.error;
    // 上涨市场买入持有 → 正收益
    EXPECT_GT(result.performance.totalReturn, 0.0);
    EXPECT_FALSE(result.trades.empty());
    // 有交易发生
    EXPECT_GT(result.trades.size(), 0u);
    // 期末总资产应大于初始（上涨）
    EXPECT_GT(result.finalPortfolio.totalAsset, config.initialCapital);
}

TEST(BacktestEngineTest, NoDataReturnsError) {
    StockCode code(Market::SH, "600519");
    DataCache cache;  // 空缓存

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = utils::parseDate("2024-01-01");
    config.endDate = utils::parseDate("2024-12-31");

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);

    auto result = engine.run();
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}
