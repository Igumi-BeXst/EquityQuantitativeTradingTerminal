#include <gtest/gtest.h>
#include "engine/analyzer/chip_distribution.h"
#include "foundation/utils/datetime.h"

#include <algorithm>
#include <vector>

using namespace st;

namespace {

const StockCode kCode(Market::SH, "600519");

/// 单价格 bar：open=high=low=close=price，量价成比例
Bar makeBar(double price, Volume vol, int day) {
    Bar b;
    b.code = kCode;
    b.period = BarPeriod::Daily;
    b.time = utils::addTradingDays(utils::parseDate("2024-01-02"), day);
    b.open = price;
    b.high = price * 1.01;
    b.low = price * 0.99;
    b.close = price;
    b.volume = vol;
    b.amount = price * vol;
    return b;
}

/// 平盘 bar（OHLC 全等，单桶集中测试用）
Bar makeFlatBar(double price, Volume vol, int day) {
    Bar b;
    b.code = kCode;
    b.period = BarPeriod::Daily;
    b.time = utils::addTradingDays(utils::parseDate("2024-01-02"), day);
    b.open = price;
    b.high = price;
    b.low = price;
    b.close = price;
    b.volume = vol;
    b.amount = price * vol;
    return b;
}

/// 显式 OHLC bar（区间统计用）
Bar makeOHLC(double open, double close, Volume vol, int day) {
    Bar b;
    b.code = kCode;
    b.period = BarPeriod::Daily;
    b.time = utils::addTradingDays(utils::parseDate("2024-01-02"), day);
    b.open = open;
    b.close = close;
    b.high = std::max(open, close) * 1.01;
    b.low = std::min(open, close) * 0.99;
    b.volume = vol;
    b.amount = (open + close) / 2.0 * vol;
    return b;
}

}  // namespace

// ============================================================
// ChipDistribution
// ============================================================
TEST(ChipDistributionTest, SinglePriceConcentratesAtThatPrice) {
    // 全在同一价位 10（平盘）→ 筹码集中单桶，平均成本 ≈ 10
    std::vector<Bar> bars;
    for (int i = 0; i < 100; ++i) bars.push_back(makeFlatBar(10.0, 1000, i));

    auto r = ChipDistribution::compute(bars, 1e9);
    ASSERT_TRUE(r.success);
    EXPECT_LE(r.points.size(), 2u);                 // 集中在 1 个桶
    EXPECT_NEAR(r.avgCost, 10.0, 0.05);
    EXPECT_NEAR(r.totalChips, 1e9, 1e9 * 0.01);     // 衰减模式归一化到流通股本
}

TEST(ChipDistributionTest, TwoRegimeAvgCostAndProfitHalf) {
    // 前 100 日 @10，后 100 日 @20，末日 @15 → 平均成本≈15，获利盘≈50%
    std::vector<Bar> bars;
    int day = 0;
    for (int i = 0; i < 100; ++i) bars.push_back(makeBar(10.0, 1000, day++));
    for (int i = 0; i < 100; ++i) bars.push_back(makeBar(20.0, 1000, day++));
    bars.push_back(makeBar(15.0, 1000, day++));  // 现价 = 15

    auto r = ChipDistribution::compute(bars, 1e9);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.avgCost, 15.0, 0.3);
    EXPECT_NEAR(r.profitRatio, 0.5, 0.05);  // 10 价位筹码约一半，≤现价
}

TEST(ChipDistributionTest, HighTurnoverDayConcentratesChips) {
    // 200 日 @10 小量 + 末日 100% 换手 @20 → 旧筹码衰减殆尽，筹码聚向新价 20
    const double floatShares = 1e6;
    std::vector<Bar> bars;
    int day = 0;
    for (int i = 0; i < 200; ++i) bars.push_back(makeBar(10.0, 1000, day++));
    bars.push_back(makeBar(20.0, static_cast<Volume>(floatShares), day++));  // 换手 100%

    auto r = ChipDistribution::compute(bars, floatShares);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.avgCost, 20.0, 0.1);
    EXPECT_NEAR(r.totalChips, floatShares, floatShares * 0.05);  // 衰减模式归一化为流通股本

    // 旧筹码衰减殆尽：绝大多数筹码聚集在现价 20 附近
    double near20 = 0.0;
    for (const auto& p : r.points) {
        if (std::abs(p.price - 20.0) < 0.5) near20 += p.shares;
    }
    EXPECT_GT(near20 / r.totalChips, 0.99);
}

TEST(ChipDistributionTest, VolumeOnlyModeWithoutFloatShares) {
    // floatShares=0 → 纯量模式，不衰减不归一化，不崩
    std::vector<Bar> bars;
    for (int i = 0; i < 50; ++i) bars.push_back(makeBar(10.0, 2000, i));

    auto r = ChipDistribution::compute(bars, 0.0);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.totalChips, 50 * 2000.0, 1.0);
    EXPECT_NEAR(r.avgCost, 10.0, 0.05);
}

TEST(ChipDistributionTest, EmptyOrAllInvalidFails) {
    EXPECT_FALSE(ChipDistribution::compute({}, 1e6).success);

    std::vector<Bar> invalid;
    Bar bad = makeBar(10.0, 1000, 0);
    bad.close = 0.0;  // 非法
    invalid.push_back(bad);
    EXPECT_FALSE(ChipDistribution::compute(invalid, 1e6).success);
}

