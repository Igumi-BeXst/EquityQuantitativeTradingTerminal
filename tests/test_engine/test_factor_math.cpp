#include <gtest/gtest.h>

#include "engine/screener/factor_math.h"
#include "foundation/utils/datetime.h"

#include <cmath>
#include <vector>

using namespace st;
using namespace st::factors::math;

namespace {

void expectNear(double actual, double expected, double eps = 1e-9) {
    EXPECT_NEAR(actual, expected, eps);
}

BarSeries makeBars(const std::vector<double>& closes, double volume = 1000.0,
                   double amount = 0.0) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (size_t i = 0; i < closes.size(); ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, static_cast<int>(i));
        bar.open = closes[i];
        bar.high = closes[i] + 0.5;
        bar.low = closes[i] - 0.5;
        bar.close = closes[i];
        bar.volume = static_cast<Volume>(volume);
        bar.amount = amount;
        bars.push_back(bar);
    }
    return BarSeries(std::move(bars));
}

}  // namespace

TEST(FactorMathTest, DelayAndDelta) {
    Series a = {1, 2, 3, 4, 5};
    Series d = delay(a, 2);
    ASSERT_EQ(d.size(), a.size());
    EXPECT_TRUE(std::isnan(d[0]));
    EXPECT_TRUE(std::isnan(d[1]));
    expectNear(d[2], 1);
    expectNear(d[3], 2);
    expectNear(d[4], 3);

    Series dd = delta(a, 2);
    EXPECT_TRUE(std::isnan(dd[1]));
    expectNear(dd[2], 2);
    expectNear(dd[4], 2);
}

TEST(FactorMathTest, RollingSumMeanStd) {
    Series a = {1, 2, 3, 4, 5};
    Series sum = tsSum(a, 3);
    EXPECT_TRUE(std::isnan(sum[1]));
    expectNear(sum[2], 6);
    expectNear(sum[4], 12);

    Series mean = tsMean(a, 3);
    expectNear(mean[2], 2);
    expectNear(mean[4], 4);

    Series sd = tsStd(a, 3);
    // 窗口 {1,2,3}：总体标准差 sqrt(((1-2)^2+(0)^2+(1)^2)/3)
    expectNear(sd[2], std::sqrt(2.0 / 3.0));
}

TEST(FactorMathTest, RollingMaxMin) {
    Series a = {5, 1, 3, 2, 4};
    Series mx = tsMax(a, 3);
    Series mn = tsMin(a, 3);
    EXPECT_TRUE(std::isnan(mx[1]));
    expectNear(mx[2], 5);
    expectNear(mx[3], 3);
    expectNear(mx[4], 4);
    expectNear(mn[2], 1);
    expectNear(mn[3], 1);
    expectNear(mn[4], 2);
}

TEST(FactorMathTest, TsRankWithinWindow) {
    Series a = {1, 2, 3, 4};
    Series r = tsRank(a, 3);
    EXPECT_TRUE(std::isnan(r[1]));
    expectNear(r[2], 1.0);  // {1,2,3} 当前 3 最大
    expectNear(r[3], 1.0);  // {2,3,4} 当前 4 最大
}

TEST(FactorMathTest, CorrOfLinearSeries) {
    Series x = {1, 2, 3, 4, 5};
    Series y = {3, 5, 7, 9, 11};  // y = 2x + 1
    Series c = corr(x, y, 4);
    expectNear(c[3], 1.0);
    expectNear(c[4], 1.0);
}

TEST(FactorMathTest, DecayAndWmaValues) {
    Series a = {1, 2, 3};
    expectNear(lastValue(decayLinear(a, 3)), 14.0 / 6.0);
    expectNear(lastValue(wma(a, 3)), 14.0 / 6.0);
}

TEST(FactorMathTest, ChineseSma) {
    Series a = {10, 20, 30};
    Series out = smaCn(a, 3, 1);
    expectNear(out[0], 10.0);
    expectNear(out[1], (20.0 + 2.0 * 10.0) / 3.0);
    expectNear(out[2], (30.0 + 2.0 * (20.0 + 2.0 * 10.0) / 3.0) / 3.0);
}

TEST(FactorMathTest, RegBetaOfLinearSeries) {
    Series a(10);
    for (size_t i = 0; i < a.size(); ++i) a[i] = 2.0 * static_cast<double>(i) + 5.0;
    Series beta = regBetaSeq(a, 6);
    expectNear(beta.back(), 2.0);
}

TEST(FactorMathTest, LowHighDay) {
    Series a = {5, 1, 3, 2, 4};
    Series low = lowDay(a, 3);   // 末窗口 {3,2,4}：低点 2 在昨日 → 1
    Series high = highDay(a, 3); // 末窗口 {3,2,4}：高点 4 在今天 → 0
    expectNear(low.back(), 1);
    expectNear(high.back(), 0);
}

TEST(FactorMathTest, ElementwiseOps) {
    Series a = {1, 2, 3};
    Series b = {2, 4, 6};
    expectNear(lastValue(mul(a, b)), 18);
    expectNear(lastValue(div(b, a)), 2);
    expectNear(lastValue(sub(b, a)), 3);
    expectNear(lastValue(addScalar(a, 1)), 4);
    // ewhere(cond, a, b)
    Series cond = {1, 0, 1};
    Series out = ewhere(cond, a, b);
    expectNear(out[0], 1);
    expectNear(out[1], 4);
    expectNear(out[2], 3);
    // egt
    Series gt = egt(a, b);
    expectNear(gt[0], 0);
    expectNear(gt[2], 0);
}

TEST(FactorMathTest, VwapSeriesUsesAmountOrFallback) {
    BarSeries withAmount = makeBars({10, 11}, 100.0, 1000.0);  // vwap = 10
    expectNear(lastValue(vwapSeries(withAmount)), 10.0);

    BarSeries noAmount = makeBars({10, 11}, 100.0, 0.0);  // 回退 (H+L+C)/3
    expectNear(lastValue(vwapSeries(noAmount)), (11.0 + 0.5 + 11.0 - 0.5 + 11.0) / 3.0);
}

TEST(FactorMathTest, LastValidSkipsTrailingNaN) {
    Series a = {1, 2, kNaN};
    auto v = lastValid(a);
    ASSERT_TRUE(v.has_value());
    expectNear(*v, 2.0);
    EXPECT_FALSE(lastValid({kNaN}).has_value());
}
