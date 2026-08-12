#pragma once

#include "foundation/bar.h"
#include "foundation/types.h"
#include <optional>
#include <vector>

namespace st {

/// K线区间统计 — 纯 C++17，无 Qt 依赖，可单测
struct RangeStats {
    DateTime fromDate;      // 区间首 bar 日期（原始）
    DateTime toDate;        // 区间末 bar 日期（原始）
    int barCount = 0;       // 区间 bar 总数（含无效 bar）
    double openClosePct = 0.0;  // 涨跌幅：lastValidClose / baseOpen - 1
    double high = 0.0;      // 区间最高价（有效 bar 极值）
    DateTime highDate;
    double low = 0.0;       // 区间最低价
    DateTime lowDate;
    double amplitude = 0.0; // 振幅：(high - low) / baseOpen
    double totalVolume = 0.0;  // 累计成交量（股）
    double totalAmount = 0.0;  // 累计成交额（元）
    double turnoverSum = 0.0;  // 区间换手率：各有效 bar turnoverRate 累加
    double avgPrice = 0.0;     // 均价：totalAmount / totalVolume（totalVolume<=0 → 0）
};

/// 计算闭区间 [from, to]（含端点）统计。
/// 空 bars / from<0 / to<0 / from>=size / to>=size / from>to → nullopt。
/// 区间内全为无效 bar → nullopt；无效 bar 跳过但计入 barCount；
/// baseOpen = 区间内首个有效 bar 的 open；涨跌幅取最后一个有效 bar 的 close。
std::optional<RangeStats> computeRangeStats(const std::vector<Bar>& bars, int from, int to);

} // namespace st
