#include "engine/optimizer/grid_heatmap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace st {

namespace {

double kNaN() { return std::numeric_limits<double>::quiet_NaN(); }

/// 升序去重（参数值为整数，double 承载）
void sortUnique(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

/// 二分查找坐标（值必然存在，调用前已校验）
int indexOf(const std::vector<double>& axis, double value) {
    const auto it = std::lower_bound(axis.begin(), axis.end(), value);
    return static_cast<int>(it - axis.begin());
}

}  // namespace

std::optional<HeatmapMatrix> buildHeatmap(
    const std::vector<GridSearchResult>& results,
    const std::string& xParam, const std::string& yParam) {

    if (results.empty() || xParam.empty() || yParam.empty() || xParam == yParam) {
        return std::nullopt;
    }

    HeatmapMatrix m;
    m.xParam = xParam;
    m.yParam = yParam;

    for (const auto& r : results) {
        std::optional<double> xv, yv;
        for (const auto& [name, val] : r.params) {
            if (name == xParam) xv = static_cast<double>(val);
            else if (name == yParam) yv = static_cast<double>(val);
        }
        if (!xv || !yv) return std::nullopt;  // 参数名不在结果中 → 无效请求
        m.xValues.push_back(*xv);
        m.yValues.push_back(*yv);
    }

    sortUnique(m.xValues);
    sortUnique(m.yValues);
    m.values.assign(m.yValues.size(),
                    std::vector<double>(m.xValues.size(), kNaN()));

    for (const auto& r : results) {
        double xv = 0.0, yv = 0.0;
        for (const auto& [name, val] : r.params) {
            if (name == xParam) xv = static_cast<double>(val);
            else if (name == yParam) yv = static_cast<double>(val);
        }
        const int xi = indexOf(m.xValues, xv);
        const int yi = indexOf(m.yValues, yv);
        m.values[static_cast<size_t>(yi)][static_cast<size_t>(xi)] = r.objectiveValue;
    }

    return m;
}

} // namespace st