TEST(ChipDistributionTest, SeventyPctRangeNarrowerThanNinety) {
    // 双价位：P15/P85（70%区间）应窄于 P5/P95（90%区间），且大致覆盖两个价位集群
    std::vector<Bar> bars;
    int day = 0;
    for (int i = 0; i < 100; ++i) bars.push_back(makeBar(10.0, 1000, day++));
    for (int i = 0; i < 100; ++i) bars.push_back(makeBar(20.0, 1000, day++));
    auto r = ChipDistribution::compute(bars, 1e9);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.costLow70, 10.0, 0.5);
    EXPECT_NEAR(r.costHigh70, 20.0, 0.5);
    EXPECT_GT(r.costHigh70 - r.costLow70, 0.0);
    EXPECT_LT(r.costHigh70 - r.costLow70, r.costHigh - r.costLow);
}

TEST(ChipDistributionTest, TightDistributionLessConcentratedThanWide) {
    // 紧密分布：全 @10（±1% 振幅）
    std::vector<Bar> tight;
    for (int i = 0; i < 100; ++i) tight.push_back(makeBar(10.0, 1000, i));
    auto rTight = ChipDistribution::compute(tight, 1e9);
    ASSERT_TRUE(rTight.success);

    // 宽松分布：10 → 20 均匀爬升
    std::vector<Bar> wide;
    for (int i = 0; i < 100; ++i) wide.push_back(makeBar(10.0 + i * 0.1, 1000, i));
    auto rWide = ChipDistribution::compute(wide, 1e9);
    ASSERT_TRUE(rWide.success);

    EXPECT_GT(rWide.concentration, rTight.concentration);
}

// ============================================================
// TransactionDistribution
// ============================================================
TEST(TransactionDistributionTest, FromTicksBuckets) {
    std::vector<Tick> ticks;
    Tick a; a.price = 10.0; a.volume = 100;
    Tick b; b.price = 10.5; b.volume = 50;
    Tick c; c.price = 19.0; c.volume = 30;
    ticks = {a, b, c};

    auto r = TransactionDistribution::fromTicks(ticks, 60);
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.totalVolume, 180);
    ASSERT_EQ(r.points.size(), 3u);
    EXPECT_EQ(r.points[0].volume, 100);
    EXPECT_EQ(r.points[1].volume, 50);
    EXPECT_EQ(r.points[2].volume, 30);
    EXPECT_NEAR(r.points[0].price, 10.075, 1e-6);   // 首个桶中心
    EXPECT_NEAR(r.points[2].price, 18.925, 1e-6);   // 末个桶中心
    EXPECT_TRUE(std::is_sorted(r.points.begin(), r.points.end(),
                               [](const PriceVolumePoint& x, const PriceVolumePoint& y) {
                                   return x.price < y.price;
                               }));
}

TEST(TransactionDistributionTest, FromIntradayUsesDeltas) {
    IntradayData data;
    data.code = kCode;
    std::vector<IntradayPoint> pts;
    IntradayPoint p1; p1.price = 10.0; p1.volume = 1000;
    IntradayPoint p2; p2.price = 10.5; p2.volume = 1500;
    IntradayPoint p3; p3.price = 10.2; p3.volume = 1700;
    pts = {p1, p2, p3};
    data.points = pts;

    auto r = TransactionDistribution::fromIntraday(data, 60);
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.totalVolume, 1700);  // 差分：1000 + 500 + 200
}

TEST(TransactionDistributionTest, EmptyReturnsEmpty) {
    EXPECT_FALSE(TransactionDistribution::fromTicks({}, 60).success);
    EXPECT_FALSE(TransactionDistribution::fromIntraday(IntradayData{}, 60).success);
}

// ============================================================
// RangeStats
// ============================================================
TEST(RangeStatsTest, ChangePctVolumeBarCount) {
    std::vector<Bar> bars = {
        makeOHLC(100, 105, 1000, 0),
        makeOHLC(105, 110, 1000, 1),
        makeOHLC(110, 115, 1000, 2),
        makeOHLC(115, 120, 1000, 3),
        makeOHLC(120, 120, 1000, 4),
    };

    auto r = RangeStats::compute(bars, 100000.0);
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.barCount, 5);
    EXPECT_NEAR(r.startPrice, 100.0, 1e-9);
    EXPECT_NEAR(r.endPrice, 120.0, 1e-9);
    EXPECT_NEAR(r.changePct, 20.0, 1e-9);        // (120-100)/100×100
    EXPECT_NEAR(r.totalVolume, 5000.0, 1e-9);
    EXPECT_NEAR(r.turnoverPct, 5.0, 1e-9);       // 5000/100000×100
}

TEST(RangeStatsTest, HighLowAmplitude) {
    std::vector<Bar> bars = {
        makeOHLC(100, 105, 1000, 0),   // high 106.05, low 99
        makeOHLC(105, 90, 1000, 1),    // low 89.1
        makeOHLC(90, 100, 1000, 2),
    };

    auto r = RangeStats::compute(bars);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.high, 106.05, 1e-9);
    EXPECT_NEAR(r.low, 89.1, 1e-9);
    EXPECT_NEAR(r.amplitudePct, 16.95, 1e-9);    // (106.05-89.1)/100×100
}

TEST(RangeStatsTest, AvgPriceWeighted) {
    std::vector<Bar> bars = {
        makeOHLC(10, 10, 1000, 0),    // amount = 10×1000
        makeOHLC(20, 20, 1000, 1),    // amount = 20×1000
    };

    auto r = RangeStats::compute(bars);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.avgPrice, 15.0, 1e-9);         // (10000+20000)/2000
}

TEST(RangeStatsTest, EmptyOrAllInvalidFails) {
    EXPECT_FALSE(RangeStats::compute({}).success);

    std::vector<Bar> invalid;
    Bar bad = makeOHLC(100, 100, 1000, 0);
    bad.low = 0.0;  // 非法
    invalid.push_back(bad);
    EXPECT_FALSE(RangeStats::compute(invalid).success);
}
