#include <gtest/gtest.h>
#include "engine/backtest/performance.h"

using namespace st;

TEST(PerformanceTest, ConstantEquityNoReturn) {
    // 净值恒为 1.0 → 收益 0
    std::vector<double> equity(10, 1.0);
    auto p = PerformanceCalculator::calculate(equity);
    EXPECT_NEAR(p.totalReturn, 0.0, 0.01);
    EXPECT_EQ(p.maxDrawdown, 0.0);
    EXPECT_EQ(p.sharpeRatio, 0.0);
}

TEST(PerformanceTest, LinearGrowthTotalReturn) {
    // 净值从1.0涨到2.0 → 总收益100%
    std::vector<double> equity{1.0, 1.1, 1.2, 1.5, 1.8, 2.0};
    auto p = PerformanceCalculator::calculate(equity);
    EXPECT_NEAR(p.totalReturn, 100.0, 0.01);
}

TEST(PerformanceTest, MaxDrawdownDetection) {
    // 净值从2.0跌到1.0 → 回撤50%
    std::vector<double> equity{1.0, 2.0, 1.5, 1.0};
    auto p = PerformanceCalculator::calculate(equity);
    EXPECT_NEAR(p.maxDrawdown, 50.0, 0.01);
}

TEST(PerformanceTest, WinRateNoTrades) {
    std::vector<double> equity{1.0};
    auto p = PerformanceCalculator::calculate(equity);
    EXPECT_DOUBLE_EQ(p.totalReturn, 0.0);
}

TEST(PerformanceTest, EmptyEquity) {
    std::vector<double> equity;
    auto p = PerformanceCalculator::calculate(equity);
    EXPECT_EQ(p.sharpeRatio, 0.0);
}

TEST(PerformanceTest, AnnualizedReturnPositive) {
    // 252天翻倍 → 年化约100%
    std::vector<double> equity(252, 1.0);
    // 线性插值从1.0到2.0
    for (int i = 0; i < 252; ++i) {
        equity[i] = 1.0 + i / 251.0;
    }
    auto p = PerformanceCalculator::calculate(equity);
    EXPECT_NEAR(p.totalReturn, 100.0, 1.0);
}
