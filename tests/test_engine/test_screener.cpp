#include <gtest/gtest.h>
#include "engine/screener/stock_screener.h"
#include "engine/screener/factor_library.h"
#include "engine/screener/ranker.h"
#include "engine/screener/condition_filter.h"
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {

std::vector<Bar> makeSeries(const std::vector<double>& closes) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (size_t i = 0; i < closes.size(); ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, static_cast<int>(i));
        bar.open = bar.close = closes[i];
        bar.high = closes[i] + 0.5;
        bar.low = closes[i] - 0.5;
        bar.volume = 10000;
        bars.push_back(bar);
    }
    return bars;
}

std::vector<double> rising(int n, double start, double step) {
    std::vector<double> c(n);
    for (int i = 0; i < n; ++i) c[i] = start + step * i;
    return c;
}

std::vector<double> falling(int n, double start, double step) {
    std::vector<double> c(n);
    for (int i = 0; i < n; ++i) c[i] = start - step * i;
    return c;
}

} // namespace

TEST(RankerTest, ComputeTotalScore) {
    std::vector<FactorResult> results = {
        {"factor_a", 10.0, 80.0},
        {"factor_b", 5.0, 60.0},
    };
    std::vector<std::pair<std::string, double>> weights = {
        {"factor_a", 1.0}, {"factor_b", 1.0}
    };
    // (80*1 + 60*1) / 2 = 70
    EXPECT_DOUBLE_EQ(Ranker::computeTotalScore(results, weights), 70.0);
}

TEST(RankerTest, SortByScoreDescending) {
    std::vector<ScreenResult> results = {
        {StockCode(Market::SH, "600001"), 30.0, {}},
        {StockCode(Market::SH, "600002"), 90.0, {}},
        {StockCode(Market::SH, "600003"), 60.0, {}},
    };
    Ranker::sortByScore(results);
    EXPECT_DOUBLE_EQ(results[0].totalScore, 90.0);
    EXPECT_DOUBLE_EQ(results[2].totalScore, 30.0);
}

TEST(ConditionFilterTest, PassesAllConditions) {
    ConditionFilter filter;
    Condition cond;
    cond.factorName = "rsi_14";
    cond.minValue = 40.0;
    cond.maxValue = 80.0;
    filter.addCondition(cond);

    std::vector<FactorResult> results = {
        {"rsi_14", 55.0, 55.0},
    };
    EXPECT_TRUE(filter.passes(results));
}

TEST(ConditionFilterTest, FailsWhenOutOfRange) {
    ConditionFilter filter;
    Condition cond;
    cond.factorName = "rsi_14";
    cond.minValue = 40.0;
    cond.maxValue = 80.0;
    filter.addCondition(cond);

    std::vector<FactorResult> results = {
        {"rsi_14", 90.0, 90.0},  // 超出 max
    };
    EXPECT_FALSE(filter.passes(results));
}

TEST(ConditionFilterTest, MissingFactorFails) {
    ConditionFilter filter;
    Condition cond;
    cond.factorName = "nonexistent";
    cond.minValue = 10.0;
    filter.addCondition(cond);

    std::vector<FactorResult> results = {
        {"rsi_14", 55.0, 55.0},
    };
    EXPECT_FALSE(filter.passes(results));
}

TEST(StockScreenerTest, RanksByMomentum) {
    // 构造 3 只股票: 强势上涨、弱涨、下跌
    StockCode strong(Market::SH, "600001");
    StockCode weak(Market::SH, "600002");
    StockCode fallCode(Market::SH, "600003");

    DataCache cache;
    cache.cacheBars(strong, BarPeriod::Daily,
                    makeSeries(rising(60, 100.0, 1.0)));
    cache.cacheBars(weak, BarPeriod::Daily,
                    makeSeries(rising(60, 100.0, 0.2)));
    cache.cacheBars(fallCode, BarPeriod::Daily,
                    makeSeries(falling(60, 100.0, 1.0)));

    ScreenerConfig config;
    config.period = BarPeriod::Daily;
    config.endDate = utils::parseDate("2024-05-01");
    config.lookbackDays = 90;
    config.topN = 10;

    StockScreener screener;
    screener.setConfig(config);
    screener.setDataCache(&cache);

    // 只加动量因子
    screener.addFactor(std::make_shared<factors::RocFactor>(), 1.0);

    auto results = screener.run({strong, weak, fallCode});
    ASSERT_EQ(results.size(), 3u);
    // 强势股应排第一
    EXPECT_EQ(results[0].code, strong);
}
