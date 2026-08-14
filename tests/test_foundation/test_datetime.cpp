#include "foundation/utils/datetime.h"
#include <gtest/gtest.h>
using namespace st;
TEST(DateTimeTest, ParseDate) {
    auto dt = utils::parseDate("2024-01-15");
    EXPECT_EQ(utils::toDateString(dt), "2024-01-15");
}
TEST(DateTimeTest, ParseCompactFormat) {
    auto dt = utils::parseDate("20240115");
    EXPECT_EQ(utils::toDateString(dt), "2024-01-15");
}
TEST(DateTimeTest, WeekendDetection) {
    auto sat = utils::parseDate("2024-01-13"); // Saturday
    auto sun = utils::parseDate("2024-01-14"); // Sunday
    auto mon = utils::parseDate("2024-01-15"); // Monday
    EXPECT_TRUE(utils::isWeekend(sat));
    EXPECT_TRUE(utils::isWeekend(sun));
    EXPECT_FALSE(utils::isWeekend(mon));
}

TEST(DateTimeTest, AddTradingDaysForward) {
    // 2024-01-12 周五 +1 → 周一 01-15（跳过周末）
    auto fri = utils::parseDate("2024-01-12");
    EXPECT_EQ(utils::toDateString(utils::addTradingDays(fri, 1)), "2024-01-15");
    // 周一 +5 → 下周一
    auto mon = utils::parseDate("2024-01-15");
    EXPECT_EQ(utils::toDateString(utils::addTradingDays(mon, 5)), "2024-01-22");
    EXPECT_EQ(utils::addTradingDays(mon, 0), mon);
}

TEST(DateTimeTest, AddTradingDaysBackward) {
    // 2024-01-15 周一 -1 → 上周五 01-12（跳过周末）
    auto mon = utils::parseDate("2024-01-15");
    EXPECT_EQ(utils::toDateString(utils::addTradingDays(mon, -1)), "2024-01-12");
    // 周五 -3 → 周二 01-09
    auto fri = utils::parseDate("2024-01-12");
    EXPECT_EQ(utils::toDateString(utils::addTradingDays(fri, -3)), "2024-01-09");
    // 周日 -1 → 周五（从周日的前一天开始数，跳过周六）
    auto sun = utils::parseDate("2024-01-14");
    EXPECT_EQ(utils::toDateString(utils::addTradingDays(sun, -1)), "2024-01-12");
    // 回看 250 个交易日 ≈ 一年前，且早于截止日
    auto end = utils::parseDate("2026-08-15");
    auto start = utils::addTradingDays(end, -250);
    EXPECT_LT(start, end);
    EXPECT_EQ(utils::tradingDaysBetween(start, end), 250);  // (start, end] 共 250 个非周末日
}
