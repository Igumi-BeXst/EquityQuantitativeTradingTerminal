#include <gtest/gtest.h>
#include "engine/analyzer/treemap.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace st;

namespace {

bool withinBounds(const TreemapRect& r, double w, double h) {
    return r.x >= -1e-9 && r.y >= -1e-9 &&
           r.x + r.w <= w + 1e-9 && r.y + r.h <= h + 1e-9 &&
           r.w >= 0.0 && r.h >= 0.0;
}

/// 无重叠且累计面积写入 areaSum；返回是否有重叠
bool noOverlap(const std::vector<TreemapRect>& rects, double& areaSum) {
    areaSum = 0.0;
    for (size_t i = 0; i < rects.size(); ++i) {
        areaSum += rects[i].w * rects[i].h;
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const auto& a = rects[i];
            const auto& b = rects[j];
            const double ox = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
            const double oy = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
            if (ox > 1e-6 && oy > 1e-6) return false;
        }
    }
    return true;
}

}  // namespace

TEST(TreemapTest, EmptyWeightsEmpty) {
    EXPECT_TRUE(Treemap::layout(100, 100, {}).empty());
}

TEST(TreemapTest, AllZeroWeightsEmpty) {
    EXPECT_TRUE(Treemap::layout(100, 100, {0.0, 0.0, 0.0}).empty());
}

TEST(TreemapTest, SingleWeightFillsRect) {
    auto r = Treemap::layout(100, 100, {5.0});
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NEAR(r[0].x, 0.0, 1e-9);
    EXPECT_NEAR(r[0].y, 0.0, 1e-9);
    EXPECT_NEAR(r[0].w, 100.0, 1e-6);
    EXPECT_NEAR(r[0].h, 100.0, 1e-6);
}

TEST(TreemapTest, TwoEqualWeightsTileWithoutOverlap) {
    auto r = Treemap::layout(100, 100, {1.0, 1.0});
    ASSERT_EQ(r.size(), 2u);
    double area = 0.0;
    EXPECT_TRUE(noOverlap(r, area));
    EXPECT_NEAR(area, 10000.0, 0.1);
    for (const auto& rr : r) EXPECT_TRUE(withinBounds(rr, 100, 100));
    // 等权重两块：水平等分
    EXPECT_NEAR(r[0].w, 50.0, 1e-6);
    EXPECT_NEAR(r[0].h, 100.0, 1e-6);
    EXPECT_NEAR(r[1].w, 50.0, 1e-6);
    EXPECT_NEAR(r[1].h, 100.0, 1e-6);
}

TEST(TreemapTest, NonSquareRectLayout) {
    auto r = Treemap::layout(200, 100, {1.0, 1.0});
    ASSERT_EQ(r.size(), 2u);
    double area = 0.0;
    EXPECT_TRUE(noOverlap(r, area));
    EXPECT_NEAR(area, 20000.0, 0.1);
    for (const auto& rr : r) EXPECT_TRUE(withinBounds(rr, 200, 100));
}

TEST(TreemapTest, MultipleWeightsConserveAreaAndBounds) {
    std::vector<double> weights = {3.0, 1.0, 2.0, 4.0, 1.5, 0.5, 2.0};
    auto r = Treemap::layout(320, 200, weights);
    ASSERT_EQ(r.size(), 7u);
    double area = 0.0;
    EXPECT_TRUE(noOverlap(r, area));
    EXPECT_NEAR(area, 320.0 * 200.0, 1.0);
    for (const auto& rr : r) EXPECT_TRUE(withinBounds(rr, 320, 200));
}
