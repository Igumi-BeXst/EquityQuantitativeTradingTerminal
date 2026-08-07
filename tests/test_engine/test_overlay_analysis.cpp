#include <gtest/gtest.h>
#include "engine/analyzer/overlay_analysis.h"
#include "foundation/utils/datetime.h"

#include <chrono>
#include <vector>

using namespace st;

namespace {

const StockCode kCode(Market::SH, "600519");
const StockCode kCode2(Market::SH, "000001");

/// 单价格 bar：close = price，日期从 2024-01-02 起加 day 个交易日
Bar makeBar(double price, int day) {
    Bar b;
    b.code = kCode;
    b.period = BarPeriod::Daily;
    b.time = utils::addTradingDays(utils::parseDate("2024-01-02"), day);
    b.open = price;
    b.high = price;
    b.low = price;
    b.close = price;
    b.volume = 1000;
    b.amount = price * 1000;
    return b;
}

/// 分时点：距 09:30 minute 分钟（0..239），价格为 price
IntradayPoint makePoint(int minute, double price) {
    IntradayPoint p;
    p.time = utils::parseDateTime("2024-01-02 09:30:00") +
             std::chrono::minutes(minute);
    p.price = price;
    p.volume = 1000;
    p.amount = price * 1000;
    return p;
}

}  // namespace

// ============================================================
// alignOverlay（K线按日期对齐）
// ============================================================

TEST(OverlayAlignTest, FullDateMatchComputesRatio) {
    std::vector<Bar> base = {makeBar(100, 0), makeBar(110, 1), makeBar(121, 2)};
    std::vector<Bar> overlay = {makeBar(50, 0), makeBar(60, 1), makeBar(55, 2)};

    auto rows = alignOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 3u);
    for (const auto& r : rows) EXPECT_TRUE(r.matched);
    EXPECT_NEAR(rows[0].overlayClose, 50.0, 1e-9);
    // makeBar 的 OHLC 全等于 price → 完整 OHLC 均被填充
    EXPECT_NEAR(rows[0].overlayOpen, 50.0, 1e-9);
    EXPECT_NEAR(rows[0].overlayHigh, 50.0, 1e-9);
    EXPECT_NEAR(rows[0].overlayLow, 50.0, 1e-9);
    EXPECT_NEAR(rows[1].overlayHigh, 60.0, 1e-9);
    EXPECT_NEAR(rows[0].relativeStrength, 100.0 / 50.0, 1e-9);
    EXPECT_NEAR(rows[1].relativeStrength, 110.0 / 60.0, 1e-9);
    EXPECT_NEAR(rows[2].relativeStrength, 121.0 / 55.0, 1e-9);
}

TEST(OverlayAlignTest, OverlayLaterListingFrontUnmatched) {
    std::vector<Bar> base = {makeBar(100, 0), makeBar(110, 1), makeBar(121, 2)};
    std::vector<Bar> overlay = {makeBar(50, 1), makeBar(55, 2)};  // day0 无数据

    auto rows = alignOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_FALSE(rows[0].matched);
    EXPECT_TRUE(rows[1].matched);
    EXPECT_TRUE(rows[2].matched);
    EXPECT_NEAR(rows[1].overlayClose, 50.0, 1e-9);
}

TEST(OverlayAlignTest, OverlayDelistedTailUnmatched) {
    std::vector<Bar> base = {makeBar(100, 0), makeBar(110, 1), makeBar(121, 2)};
    std::vector<Bar> overlay = {makeBar(50, 0), makeBar(55, 1)};  // day2 无数据

    auto rows = alignOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[0].matched);
    EXPECT_TRUE(rows[1].matched);
    EXPECT_FALSE(rows[2].matched);
}

TEST(OverlayAlignTest, ExtraOverlayDatesIgnored) {
    std::vector<Bar> base = {makeBar(100, 0), makeBar(110, 1)};
    std::vector<Bar> overlay = {makeBar(50, 0), makeBar(55, 1), makeBar(60, 2)};

    auto rows = alignOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_TRUE(rows[0].matched);
    EXPECT_TRUE(rows[1].matched);
}

