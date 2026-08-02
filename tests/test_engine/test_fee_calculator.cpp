#include <gtest/gtest.h>
#include "engine/backtest/fee_calculator.h"

using namespace st;

namespace {
Trade makeTrade(double price, int volume, Direction dir) {
    Trade t;
    t.price = price;
    t.volume = volume;
    t.direction = dir;
    return t;
}
}

TEST(FeeCalculatorTest, CommissionRate) {
    FeeCalculator calc(FeeConfig::defaultAShare());
    auto t = makeTrade(100.0, 1000, Direction::Buy);  // 成交额 10万
    auto fees = calc.calculate(t);
    // 佣金 = 100000 * 0.00025 = 25元 (>最低5元)
    EXPECT_NEAR(fees.commission, 25.0, 0.01);
}

TEST(FeeCalculatorTest, MinCommission) {
    FeeCalculator calc(FeeConfig::defaultAShare());
    auto t = makeTrade(10.0, 100, Direction::Buy);  // 成交额 1000
    auto fees = calc.calculate(t);
    // 佣金 = 1000 * 0.00025 = 0.25 < 5元，取最低5元
    EXPECT_NEAR(fees.commission, 5.0, 0.01);
}

TEST(FeeCalculatorTest, StampTaxOnlyOnSell) {
    FeeCalculator calc(FeeConfig::defaultAShare());
    auto buy = makeTrade(100.0, 1000, Direction::Buy);
    auto sell = makeTrade(100.0, 1000, Direction::Sell);

    EXPECT_EQ(calc.calculate(buy).stampTax, 0.0);
    // 卖出印花税 = 100000 * 0.001 = 100
    EXPECT_NEAR(calc.calculate(sell).stampTax, 100.0, 0.01);
}

TEST(FeeCalculatorTest, TransferFee) {
    FeeCalculator calc(FeeConfig::defaultAShare());
    auto t = makeTrade(100.0, 1000, Direction::Buy);  // 成交额 10万
    auto fees = calc.calculate(t);
    // 过户费 = 100000 * 0.00002 = 2元
    EXPECT_NEAR(fees.transferFee, 2.0, 0.01);
}

TEST(FeeCalculatorTest, TotalFees) {
    FeeCalculator calc(FeeConfig::defaultAShare());
    auto t = makeTrade(100.0, 1000, Direction::Buy);
    auto fees = calc.calculate(t);
    // 佣金25 + 过户2 + 经手4.87 + 证管2 = 33.87
    EXPECT_NEAR(fees.total, 33.87, 0.1);
}

TEST(FeeCalculatorTest, EtfTemplate) {
    FeeCalculator calc(FeeConfig::etf());
    auto t = makeTrade(100.0, 1000, Direction::Sell);
    auto fees = calc.calculate(t);
    // ETF: 无印花税
    EXPECT_EQ(fees.stampTax, 0.0);
    EXPECT_EQ(fees.transferFee, 0.0);
}

TEST(FeeCalculatorTest, InvalidTradeReturnsZero) {
    FeeCalculator calc;
    auto t = makeTrade(0.0, 0, Direction::Buy);
    auto fees = calc.calculate(t);
    EXPECT_EQ(fees.total, 0.0);
}
