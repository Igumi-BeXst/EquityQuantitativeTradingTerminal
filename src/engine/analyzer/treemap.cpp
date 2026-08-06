#include "engine/analyzer/treemap.h"

#include <algorithm>
#include <cmath>

namespace st {

namespace {

/// 行内最差纵横比（厚度 t 下）：max(长边/短边)。用于判断加项是否让行更歪。
double worstAspect(const std::vector<double>& areas, double thickness) {
    double worst = 1.0;
    if (thickness <= 0.0) return 1.0;
    const double t2 = thickness * thickness;
    for (const double a : areas) {
        const double r = std::max(a / t2, t2 / a);
        worst = std::max(worst, r);
    }
    return worst;
}

}  // namespace

std::vector<TreemapRect> Treemap::layout(double width, double height,
                                         const std::vector<double>& weights) {
    std::vector<TreemapRect> rects;
    if (width <= 0.0 || height <= 0.0) return rects;

    // 过滤非正权重，按面积占比归一化到总区域
    std::vector<double> areas;
    areas.reserve(weights.size());
    double total = 0.0;
    for (const double w : weights) {
        if (w > 0.0) {
            areas.push_back(w);
            total += w;
        }
    }
    if (areas.empty() || total <= 0.0) return rects;

    std::sort(areas.begin(), areas.end(), std::greater<double>());
    const double scale = width * height / total;
    for (double& a : areas) a *= scale;

    double remX = 0.0, remY = 0.0, remW = width, remH = height;
    std::vector<double> row;
    double rowSum = 0.0;

    // 铺一行：行沿剩余矩形短边作厚度，沿长边延伸
    const auto flush = [&]() {
        if (row.empty()) return;
        const double thickness = std::min(remW, remH);
        if (thickness <= 0.0) {
            row.clear();
            rowSum = 0.0;
            return;
        }
        const double longSide = std::max(remW, remH);
        const double rowLen = std::min(rowSum / thickness, longSide);
        if (remW >= remH) {
            // 水平行：厚=remH，铺在顶部
            double xx = remX;
            for (const double a : row) {
                const double w = rowSum > 0.0 ? a / rowSum * rowLen : 0.0;
                rects.push_back({xx, remY, w, thickness});
                xx += w;
            }
            remX += rowLen;
            remW -= rowLen;
        } else {
            // 垂直行：厚=remW，铺在左侧
            double yy = remY;
            for (const double a : row) {
                const double h = rowSum > 0.0 ? a / rowSum * rowLen : 0.0;
                rects.push_back({remX, yy, thickness, h});
                yy += h;
            }
            remY += rowLen;
            remH -= rowLen;
        }
        row.clear();
        rowSum = 0.0;
    };

    size_t i = 0;
    while (i < areas.size()) {
        const double a = areas[i];
        const double thickness = std::min(remW, remH);
        const double longSide = std::max(remW, remH);
        const double worstRow = row.empty() ? 1.0 : worstAspect(row, thickness);
        // 加入 a 是否：不溢出（面积守恒保证最终能放下）+ 不使行更歪
        const bool fits = row.empty() ||
            (rowSum + a) / thickness <= longSide + 1e-9;
        const bool aspectOk = row.empty() ||
            (a / (thickness * thickness) <= worstRow &&
             (thickness * thickness) / a <= worstRow);
        if (fits && aspectOk) {
            row.push_back(a);
            rowSum += a;
            ++i;
        } else if (!row.empty()) {
            flush();
        } else {
            // 单元素也必须放入（面积守恒保证不溢出）
            row.push_back(a);
            rowSum += a;
            ++i;
        }
    }
    flush();
    return rects;
}

} // namespace st
