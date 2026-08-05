#include <gtest/gtest.h>
#include "intelligence/screener/pattern_factor.h"
#include "engine/screener/stock_screener.h"
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"

#include <memory>
#include <utility>
#include <vector>

using namespace st;
using namespace st::screener;

namespace {

/// 单调上涨日线序列
std::vector<Bar> risingBars(int n, double start = 100.0, double step = 2.0) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        const double price = start + step * i;
        bar.open = price;
        bar.close = price + 0.5;
        bar.high = price + 1.0;
        bar.low = price;
        bar.volume = 100000;
        bars.push_back(bar);
    }
    return bars;
}

/// 单调下跌日线序列
std::vector<Bar> fallingBars(int n, double start = 200.0, double step = 2.0) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        const double price = start - step * i;
        bar.open = price;
        bar.close = price - 0.5;
        bar.high = price + 0.5;
        bar.low = price - 1.0;
        bar.volume = 100000;
        bars.push_back(bar);
    }
    return bars;
}

} // namespace

TEST(PatternFactorTest, NameAndCategory) {
    PatternFactor factor;
    EXPECT_EQ(factor.name(), "pattern_score");
    EXPECT_EQ(factor.category(), FactorCategory::Momentum);
}

TEST(PatternFactorTest, RisingSeriesScoresAboveNeutral) {
    auto bars = risingBars(60);
    BarSeries series(std::move(bars));
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    PatternFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 50.0);
    EXPECT_LE(*value, 100.0);
}

TEST(PatternFactorTest, FallingSeriesScoresBelowNeutral) {
    auto bars = fallingBars(60);
    BarSeries series(std::move(bars));
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    PatternFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_LT(*value, 50.0);
    EXPECT_GE(*value, 0.0);
}

TEST(PatternFactorTest, InsufficientDataReturnsNull) {
    auto bars = risingBars(45);  // < 50 根 → 数据不足
    BarSeries series(std::move(bars));
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    PatternFactor factor;
    EXPECT_FALSE(factor.calculate(ctx).has_value());
}

TEST(PatternFactorTest, IntegratesWithStockScreener) {
    StockCode code(Market::SH, "600519");
    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, risingBars(60));

    ScreenerConfig config;
    config.period = BarPeriod::Daily;
    config.endDate = utils::parseDate("2024-05-01");
    config.lookbackDays = 90;
    config.topN = 10;

    StockScreener screener;
    screener.setConfig(config);
    screener.setDataCache(&cache);
    screener.addFactor(std::make_shared<PatternFactor>(), 1.0);

    auto results = screener.run({code});
    ASSERT_EQ(results.size(), 1u);
    bool found = false;
    for (const auto& fr : results[0].factorResults) {
        if (fr.name == "pattern_score") found = true;
    }
    EXPECT_TRUE(found);
}
