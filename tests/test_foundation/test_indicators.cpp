#include <gtest/gtest.h>
#include "foundation/utils/indicators.h"
#include "foundation/utils/datetime.h"

#include <cmath>

using namespace st::indicators;

namespace {

bool isNan(double v) { return std::isnan(v); }

}  // namespace

TEST(IndicatorsTest, SmaBasic) {
    std::vector<double> v{1, 2, 3, 4, 5};
    auto r = sma(v, 3);
    ASSERT_EQ(r.size(), 5u);
    EXPECT_TRUE(isNan(r[0]));
    EXPECT_TRUE(isNan(r[1]));
    EXPECT_DOUBLE_EQ(r[2], 2.0);  // (1+2+3)/3
    EXPECT_DOUBLE_EQ(r[3], 3.0);
    EXPECT_DOUBLE_EQ(r[4], 4.0);
}

TEST(IndicatorsTest, SmaInsufficientData) {
    auto r = sma({1.0, 2.0}, 5);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_TRUE(isNan(r[0]));
    EXPECT_TRUE(isNan(r[1]));
    // 空输入
    EXPECT_TRUE(sma({}, 3).empty());
    // 非法周期
    auto r2 = sma({1.0, 2.0, 3.0}, 0);
    ASSERT_EQ(r2.size(), 3u);
}

TEST(IndicatorsTest, EmaManual) {
    // period=3, α=0.5; 种子 = 首3均值 = 2
    // ema[2]=2; ema[3]=0.5*4+0.5*2=3; ema[4]=0.5*5+0.5*3=4
    auto r = ema({1.0, 2.0, 3.0, 4.0, 5.0}, 3);
    EXPECT_TRUE(isNan(r[0]));
    EXPECT_TRUE(isNan(r[1]));
    EXPECT_DOUBLE_EQ(r[2], 2.0);
    EXPECT_DOUBLE_EQ(r[3], 3.0);
    EXPECT_DOUBLE_EQ(r[4], 4.0);
}

TEST(IndicatorsTest, MacdMonotonicForRisingSeries) {
    // 递增序列 → dif 递增（fast ema 上升快于 slow），hist 有定义
    std::vector<double> closes;
    for (int i = 1; i <= 60; ++i) closes.push_back(static_cast<double>(i));
    auto m = macd(closes);
    ASSERT_EQ(m.dif.size(), closes.size());
    EXPECT_TRUE(isNan(m.dif[0]));
    // 尾部 dif 为正（fast > slow）
    EXPECT_GT(m.dif.back(), 0.0);
    EXPECT_TRUE(std::isfinite(m.dea.back()));
    EXPECT_TRUE(std::isfinite(m.hist.back()));
}

TEST(IndicatorsTest, RsiAllUpIs100) {
    std::vector<double> closes;
    for (int i = 1; i <= 30; ++i) closes.push_back(static_cast<double>(i));
    auto r = rsi(closes, 14);
    EXPECT_DOUBLE_EQ(r.back(), 100.0);
}

TEST(IndicatorsTest, RsiAllDownIs0) {
    std::vector<double> closes;
    for (int i = 30; i >= 1; --i) closes.push_back(static_cast<double>(i));
    auto r = rsi(closes, 14);
    EXPECT_DOUBLE_EQ(r.back(), 0.0);
}

TEST(IndicatorsTest, RsiClassicWilder) {
    // 已知样本: 首 15 根收盘 [44.34,44.09,44.15,43.61,44.33,44.83,45.10,45.42,45.84,
    // 46.08,45.89,46.03,45.61,46.28,46.28] → RSI14 ≈ 70.53
    std::vector<double> closes{
        44.34, 44.09, 44.15, 43.61, 44.33, 44.83, 45.10, 45.42,
        45.84, 46.08, 45.89, 46.03, 45.61, 46.28, 46.28};
    auto r = rsi(closes, 14);
    ASSERT_EQ(r.size(), closes.size());
    EXPECT_NEAR(r.back(), 70.53, 0.5);
}

TEST(IndicatorsTest, BollMidEqualsSma) {
    std::vector<double> closes;
    for (int i = 1; i <= 30; ++i) closes.push_back(10.0 + i);
    auto b = boll(closes, 20, 2.0);
    auto mid = sma(closes, 20);
    ASSERT_EQ(b.mid.size(), closes.size());
    for (size_t i = 20; i < closes.size(); ++i) {
        EXPECT_DOUBLE_EQ(b.mid[i], mid[i]);
        EXPECT_GT(b.upper[i], b.mid[i]);
        EXPECT_LT(b.lower[i], b.mid[i]);
        EXPECT_NEAR((b.upper[i] - b.mid[i]) / 2.0,
                    (b.mid[i] - b.lower[i]) / 2.0, 1e-9);
    }
}

TEST(IndicatorsTest, ParseMinuteTime) {
    auto t = st::utils::parseMinuteTime("202408021035");
    EXPECT_EQ(st::utils::toDateString(t), "2024-08-02");
    EXPECT_EQ(st::utils::toDateTimeString(t), "2024-08-02 10:35:00");
    // 非法输入
    EXPECT_EQ(st::utils::parseMinuteTime("20240802"), st::DateTime{});
    EXPECT_EQ(st::utils::parseMinuteTime("abcdefghijkl"), st::DateTime{});
}
