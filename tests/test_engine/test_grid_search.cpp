#include <gtest/gtest.h>
#include "engine/optimizer/grid_search.h"
#include "engine/backtest/backtest_engine.h"
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

GridSearchConfig makeConfig(DataCache* cache, int lanes, Objective obj) {
    GridSearchConfig cfg;
    cfg.strategyId = "MACross";
    cfg.ranges = {{"fastPeriod", 2, 6, 2}, {"slowPeriod", 10, 30, 10}};
    cfg.startDate = utils::parseDate("2024-01-02");
    cfg.endDate = utils::parseDate("2024-12-31");
    cfg.cache = cache;
    cfg.parallelLanes = lanes;
    cfg.objective = obj;
    cfg.feeConfig = FeeConfig::defaultAShare();
    return cfg;
}

} // namespace

TEST(GridSearchTest, GeneratesCombinations) {
    GridSearchConfig cfg;
    cfg.ranges = {{"fastPeriod", 1, 3, 1}, {"slowPeriod", 5, 15, 5}};
    auto combos = GridSearchOptimizer::generateCombinations(cfg);
    // 3 × 3 = 9 组
    ASSERT_EQ(combos.size(), 9u);
    // 每组 2 个参数
    EXPECT_EQ(combos[0].size(), 2u);
    EXPECT_EQ(combos[0][0].first, "fastPeriod");
    EXPECT_EQ(combos[0][1].first, "slowPeriod");
}

TEST(GridSearchTest, GeneratesEmptyWhenNoRanges) {
    GridSearchConfig cfg;
    auto combos = GridSearchOptimizer::generateCombinations(cfg);
    ASSERT_EQ(combos.size(), 1u);  // 一个空组合
    EXPECT_TRUE(combos[0].empty());
}

TEST(GridSearchTest, SortsByObjectiveDescending) {
    StockCode code(Market::SH, "600519");
    // 先跌后涨的振荡行情：MACross 能盈利，且不同参数收益不同
    std::vector<double> closes;
    for (int i = 0; i < 60; ++i) closes.push_back(100.0 + 0.5 * i);
    for (int i = 0; i < 60; ++i) closes.push_back(130.0 - 0.3 * i);
    for (int i = 0; i < 60; ++i) closes.push_back(112.0 + 0.6 * i);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, makeSeries(code, closes));

    auto cfg = makeConfig(&cache, 1, Objective::TotalReturn);
    cfg.symbols = {code};

    GridSearchOptimizer opt;
    auto results = opt.run(cfg);

    // 3 × 3 = 9 组全跑
    ASSERT_EQ(results.size(), 9u);
    // 全部成功
    for (const auto& r : results) EXPECT_TRUE(r.success);
    // 按总收益降序
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_GE(results[i - 1].objectiveValue, results[i].objectiveValue);
    }
    // 净值曲线非空
    EXPECT_FALSE(results[0].equityCurve.empty());
}

TEST(GridSearchTest, ObjectiveMapping) {
    Performance p;
    p.totalReturn = 25.0;
    p.maxDrawdown = 12.0;
    p.sharpeRatio = 1.5;
    EXPECT_EQ(GridSearchOptimizer::objectiveValue(p, Objective::TotalReturn), 25.0);
    EXPECT_EQ(GridSearchOptimizer::objectiveValue(p, Objective::MaxDrawdown), 12.0);
    EXPECT_EQ(GridSearchOptimizer::objectiveValue(p, Objective::SharpeRatio), 1.5);
    EXPECT_TRUE(GridSearchOptimizer::objectiveMinimized(Objective::MaxDrawdown));
    EXPECT_FALSE(GridSearchOptimizer::objectiveMinimized(Objective::TotalReturn));
}

TEST(GridSearchTest, ReusesDataCache) {
    StockCode code(Market::SH, "600519");
    auto closes = std::vector<double>(120, 0.0);
    for (size_t i = 0; i < closes.size(); ++i) closes[i] = 100.0 + i;

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, makeSeries(code, closes));
    const size_t cacheSizeBefore = cache.size();

    auto cfg = makeConfig(&cache, 1, Objective::TotalReturn);
    cfg.symbols = {code};

    GridSearchOptimizer opt;
    auto results = opt.run(cfg);

    // 结果非空
    EXPECT_FALSE(results.empty());
    // 缓存大小不变（未做额外 IO/写入）
    EXPECT_EQ(cache.size(), cacheSizeBefore);
}

TEST(GridSearchTest, ParallelDeterminism) {
    StockCode code(Market::SH, "600519");
    std::vector<double> closes;
    for (int i = 0; i < 60; ++i) closes.push_back(100.0 + 0.5 * i);
    for (int i = 0; i < 60; ++i) closes.push_back(130.0 - 0.3 * i);
    for (int i = 0; i < 60; ++i) closes.push_back(112.0 + 0.6 * i);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, makeSeries(code, closes));

    auto cfg1 = makeConfig(&cache, 1, Objective::SharpeRatio);
    cfg1.symbols = {code};
    auto cfg2 = makeConfig(&cache, 4, Objective::SharpeRatio);
    cfg2.symbols = {code};

    GridSearchOptimizer opt;
    auto r1 = opt.run(cfg1);
    auto r2 = opt.run(cfg2);

    ASSERT_EQ(r1.size(), r2.size());
    // 最优参数组合一致（确定性）
    EXPECT_EQ(r1.front().params, r2.front().params);
}
