#include "engine/analyzer/range_statistics.h"
#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"
#include <gtest/gtest.h>
#include <string_view>

namespace st {
namespace {

Bar makeBar(const char* date, double open, double high, double low, double close,
            double volume, double amount, double turnover) {
    Bar b;
    b.code = StockCode(std::string_view("SH600000"));
    b.time = utils::parseDate(date);
    b.period = BarPeriod::Daily;
    b.open = open; b.high = high; b.low = low; b.close = close;
    b.volume = static_cast<Volume>(volume);
    b.amount = amount;
    b.turnoverRate = turnover;
    return b;
}

TEST(RangeStatisticsTest, NormalRange) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10.0, 10.5, 9.8, 10.2, 100000, 1000000, 0.010),
        makeBar("2026-08-04", 10.2, 11.0, 10.1, 10.9, 120000, 1200000, 0.012),
        makeBar("2026-08-05", 10.9, 11.5, 10.8, 11.2, 140000, 1500000, 0.014),
        makeBar("2026-08-06", 11.2, 11.3, 10.6, 10.8,  90000,  950000, 0.009),
        makeBar("2026-08-07", 10.8, 10.9, 10.4, 10.5, 110000, 1100000, 0.011),
    };
    auto rs = computeRangeStats(bars, 0, 4);
    ASSERT_TRUE(rs.has_value());
    EXPECT_EQ(utils::toDateString(rs->fromDate), "2026-08-03");
    EXPECT_EQ(utils::toDateString(rs->toDate), "2026-08-07");
    EXPECT_EQ(rs->barCount, 5);
    EXPECT_NEAR(rs->openClosePct, (10.5 - 10.0) / 10.0, 1e-9);   // 末close/首open-1
    EXPECT_NEAR(rs->high, 11.5, 1e-9);
    EXPECT_EQ(utils::toDateString(rs->highDate), "2026-08-05");
    EXPECT_NEAR(rs->low, 9.8, 1e-9);
    EXPECT_EQ(utils::toDateString(rs->lowDate), "2026-08-03");
    EXPECT_NEAR(rs->amplitude, (11.5 - 9.8) / 10.0, 1e-9);       // (高-低)/首open
    EXPECT_NEAR(rs->totalVolume, 560000.0, 1e-9);
    EXPECT_NEAR(rs->totalAmount, 5750000.0, 1e-9);
    EXPECT_NEAR(rs->turnoverSum, 0.056, 1e-9);
    EXPECT_NEAR(rs->avgPrice, 5750000.0 / 560000.0, 1e-9);
}

TEST(RangeStatisticsTest, SingleBar) {
    std::vector<Bar> bars{ makeBar("2026-08-03", 10.0, 11.0, 9.0, 10.5, 50000, 500000, 0.02) };
    auto rs = computeRangeStats(bars, 0, 0);
    ASSERT_TRUE(rs.has_value());
    EXPECT_EQ(rs->barCount, 1);
    EXPECT_NEAR(rs->openClosePct, (10.5 - 10.0) / 10.0, 1e-9);
    EXPECT_NEAR(rs->high, 11.0, 1e-9);
    EXPECT_NEAR(rs->low, 9.0, 1e-9);
    EXPECT_NEAR(rs->amplitude, (11.0 - 9.0) / 10.0, 1e-9);
    EXPECT_EQ(utils::toDateString(rs->highDate), "2026-08-03");
    EXPECT_EQ(utils::toDateString(rs->lowDate), "2026-08-03");
}

TEST(RangeStatisticsTest, ReversedRange) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10, 10.5, 9.8, 10.2, 1, 1, 0),
        makeBar("2026-08-04", 10, 11, 10, 10.9, 1, 1, 0),
    };
    EXPECT_FALSE(computeRangeStats(bars, 1, 0).has_value());
}

TEST(RangeStatisticsTest, OutOfBounds) {
    std::vector<Bar> bars{ makeBar("2026-08-03", 10, 10.5, 9.8, 10.2, 1, 1, 0) };
    EXPECT_FALSE(computeRangeStats(bars, 0, 5).has_value());
    EXPECT_FALSE(computeRangeStats(bars, -1, 0).has_value());
}

TEST(RangeStatisticsTest, EmptyBars) {
    EXPECT_FALSE(computeRangeStats({}, 0, 0).has_value());
}

TEST(RangeStatisticsTest, SkipsInvalidBars) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10.0, 10.5, 9.8, 10.2, 100, 1000, 0.01),
        Bar{},  // 全 0 → !isValid()，不参与极值/基准/量额/换手，但计入 barCount
        makeBar("2026-08-05", 10.9, 11.5, 10.8, 11.2, 140, 1500, 0.02),
    };
    auto rs = computeRangeStats(bars, 0, 2);
    ASSERT_TRUE(rs.has_value());
    EXPECT_EQ(rs->barCount, 3);
    EXPECT_NEAR(rs->high, 11.5, 1e-9);
    EXPECT_NEAR(rs->low, 9.8, 1e-9);
    EXPECT_NEAR(rs->openClosePct, (11.2 - 10.0) / 10.0, 1e-9);   // 基准=首个有效 open
    EXPECT_NEAR(rs->totalVolume, 240.0, 1e-9);
    EXPECT_NEAR(rs->totalAmount, 2500.0, 1e-9);
    EXPECT_NEAR(rs->turnoverSum, 0.03, 1e-9);
}

TEST(RangeStatisticsTest, AllInvalidReturnsNullopt) {
    std::vector<Bar> bars{ Bar{}, Bar{} };
    EXPECT_FALSE(computeRangeStats(bars, 0, 1).has_value());
}

TEST(RangeStatisticsTest, AveragePriceAndZeroVolume) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10, 10.5, 9.8, 10.2, 100000, 1000000, 0.01),
        makeBar("2026-08-04", 10.2, 11, 10.1, 10.9, 0, 0, 0.02),  // 有效但量额为 0
    };
    auto rs = computeRangeStats(bars, 0, 1);
    ASSERT_TRUE(rs.has_value());
    EXPECT_NEAR(rs->avgPrice, 1000000.0 / 100000.0, 1e-9);
    auto rs2 = computeRangeStats(
        std::vector<Bar>{ makeBar("2026-08-03", 10, 10, 10, 10, 0, 0, 0) }, 0, 0);
    ASSERT_TRUE(rs2.has_value());
    EXPECT_NEAR(rs2->avgPrice, 0.0, 1e-9);
}

}  // namespace
}  // namespace st
