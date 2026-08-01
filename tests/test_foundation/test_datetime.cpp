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
