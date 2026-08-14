#pragma once

#include "engine/optimizer/grid_search.h"
#include <optional>
#include <string>
#include <vector>

namespace st {

/// 网格结果 → 热力图矩阵（x/y 轴参数名、行列值、目标值矩阵）
///
/// 纯 C++17，无 Qt 依赖，可单测。values[y][x] 对应 yValues[y] × xValues[x]。
struct HeatmapMatrix {
    std::string xParam;                    // x 轴参数名
    std::string yParam;                    // y 轴参数名
    std::vector<double> xValues;           // x 参数值（升序、去重）
    std::vector<double> yValues;           // y 参数值（升序、去重）
    std::vector<std::vector<double>> values;  // values[y][x] = objectiveValue；缺格 NaN
};

/// 从网格搜索结果构建热力图矩阵。
///
/// 规则：
///  - results 为空 / xParam 或 yParam 为空 / xParam == yParam → nullopt
///  - 任一结果缺少 xParam 或 yParam 参数 → nullopt（参数名不存在）
///  - 坐标轴升序去重；缺失组合的格子为 NaN
///  - 同一 (x,y) 出现多次 → 后者覆盖前者（last wins）
std::optional<HeatmapMatrix> buildHeatmap(
    const std::vector<GridSearchResult>& results,
    const std::string& xParam, const std::string& yParam);

} // namespace st
