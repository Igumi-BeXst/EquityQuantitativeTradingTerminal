#pragma once

#include <vector>

namespace st::indicators {

/// 简单移动平均。输出与输入等长，前 (period-1) 个为 NaN（数据不足）。
[[nodiscard]] std::vector<double> sma(const std::vector<double>& data, int period);

/// 指数移动平均（种子 = 首 period 个均值，α = 2/(period+1)）。
/// 输出与输入等长，前 (period-1) 个为 NaN。
[[nodiscard]] std::vector<double> ema(const std::vector<double>& data, int period);

/// MACD：dif = ema(fast) - ema(slow)；dea = ema(dif, signal)；hist = 2*(dif-dea)（A 股惯例）。
/// 输出与输入等长（前导段 NaN）。
struct MacdResult {
    std::vector<double> dif;
    std::vector<double> dea;
    std::vector<double> hist;
};
[[nodiscard]] MacdResult macd(const std::vector<double>& closes,
                              int fast = 12, int slow = 26, int signal = 9);

/// RSI（Wilder 平滑）。全涨=100，全跌=0。输出与输入等长（前导段 NaN）。
[[nodiscard]] std::vector<double> rsi(const std::vector<double>& closes, int period = 14);

/// BOLL 布林带：mid = SMA(period)，upper/lower = mid ± k * 样本标准差。
/// 输出与输入等长（前导段 NaN）。
struct BollResult {
    std::vector<double> mid;
    std::vector<double> upper;
    std::vector<double> lower;
};
[[nodiscard]] BollResult boll(const std::vector<double>& closes,
                              int period = 20, double k = 2.0);

} // namespace st::indicators
