#pragma once

#include <vector>

namespace st {

/// Treemap 矩形
struct TreemapRect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

/// Squarified 矩形树布局（Bruls et al.）
///
/// 输入权重（面积比例），输出铺满指定矩形的矩形序列（与输入顺序一致，权重按面积占比缩放）。
/// 权重 ≤0 过滤；空/全零 → 空；总面积 = w×h（浮点 epsilon 内）；矩形不重叠、均在边界内。
class Treemap {
public:
    static std::vector<TreemapRect> layout(double width, double height,
                                           const std::vector<double>& weights);
};

} // namespace st
