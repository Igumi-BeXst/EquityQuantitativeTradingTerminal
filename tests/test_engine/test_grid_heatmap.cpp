#include "engine/optimizer/grid_heatmap.h"
#include <gtest/gtest.h>
#include <cmath>

namespace st {
namespace {

using st::GridSearchResult;

GridSearchResult makeResult(int x, int y, double value) {
    GridSearchResult r;
    r.params = {{"fastPeriod", x}, {"slowPeriod", y}};
    r.objectiveValue = value;
    r.success = true;
    return r;
}

TEST(GridHeatmapTest, BasicMatrix3x3) {
    std::vector<GridSearchResult> results{
        makeResult(2, 10, 1.0), makeResult(2, 20, 2.0), makeResult(2, 30, 3.0),
        makeResult(4, 10, 4.0), makeResult(4, 20, 5.0), makeResult(4, 30, 6.0),
        makeResult(6, 10, 7.0), makeResult(6, 20, 8.0), makeResult(6, 30, 9.0),
    };
    auto m = buildHeatmap(results, "fastPeriod", "slowPeriod");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->xParam, "fastPeriod");
    EXPECT_EQ(m->yParam, "slowPeriod");
    ASSERT_EQ(m->xValues.size(), 3u);
    ASSERT_EQ(m->yValues.size(), 3u);
    EXPECT_DOUBLE_EQ(m->xValues[0], 2.0);
    EXPECT_DOUBLE_EQ(m->xValues[2], 6.0);
    EXPECT_DOUBLE_EQ(m->yValues[0], 10.0);
    EXPECT_DOUBLE_EQ(m->yValues[2], 30.0);
    // values[y][x]：y 行对应 slowPeriod，x 列对应 fastPeriod
    EXPECT_DOUBLE_EQ(m->values[0][0], 1.0);   // (fast=2, slow=10)
    EXPECT_DOUBLE_EQ(m->values[1][2], 8.0);   // (fast=6, slow=20)
    EXPECT_DOUBLE_EQ(m->values[2][2], 9.0);   // (fast=6, slow=30)
}

TEST(GridHeatmapTest, AxisSortedAndDeduped) {
    // 乱序输入 + 重复坐标轴值 → 升序去重
    std::vector<GridSearchResult> results{
        makeResult(6, 10, 7.0), makeResult(2, 10, 1.0),
        makeResult(4, 30, 6.0), makeResult(4, 10, 4.0),
    };
    auto m = buildHeatmap(results, "fastPeriod", "slowPeriod");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->xValues.size(), 3u);
    ASSERT_EQ(m->yValues.size(), 2u);
    EXPECT_DOUBLE_EQ(m->xValues[0], 2.0);
    EXPECT_DOUBLE_EQ(m->xValues[1], 4.0);
    EXPECT_DOUBLE_EQ(m->xValues[2], 6.0);
    EXPECT_DOUBLE_EQ(m->yValues[0], 10.0);
    EXPECT_DOUBLE_EQ(m->yValues[1], 30.0);
    EXPECT_DOUBLE_EQ(m->values[1][1], 6.0);  // (fast=4, slow=30)
}

TEST(GridHeatmapTest, MissingCellsAreNaN) {
    // 只有 (2,10) 与 (6,30) 两个组合 → 其余格 NaN
    std::vector<GridSearchResult> results{
        makeResult(2, 10, 1.0), makeResult(6, 30, 9.0),
    };
    auto m = buildHeatmap(results, "fastPeriod", "slowPeriod");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->xValues.size(), 2u);
    ASSERT_EQ(m->yValues.size(), 2u);
    EXPECT_DOUBLE_EQ(m->values[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m->values[1][1], 9.0);
    EXPECT_TRUE(std::isnan(m->values[0][1]));
    EXPECT_TRUE(std::isnan(m->values[1][0]));
}

TEST(GridHeatmapTest, SingleColumnWorks) {
    // 单参数值 → 单列矩阵（1 维退化，仍可渲染为竖条）
    std::vector<GridSearchResult> results{
        makeResult(2, 10, 1.0), makeResult(2, 20, 2.0),
    };
    auto m = buildHeatmap(results, "fastPeriod", "slowPeriod");
    ASSERT_TRUE(m.has_value());
    ASSERT_EQ(m->xValues.size(), 1u);
    ASSERT_EQ(m->yValues.size(), 2u);
    EXPECT_DOUBLE_EQ(m->values[0][0], 1.0);
    EXPECT_DOUBLE_EQ(m->values[1][0], 2.0);
}

TEST(GridHeatmapTest, DuplicateCellLastWins) {
    std::vector<GridSearchResult> results{
        makeResult(2, 10, 1.0), makeResult(2, 10, 99.0),
    };
    auto m = buildHeatmap(results, "fastPeriod", "slowPeriod");
    ASSERT_TRUE(m.has_value());
    EXPECT_DOUBLE_EQ(m->values[0][0], 99.0);
    EXPECT_EQ(m->xValues.size(), 1u);   // 坐标轴仍去重
    EXPECT_EQ(m->yValues.size(), 1u);
}

TEST(GridHeatmapTest, SameParamRejected) {
    EXPECT_FALSE(buildHeatmap({makeResult(2, 10, 1.0)}, "fastPeriod", "fastPeriod").has_value());
}

TEST(GridHeatmapTest, UnknownParamReturnsNullopt) {
    // 结果里没有 entryPeriod
    EXPECT_FALSE(buildHeatmap({makeResult(2, 10, 1.0)}, "fastPeriod", "entryPeriod").has_value());
    EXPECT_FALSE(buildHeatmap({makeResult(2, 10, 1.0)}, "nope", "slowPeriod").has_value());
}

TEST(GridHeatmapTest, EmptyResultsReturnsNullopt) {
    EXPECT_FALSE(buildHeatmap({}, "fastPeriod", "slowPeriod").has_value());
    EXPECT_FALSE(buildHeatmap({makeResult(2, 10, 1.0)}, "", "slowPeriod").has_value());
    EXPECT_FALSE(buildHeatmap({makeResult(2, 10, 1.0)}, "fastPeriod", "").has_value());
}

}  // namespace
}  // namespace st