TEST(OverlayAlignTest, MissingMiddleDateUnmatched) {
    // overlay 缺中间某日（与 base 时间轴错位）
    std::vector<Bar> base = {makeBar(100, 0), makeBar(110, 1), makeBar(121, 2)};
    std::vector<Bar> overlay = {makeBar(50, 0), makeBar(60, 2)};  // day1 缺席

    auto rows = alignOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[0].matched);
    EXPECT_FALSE(rows[1].matched);
    EXPECT_TRUE(rows[2].matched);
}

TEST(OverlayAlignTest, DuplicateOverlayDateLastWins) {
    std::vector<Bar> base = {makeBar(100, 0)};
    std::vector<Bar> overlay = {makeBar(50, 0), makeBar(66, 0)};  // 同日两条 → 后写覆盖

    auto rows = alignOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(rows[0].matched);
    EXPECT_NEAR(rows[0].overlayClose, 66.0, 1e-9);
    EXPECT_NEAR(rows[0].relativeStrength, 100.0 / 66.0, 1e-9);
}

TEST(OverlayAlignTest, EmptyInputs) {
    EXPECT_TRUE(alignOverlay({}, {makeBar(50, 0)}).empty());
    auto rows = alignOverlay({makeBar(100, 0), makeBar(110, 1)}, {});
    ASSERT_EQ(rows.size(), 2u);
    for (const auto& r : rows) EXPECT_FALSE(r.matched);
}

// ============================================================
// alignIntradayOverlay（分时按分钟对齐）
// ============================================================

TEST(IntradayOverlayAlignTest, FullMinuteMatchComputesRatio) {
    IntradayData base, overlay;
    base.code = kCode;
    overlay.code = kCode2;
    base.points = {makePoint(0, 10.0), makePoint(30, 10.5), makePoint(120, 10.2)};
    overlay.points = {makePoint(0, 5.0), makePoint(30, 5.25), makePoint(120, 6.0)};

    auto rows = alignIntradayOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 3u);
    for (const auto& r : rows) EXPECT_TRUE(r.matched);
    EXPECT_NEAR(rows[0].overlayPrice, 5.0, 1e-9);
    EXPECT_NEAR(rows[0].relativeStrength, 10.0 / 5.0, 1e-9);
    EXPECT_NEAR(rows[1].relativeStrength, 10.5 / 5.25, 1e-9);
    EXPECT_NEAR(rows[2].relativeStrength, 10.2 / 6.0, 1e-9);
}

TEST(IntradayOverlayAlignTest, MissingMinutesUnmatched) {
    IntradayData base, overlay;
    base.points = {makePoint(0, 10.0), makePoint(30, 10.5), makePoint(60, 10.2)};
    overlay.points = {makePoint(0, 5.0), makePoint(60, 5.1)};  // 30 分缺席

    auto rows = alignIntradayOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[0].matched);
    EXPECT_FALSE(rows[1].matched);
    EXPECT_TRUE(rows[2].matched);
}

TEST(IntradayOverlayAlignTest, ExtraOverlayMinutesIgnored) {
    IntradayData base, overlay;
    base.points = {makePoint(0, 10.0), makePoint(30, 10.5)};
    overlay.points = {makePoint(0, 5.0), makePoint(30, 5.25), makePoint(90, 5.5)};

    auto rows = alignIntradayOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_TRUE(rows[0].matched);
    EXPECT_TRUE(rows[1].matched);
}

TEST(IntradayOverlayAlignTest, EmptyBaseReturnsEmpty) {
    EXPECT_TRUE(alignIntradayOverlay(IntradayData{}, IntradayData{}).empty());
}

TEST(IntradayOverlayAlignTest, EmptyOverlayAllUnmatched) {
    IntradayData base, overlay;
    base.points = {makePoint(0, 10.0), makePoint(30, 10.5)};

    auto rows = alignIntradayOverlay(base, overlay);
    ASSERT_EQ(rows.size(), 2u);
    for (const auto& r : rows) EXPECT_FALSE(r.matched);
}

TEST(OverlayTargetTest, IsTdxSectorCode) {
    EXPECT_TRUE(isTdxSectorCode("880301"));
    EXPECT_TRUE(isTdxSectorCode("885001"));
    EXPECT_FALSE(isTdxSectorCode("BK0475"));
    EXPECT_FALSE(isTdxSectorCode("new_blhy"));
    EXPECT_FALSE(isTdxSectorCode("600519"));
    EXPECT_FALSE(isTdxSectorCode(""));
}
