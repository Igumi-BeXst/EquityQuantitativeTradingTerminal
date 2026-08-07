#include <gtest/gtest.h>
#include "engine/analyzer/custom_index.h"
#include "foundation/utils/datetime.h"

#include <chrono>
#include <map>
#include <string>
#include <vector>

using namespace st;

namespace {

const StockCode kA(Market::SH, "600519");
const StockCode kB(Market::SH, "000001");

Bar mkBar(const StockCode& code, const std::string& date, double price) {
    Bar b;
    b.code = code;
    b.time = utils::parseDate(date);
    b.period = BarPeriod::Daily;
    b.open = b.high = b.low = b.close = price;
    b.volume = 1000;
    b.amount = price * 1000;
    return b;
}

IntradayPoint mkPoint(int minute, double price) {
    IntradayPoint p;
    p.time = utils::parseDateTime("2024-01-02 09:30:00") +
             std::chrono::minutes(minute);
    p.price = price;
    p.volume = 1000;
    p.amount = price * 1000;
    return p;
}

/// 假成分股日线拉取器：返回 map 中的日线（忽略 period，统一按日线）
class FakeBars {
public:
    std::map<StockCode, std::vector<Bar>> data;
    BarsFetcher fetcher() const {
        return [this](const StockCode& c, BarPeriod) {
            auto it = data.find(c);
            return it == data.end() ? std::vector<Bar>{} : it->second;
        };
    }
};

}  // namespace

// ============================================================
// normalizeWeights
// ============================================================

TEST(CustomIndexTest, NormalizeAllZeroBecomesEqual) {
    CustomIndex idx;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    normalizeWeights(idx.constituents);
    ASSERT_EQ(idx.constituents.size(), 2u);
    EXPECT_NEAR(idx.constituents[0].weight, 0.5, 1e-9);
    EXPECT_NEAR(idx.constituents[1].weight, 0.5, 1e-9);
}

TEST(CustomIndexTest, NormalizeManualWeightsToSumOne) {
    CustomIndex idx;
    idx.constituents = {{kA, "A", 3.0}, {kB, "B", 1.0}};
    normalizeWeights(idx.constituents);
    EXPECT_NEAR(idx.constituents[0].weight, 0.75, 1e-9);
    EXPECT_NEAR(idx.constituents[1].weight, 0.25, 1e-9);
}

TEST(CustomIndexTest, NormalizeNegativeClampedToZero) {
    CustomIndex idx;
    idx.constituents = {{kA, "A", 1.0}, {kB, "B", -1.0}};
    normalizeWeights(idx.constituents);
    EXPECT_NEAR(idx.constituents[0].weight, 1.0, 1e-9);
    EXPECT_NEAR(idx.constituents[1].weight, 0.0, 1e-9);
}

// ============================================================
// computeIndexBars（价格加权 + 基点重定基）
// ============================================================

TEST(CustomIndexTest, EqualWeightDailyIndexRebasedToBase) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110),
                   mkBar(kA, "2024-01-04", 121)};
    fb.data[kB] = {mkBar(kB, "2024-01-02", 50),  mkBar(kB, "2024-01-03", 60),
                   mkBar(kB, "2024-01-04", 55)};

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily);
    ASSERT_EQ(bars.size(), 3u);
    // 基准日（自动 = 首个共同日 01-02）= 基点 1000
    EXPECT_NEAR(bars[0].close, 1000.0, 1e-9);
    EXPECT_NEAR(bars[1].close, 1000.0 * (110.0 + 60.0) / (100.0 + 50.0), 1e-9);
    EXPECT_NEAR(bars[2].close, 1000.0 * (121.0 + 55.0) / (100.0 + 50.0), 1e-9);
    // OHLC 同样按比例缩放
    EXPECT_NEAR(bars[1].open, bars[1].close, 1e-9);   // 测试数据开=收
    EXPECT_NEAR(bars[2].open, bars[2].close, 1e-9);
    EXPECT_NEAR(bars[2].high, bars[2].close, 1e-9);
    EXPECT_NEAR(bars[2].low, bars[2].close, 1e-9);
}

TEST(CustomIndexTest, ManualWeightsAffectIndexLevel) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 3.0}, {kB, "B", 1.0}};  // 0.75 / 0.25
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110)};
    fb.data[kB] = {mkBar(kB, "2024-01-02", 50),  mkBar(kB, "2024-01-03", 60)};

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily);
    ASSERT_EQ(bars.size(), 2u);
    const double divisor = 0.75 * 100.0 + 0.25 * 50.0;   // 87.5
    EXPECT_NEAR(bars[0].close, 1000.0, 1e-9);
    EXPECT_NEAR(bars[1].close, 1000.0 * (0.75 * 110.0 + 0.25 * 60.0) / divisor, 1e-9);
}

