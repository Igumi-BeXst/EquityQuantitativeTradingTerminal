#pragma once

#include "foundation/bar.h"
#include "foundation/portfolio.h"
#include <algorithm>
#include <limits>

namespace st::detail {

/// 最近 period 根的 SMA（offset=0 含当前 bar；lookback 1..period）
inline double smaAt(const BarSeries& s, int period, int offset = 0) {
    if (s.size() < static_cast<size_t>(period + offset + 1)) return 0.0;
    double sum = 0.0;
    for (int i = 1; i <= period; ++i) {
        sum += s.lookback(i + offset).close;
    }
    return sum / period;
}

/// 组合中是否有该代码持仓
inline bool hasPosition(const Portfolio* pf) {
    if (!pf || pf->positions.empty()) return false;
    for (const auto& pos : pf->positions) {
        if (pos.quantity > 0) return true;
    }
    return false;
}

} // namespace st::detail
