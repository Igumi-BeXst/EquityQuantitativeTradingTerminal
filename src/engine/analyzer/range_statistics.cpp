#include "engine/analyzer/range_statistics.h"

#include <algorithm>

namespace st {

std::optional<RangeStats> computeRangeStats(const std::vector<Bar>& bars, int from, int to) {
    if (bars.empty() || from < 0 || to < 0 ||
        from >= static_cast<int>(bars.size()) ||
        to >= static_cast<int>(bars.size()) || from > to) {
        return std::nullopt;
    }
    RangeStats rs;
    rs.fromDate = bars[static_cast<size_t>(from)].time;
    rs.toDate = bars[static_cast<size_t>(to)].time;
    rs.barCount = to - from + 1;

    double baseOpen = 0.0;
    double lastClose = 0.0;
    bool hasValid = false;
    double high = -1.0;    // 价格恒正，-1 哨兵
    double low = 1e300;    // 大数哨兵
    for (int i = from; i <= to; ++i) {
        const auto& b = bars[static_cast<size_t>(i)];
        if (!b.isValid()) continue;
        if (!hasValid) { baseOpen = b.open; hasValid = true; }
        if (b.high > high) { high = b.high; rs.highDate = b.time; }
        if (b.low < low)   { low  = b.low;  rs.lowDate  = b.time; }
        rs.totalVolume += static_cast<double>(b.volume);
        rs.totalAmount += b.amount;
        rs.turnoverSum += b.turnoverRate;
        lastClose = b.close;
    }
    if (!hasValid) return std::nullopt;   // 区间内全无效 bar → 无可统计
    rs.high = high;
    rs.low = low;
    rs.openClosePct = lastClose / baseOpen - 1.0;
    rs.amplitude = (high - low) / baseOpen;
    if (rs.totalVolume > 0) rs.avgPrice = rs.totalAmount / rs.totalVolume;
    return rs;
}

} // namespace st