TEST(CustomIndexTest, SuspendedDayCarryForwardLastClose) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110),
                   mkBar(kA, "2024-01-04", 121)};
    fb.data[kB] = {mkBar(kB, "2024-01-02", 50),  mkBar(kB, "2024-01-04", 55)};  // 01-03 停牌

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily);
    ASSERT_EQ(bars.size(), 3u);
    // 01-03：B 用上一收盘 50
    EXPECT_NEAR(bars[1].close, 1000.0 * (110.0 + 50.0) / (100.0 + 50.0), 1e-9);
    EXPECT_NEAR(bars[2].close, 1000.0 * (121.0 + 55.0) / (100.0 + 50.0), 1e-9);
}

TEST(CustomIndexTest, ExplicitBaseDateAnchorsDivisorIncludesHistory) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.baseDate = utils::parseDate("2024-01-03");   // 基准日 = 创建当天（示例）
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110),
                   mkBar(kA, "2024-01-04", 121)};
    fb.data[kB] = {mkBar(kB, "2024-01-02", 50),  mkBar(kB, "2024-01-03", 60),
                   mkBar(kB, "2024-01-04", 55)};

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily);
    // 基准日前历史保留（回溯到基点 1000）：除数锚定 01-03
    ASSERT_EQ(bars.size(), 3u);
    EXPECT_NEAR(bars[0].close, 1000.0 * (100.0 + 50.0) / (110.0 + 60.0), 1e-9);
    EXPECT_NEAR(bars[1].close, 1000.0, 1e-9);   // 01-03 = 基点
    EXPECT_NEAR(bars[2].close, 1000.0 * (121.0 + 55.0) / (110.0 + 60.0), 1e-9);
}

TEST(CustomIndexTest, BaseDateOnNonTradingDayFallsBackToLastBar) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.baseDate = utils::parseDate("2024-01-06");   // 周六（无 bar）
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110),
                   mkBar(kA, "2024-01-04", 121), mkBar(kA, "2024-01-05", 132)};
    fb.data[kB] = {mkBar(kB, "2024-01-02", 50),  mkBar(kB, "2024-01-03", 60),
                   mkBar(kB, "2024-01-04", 55),  mkBar(kB, "2024-01-05", 66)};

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily);
    ASSERT_EQ(bars.size(), 4u);
    // 除数回退到 ≤01-06 的最后一根（01-05）→ 指数在 01-05 = 基点 1000
    EXPECT_NEAR(bars.back().close, 1000.0, 1e-9);
    EXPECT_NEAR(bars.front().close, 1000.0 * (100.0 + 50.0) / (132.0 + 66.0), 1e-9);
}

TEST(CustomIndexTest, LaterListingBaseMovesToCommonDate) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110),
                   mkBar(kA, "2024-01-04", 121)};
    fb.data[kB] = {mkBar(kB, "2024-01-04", 55)};   // B 01-04 才上市

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily);
    ASSERT_EQ(bars.size(), 1u);   // 基准日抬到 01-04
    EXPECT_NEAR(bars[0].close, 1000.0, 1e-9);
}

TEST(CustomIndexTest, WeeklyAggregationFromDailyIndex) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    // 2024-01-01 是周一；01-02(周二)..01-05(周五) 为第一周，01-08(周一) 为第二周
    fb.data[kA] = {mkBar(kA, "2024-01-02", 100), mkBar(kA, "2024-01-03", 110),
                   mkBar(kA, "2024-01-04", 121), mkBar(kA, "2024-01-05", 132),
                   mkBar(kA, "2024-01-08", 143)};
    fb.data[kB] = {mkBar(kB, "2024-01-02", 50),  mkBar(kB, "2024-01-03", 60),
                   mkBar(kB, "2024-01-04", 55),  mkBar(kB, "2024-01-05", 66),
                   mkBar(kB, "2024-01-08", 77)};

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Weekly);
    ASSERT_EQ(bars.size(), 2u);
    // 第一周收盘 = 周五 01-05
    EXPECT_NEAR(bars[0].close, 1000.0 * (132.0 + 66.0) / (100.0 + 50.0), 1e-9);
    EXPECT_EQ(utils::toDateString(bars[0].time), "2024-01-05");
    // 第二周收盘 = 周一 01-08
    EXPECT_NEAR(bars[1].close, 1000.0 * (143.0 + 77.0) / (100.0 + 50.0), 1e-9);
}

TEST(CustomIndexTest, MonthlyAggregationFromDailyIndex) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    FakeBars fb;
    fb.data[kA] = {mkBar(kA, "2024-01-31", 100), mkBar(kA, "2024-02-01", 110),
                   mkBar(kA, "2024-02-28", 121)};
    fb.data[kB] = {mkBar(kB, "2024-01-31", 50),  mkBar(kB, "2024-02-01", 60),
                   mkBar(kB, "2024-02-28", 55)};

    auto bars = computeIndexBars(idx, fb.fetcher(), BarPeriod::Monthly);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_NEAR(bars[0].close, 1000.0 * (100.0 + 50.0) / (100.0 + 50.0), 1e-9);  // 1月
    EXPECT_NEAR(bars[1].close, 1000.0 * (121.0 + 55.0) / (100.0 + 50.0), 1e-9);  // 2月
}

