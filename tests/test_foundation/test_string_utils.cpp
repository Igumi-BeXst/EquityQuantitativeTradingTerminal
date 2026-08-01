#include "foundation/utils/string_utils.h"
#include <gtest/gtest.h>
#include <string>

using namespace st;

TEST(StringUtilsTest, Trim) {
    EXPECT_EQ(utils::trim("  hello  "), std::string("hello"));
}

TEST(StringUtilsTest, Split) {
    auto parts = utils::split("a,b,c", ',');
    EXPECT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], std::string("a"));
    EXPECT_EQ(parts[1], std::string("b"));
    EXPECT_EQ(parts[2], std::string("c"));
}

TEST(StringUtilsTest, ToLower) {
    EXPECT_EQ(utils::toLower("HELLO"), std::string("hello"));
}

TEST(StringUtilsTest, StartsWith) {
    EXPECT_TRUE(utils::startsWith("hello world", "hello"));
}

TEST(StringUtilsTest, EndsWith) {
    EXPECT_TRUE(utils::endsWith("hello world", "world"));
}
