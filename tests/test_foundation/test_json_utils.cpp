#include "foundation/utils/json_utils.h"
#include <gtest/gtest.h>
using namespace st;
TEST(JsonUtilsTest, SetAndGetNested) {
    utils::Json j;
    utils::setNested(j, "a.b.c", 42);
    auto* v = utils::getNested(j, "a.b.c");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->get<int>(), 42);
}
TEST(JsonUtilsTest, GetMissingPath) {
    utils::Json j;
    EXPECT_EQ(utils::getNested(j, "x.y.z"), nullptr);
}