TEST(CustomIndexTest, EmptyConstituentsReturnsEmpty) {
    CustomIndex idx;
    FakeBars fb;
    EXPECT_TRUE(computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily).empty());
}

TEST(CustomIndexTest, MissingConstituentDataReturnsEmpty) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}};
    FakeBars fb;  // 无任何数据
    EXPECT_TRUE(computeIndexBars(idx, fb.fetcher(), BarPeriod::Daily).empty());
}

// ============================================================
// computeIndexIntraday（涨跌幅外推）
// ============================================================

TEST(CustomIndexTest, IntradayRebasedFromPrevClose) {
    CustomIndex idx;
    idx.baseValue = 1000.0;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    auto fetch = [](const StockCode& c) {
        IntradayData d;
        d.preClose = (c == kA) ? 10.0 : 5.0;
        if (c == kA) {
            d.points = {mkPoint(0, 10.0), mkPoint(30, 10.5)};
        } else {
            d.points = {mkPoint(0, 5.0),  mkPoint(30, 5.25)};
        }
        return d;
    };

    auto data = computeIndexIntraday(idx, 1000.0, fetch);
    ASSERT_EQ(data.points.size(), 2u);
    EXPECT_NEAR(data.points[0].price, 1000.0, 1e-9);   // 开盘平昨收
    // 30 分：A +5%，B +5% → 等权 +5%
    EXPECT_NEAR(data.points[1].price, 1000.0 * 1.05, 1e-9);
    EXPECT_NEAR(data.preClose, 1000.0, 1e-9);
}

TEST(CustomIndexTest, IntradayMissingMinuteCarryForward) {
    CustomIndex idx;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    auto fetch = [](const StockCode& c) {
        IntradayData d;
        d.preClose = (c == kA) ? 10.0 : 5.0;
        if (c == kA) {
            d.points = {mkPoint(0, 10.0), mkPoint(60, 10.5)};   // 30 分缺席
        } else {
            d.points = {mkPoint(0, 5.0),  mkPoint(30, 5.25)};
        }
        return d;
    };

    auto data = computeIndexIntraday(idx, 1000.0, fetch);
    ASSERT_EQ(data.points.size(), 3u);  // 0/30/60
    // 30 分：A carry 10.0（0%），B +5% → +2.5%
    EXPECT_NEAR(data.points[1].price, 1000.0 * 1.025, 1e-9);
    // 60 分：A +5%，B carry 5.25（+5%）→ +5%
    EXPECT_NEAR(data.points[2].price, 1000.0 * 1.05, 1e-9);
}

TEST(CustomIndexTest, IntradayEmptyInputs) {
    CustomIndex idx;
    idx.constituents = {{kA, "A", 0.0}};
    auto fetch = [](const StockCode&) { return IntradayData{}; };
    EXPECT_TRUE(computeIndexIntraday(idx, 1000.0, fetch).points.empty());
    EXPECT_TRUE(computeIndexIntraday(idx, 0.0, fetch).points.empty());
}

// ============================================================
// computeIndexLive / lastCompletedClose
// ============================================================

TEST(CustomIndexTest, LiveValueFromWeightedChanges) {
    CustomIndex idx;
    idx.constituents = {{kA, "A", 0.0}, {kB, "B", 0.0}};
    Quote qa, qb;
    qa.code = kA; qa.change = 2.0;   // +2%
    qb.code = kB; qb.change = 4.0;   // +4%
    EXPECT_NEAR(computeIndexLive(1000.0, idx, {qa, qb}), 1000.0 * 1.03, 1e-9);

    // 缺一只报价 → 该只涨跌幅按 0
    EXPECT_NEAR(computeIndexLive(1000.0, idx, {qa}), 1000.0 * 1.01, 1e-9);
}

TEST(CustomIndexTest, LastCompletedCloseSkipsToday) {
    std::vector<Bar> bars = {mkBar(kA, "2024-01-02", 100),
                             mkBar(kA, "2024-01-03", 110)};
    // now = 01-03（进行中当日）→ 取 01-02
    EXPECT_NEAR(lastCompletedClose(bars, utils::parseDate("2024-01-03")), 100.0, 1e-9);
    // now = 01-04 → 01-03 已完成
    EXPECT_NEAR(lastCompletedClose(bars, utils::parseDate("2024-01-04")), 110.0, 1e-9);
    // 空
    EXPECT_EQ(lastCompletedClose({}, utils::now()), 0.0);
}
