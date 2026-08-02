#include <gtest/gtest.h>
#include "engine/analyzer/strategy_comparator.h"
#include "engine/analyzer/stress_test.h"
#include "engine/analyzer/monte_carlo.h"
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {

std::vector<Bar> makeSeries(const StockCode& code, const std::vector<double>& closes) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (size_t i = 0; i < closes.size(); ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, static_cast<int>(i));
        bar.open = closes[i];
        bar.high = closes[i] * 1.01;
        bar.low = closes[i] * 0.99;
        bar.close = closes[i];
        bar.volume = 10000;
        bars.push_back(bar);
    }
    return bars;
}

} // namespace

// ============================================================
// StressTest
// ============================================================
TEST(StressTestTest, FallingWindowWorseThanRising) {
    StockCode code(Market::SH, "600519");
    // 先跌后涨：前 60 根下跌，后 60 根上涨
    std::vector<double> closes;
    for (int i = 0; i < 60; ++i) closes.push_back(100.0 - 0.4 * i);
    const double trough = closes.back();
    for (int i = 0; i < 60; ++i) closes.push_back(trough + 0.5 * i);

    const auto base = utils::parseDate("2024-01-02");
    const auto end = utils::addTradingDays(base, static_cast<int>(closes.size()) - 1);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, makeSeries(code, closes));

    StressTestConfig cfg;
    cfg.strategyId = "MACross";
    cfg.params = {{"fastPeriod", 5}, {"slowPeriod", 20}};
    cfg.symbols = {code};
    cfg.baselineStart = base;
    cfg.baselineEnd = end;
    cfg.cache = &cache;
    cfg.feeConfig = FeeConfig::defaultAShare();

    std::vector<StressWindow> windows = {
        {"fall", "下跌段", base, utils::addTradingDays(base, 59)},
        {"rise", "上涨段", utils::addTradingDays(base, 60), end},
    };

    StressTest st;
    auto out = st.run(cfg, windows);

    ASSERT_EQ(out.windows.size(), 2u);
    EXPECT_TRUE(out.windows[0].success);
    EXPECT_TRUE(out.windows[1].success);
    EXPECT_FALSE(out.windows[0].equityCurve.empty());
    EXPECT_FALSE(out.windows[1].equityCurve.empty());
    // 下跌段表现应不优于上涨段
    EXPECT_LE(out.windows[0].performance.totalReturn,
              out.windows[1].performance.totalReturn);
    // 基线跑通
    EXPECT_TRUE(out.baseline.success);
    EXPECT_FALSE(out.baseline.equityCurve.empty());
}

TEST(StressTestTest, DefaultWindowsCount) {
    EXPECT_EQ(StressTest::defaultWindows().size(), 5u);
}

// ============================================================
// StrategyComparator
// ============================================================
TEST(ComparatorTest, SortsByReturnDescending) {
    StockCode code(Market::SH, "600519");
    // 稳定上涨行情
    std::vector<double> closes;
    for (int i = 0; i < 120; ++i) closes.push_back(100.0 + 0.5 * i);

    const auto base = utils::parseDate("2024-01-02");
    const auto end = utils::addTradingDays(base, static_cast<int>(closes.size()) - 1);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, makeSeries(code, closes));

    ComparisonConfig cfg;
    cfg.items = {
        {"MA5/20", "MACross", {{"fastPeriod", 5}, {"slowPeriod", 20}}},
        {"Turtle20/10", "Turtle", {{"entryPeriod", 20}, {"exitPeriod", 10}}},
    };
    cfg.symbols = {code};
    cfg.startDate = base;
    cfg.endDate = end;
    cfg.cache = &cache;
    cfg.feeConfig = FeeConfig::defaultAShare();

    StrategyComparator comp;
    auto results = comp.run(cfg);

    ASSERT_EQ(results.size(), 2u);
    for (const auto& r : results) {
        EXPECT_TRUE(r.success);
        EXPECT_FALSE(r.equityCurve.empty());
    }
    // 按总收益降序
    EXPECT_GE(results[0].performance.totalReturn,
              results[1].performance.totalReturn);
}

// ============================================================
// MonteCarlo
// ============================================================
TEST(MonteCarloTest, ConstantReturns) {
    MonteCarlo::Input in;
    in.dailyReturns = std::vector<double>(20, 0.0);
    in.iterations = 100;
    auto out = MonteCarlo::simulate(in);
    ASSERT_EQ(out.finals.size(), 100u);
    EXPECT_NEAR(out.p5, 1.0, 1e-9);
    EXPECT_NEAR(out.p50, 1.0, 1e-9);
    EXPECT_NEAR(out.p95, 1.0, 1e-9);
    EXPECT_NEAR(out.probOfLoss, 0.0, 1e-9);
}

TEST(MonteCarloTest, RisingReturnsDistribution) {
    MonteCarlo::Input in;
    in.dailyReturns = std::vector<double>(20, 0.01);   // 每天 +1%
    in.iterations = 200;
    auto out = MonteCarlo::simulate(in);
    // 恒正收益 → 期末净值 > 1
    EXPECT_GT(out.p50, 1.0);
    // 分位有序
    EXPECT_LE(out.p5, out.p50);
    EXPECT_LE(out.p50, out.p95);
    EXPECT_DOUBLE_EQ(out.probOfLoss, 0.0);
}

TEST(MonteCarloTest, EmptyInput) {
    MonteCarlo::Input in;
    auto out = MonteCarlo::simulate(in);
    EXPECT_TRUE(out.finals.empty());
}
