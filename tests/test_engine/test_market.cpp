#include <gtest/gtest.h>
#include "engine/market/market_scanner.h"
#include "engine/market/market_breadth.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {

BarSeries makeSeries(const std::vector<double>& closes) {
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
    return BarSeries(std::move(bars));
}

} // namespace

TEST(MarketScannerTest, CalculateChangePct) {
    auto series = makeSeries({100.0, 105.0});
    EXPECT_NEAR(MarketScanner::calculateChangePct(series), 5.0, 0.01);
}

TEST(MarketScannerTest, ChangePctNegative) {
    auto series = makeSeries({100.0, 95.0});
    EXPECT_NEAR(MarketScanner::calculateChangePct(series), -5.0, 0.01);
}

TEST(MarketScannerTest, InsufficientDataZero) {
    auto series = makeSeries({100.0});
    EXPECT_DOUBLE_EQ(MarketScanner::calculateChangePct(series), 0.0);
}

TEST(MarketScannerTest, ScanSortsDescending) {
    StockCode a(Market::SH, "600001");
    StockCode b(Market::SH, "600002");
    StockCode c(Market::SH, "600003");

    std::vector<std::pair<StockCode, BarSeries>> inputs = {
        {a, makeSeries({100.0, 110.0})},   // +10%
        {b, makeSeries({100.0, 102.0})},   // +2%
        {c, makeSeries({100.0, 98.0})},    // -2%
    };
    auto board = MarketScanner::scan(inputs);
    ASSERT_EQ(board.size(), 3u);
    EXPECT_EQ(board[0].code, a);  // 涨幅最大排第一
    EXPECT_EQ(board[2].code, c);  // 跌幅排最后
}

TEST(MarketScannerTest, TopNLimit) {
    StockCode a(Market::SH, "600001");
    StockCode b(Market::SH, "600002");
    std::vector<std::pair<StockCode, BarSeries>> inputs = {
        {a, makeSeries({100.0, 110.0})},
        {b, makeSeries({100.0, 98.0})},
    };
    auto top = MarketScanner::scan(inputs, 1);
    EXPECT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0].code, a);
}

TEST(MarketBreadthTest, CountsAdvancingDeclining) {
    StockCode a(Market::SH, "600001");
    StockCode b(Market::SH, "600002");
    StockCode c(Market::SH, "600003");

    std::vector<std::pair<StockCode, BarSeries>> inputs = {
        {a, makeSeries({100.0, 101.0})},  // 上涨
        {b, makeSeries({100.0, 99.0})},   // 下跌
        {c, makeSeries({100.0, 100.0})},  // 平盘
    };
    auto data = MarketBreadth::calculate(inputs);
    EXPECT_EQ(data.advancing, 1);
    EXPECT_EQ(data.declining, 1);
    EXPECT_EQ(data.unchanged, 1);
    EXPECT_DOUBLE_EQ(data.advanceRatio(), 0.5);
}

TEST(MarketBreadthTest, AdlCalculation) {
    StockCode a(Market::SH, "600001");
    StockCode b(Market::SH, "600002");
    std::vector<std::pair<StockCode, BarSeries>> inputs = {
        {a, makeSeries({100.0, 101.0})},
        {b, makeSeries({100.0, 99.0})},
    };
    auto data = MarketBreadth::calculate(inputs);
    EXPECT_EQ(MarketBreadth::adl(data), 0);  // 1涨1跌
}
