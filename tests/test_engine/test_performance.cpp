#include <gtest/gtest.h>
#include "engine/backtest/performance.h"

using namespace st;

namespace {

Trade makeTrade(const StockCode& code, Direction dir, Price price, Volume vol,
                Amount fee = 0.0) {
    Trade t;
    t.code = code;
    t.direction = dir;
    t.price = price;
    t.volume = vol;
    t.amount = price * vol;
    t.totalFee = fee;
    return t;
}

} // namespace

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

// ============================================================
// computeTradeStats — FIFO 配对交易统计
// ============================================================
TEST(PerformanceTest, TradeStatsSingleWin) {
    StockCode code(Market::SH, "600519");
    std::vector<Trade> trades;
    trades.push_back(makeTrade(code, Direction::Buy, 10.0, 100));   // 成本 1000
    trades.push_back(makeTrade(code, Direction::Sell, 12.0, 100));  // 回款 1200
    auto ts = PerformanceCalculator::computeTradeStats(trades);
    EXPECT_EQ(ts.totalTrades, 1);
    EXPECT_EQ(ts.winningTrades, 1);
    EXPECT_NEAR(ts.winRate, 100.0, 0.01);
    EXPECT_NEAR(ts.totalPnl, 200.0, 0.01);
    // 全部盈利无亏损 → 盈亏比上限
    EXPECT_NEAR(ts.profitFactor, 999.0, 0.01);
}

TEST(PerformanceTest, TradeStatsWinAndLoss) {
    StockCode code(Market::SH, "600519");
    std::vector<Trade> trades;
    trades.push_back(makeTrade(code, Direction::Buy, 10.0, 100));
    trades.push_back(makeTrade(code, Direction::Sell, 12.0, 100));  // 盈利 200
    trades.push_back(makeTrade(code, Direction::Buy, 20.0, 100));
    trades.push_back(makeTrade(code, Direction::Sell, 15.0, 100));  // 亏损 500
    auto ts = PerformanceCalculator::computeTradeStats(trades);
    EXPECT_EQ(ts.totalTrades, 2);
    EXPECT_EQ(ts.winningTrades, 1);
    EXPECT_NEAR(ts.winRate, 50.0, 0.01);
    EXPECT_NEAR(ts.profitFactor, 200.0 / 500.0, 0.01);
    EXPECT_NEAR(ts.totalPnl, -300.0, 0.01);
}

TEST(PerformanceTest, TradeStatsEmpty) {
    std::vector<Trade> trades;
    auto ts = PerformanceCalculator::computeTradeStats(trades);
    EXPECT_EQ(ts.totalTrades, 0);
    EXPECT_EQ(ts.winningTrades, 0);
    EXPECT_EQ(ts.winRate, 0.0);
    EXPECT_EQ(ts.profitFactor, 0.0);
    EXPECT_EQ(ts.totalPnl, 0.0);
}

TEST(PerformanceTest, TradeStatsFifoPartial) {
    // FIFO: 两笔买入，卖出一笔部分 → 只配对最早买入
    StockCode code(Market::SH, "600519");
    std::vector<Trade> trades;
    trades.push_back(makeTrade(code, Direction::Buy, 10.0, 100));
    trades.push_back(makeTrade(code, Direction::Buy, 20.0, 100));
    trades.push_back(makeTrade(code, Direction::Sell, 12.0, 50));  // 配最早 100@10 → 盈利 100
    auto ts = PerformanceCalculator::computeTradeStats(trades);
    EXPECT_EQ(ts.totalTrades, 1);
    EXPECT_EQ(ts.winningTrades, 1);
    EXPECT_NEAR(ts.totalPnl, 100.0, 0.01);
}
