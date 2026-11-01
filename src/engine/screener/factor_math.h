#pragma once

#include "foundation/bar.h"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace st::factors::math {

/// 因子序列：与 BarSeries 等长对齐，前导无效值为 NaN
using Series = std::vector<double>;

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ============================ 基础序列 ============================

Series openSeries(const BarSeries& bars);
Series highSeries(const BarSeries& bars);
Series lowSeries(const BarSeries& bars);
Series closeSeries(const BarSeries& bars);
Series volumeSeries(const BarSeries& bars);
Series amountSeries(const BarSeries& bars);
/// VWAP = 成交额 / 成交量；成交额缺失（<=0）时回退典型价 (H+L+C)/3
Series vwapSeries(const BarSeries& bars);
/// 日收益率 close/delay(close,1)-1（首值 NaN）
Series returnsSeries(const Series& close);
/// 全 0 序列（配合 where 使用）
Series zeros(size_t n);

// ============================ 逐元素运算 ============================
// 所有函数保持输入长度；任一操作数为 NaN 时结果 NaN（比较/条件函数除外）

Series add(const Series& a, const Series& b);
Series sub(const Series& a, const Series& b);
Series mul(const Series& a, const Series& b);
Series div(const Series& a, const Series& b);
Series addScalar(const Series& a, double k);
Series subScalar(const Series& a, double k);  // a - k
Series mulScalar(const Series& a, double k);
Series divScalar(const Series& a, double k);  // a / k
Series eabs(const Series& a);
Series esign(const Series& a);
Series elog(const Series& a);
Series esqrt(const Series& a);
Series epow(const Series& a, double p);
/// 逐元素 max/min（任一 NaN → 另一侧；两侧均 NaN → NaN）
Series emax(const Series& a, const Series& b);
Series emin(const Series& a, const Series& b);
Series emaxScalar(const Series& a, double k);
Series eminScalar(const Series& a, double k);

/// 比较：NaN 参与比较返回 0（条件不成立）
Series egt(const Series& a, const Series& b);
Series elt(const Series& a, const Series& b);
Series ege(const Series& a, const Series& b);
Series ele(const Series& a, const Series& b);
Series eeq(const Series& a, const Series& b);

/// 条件选择：cond > 0（且非 NaN）取 a，否则取 b
Series ewhere(const Series& cond, const Series& a, const Series& b);

// ============================ 时间序列算子 ============================

/// 向前偏移 n 根（n<=0 原样复制；前 n 个为 NaN）
Series delay(const Series& a, int n);
/// a - delay(a, n)
Series delta(const Series& a, int n);
/// 最近 n 根求和/均值/总体标准差（窗口不足 NaN）
Series tsSum(const Series& a, int n);
Series tsMean(const Series& a, int n);
Series tsStd(const Series& a, int n);
/// 最近 n 根最大/最小
Series tsMax(const Series& a, int n);
Series tsMin(const Series& a, int n);
/// 当前值在最近 n 根中的分位排名 [0,1]（0 = 最小，1 = 最大；窗口不足 NaN）
Series tsRank(const Series& a, int n);
/// 最近 n 根中 cond>0 的个数
Series count(const Series& cond, int n);
/// 最近 n 根中 cond>0 位置的 values 之和（无满足项 = 0）
Series sumIf(const Series& values, const Series& cond, int n);
/// 滚动相关系数 / 协方差（窗口不足 NaN；方差为 0 → NaN）
Series corr(const Series& x, const Series& y, int n);
Series covar(const Series& x, const Series& y, int n);
/// 线性衰减加权均值（最近一根权重 n，最远 1，总权重归一）
Series decayLinear(const Series& a, int n);
/// 线性加权均值（最近一根权重 n，最远 1）
Series wma(const Series& a, int n);
/// 中国式 SMA：y = (m*x + (n-m)*y_prev)/n（首值取首个有效 x）
Series smaCn(const Series& a, int n, int m);
/// 滚动 OLS 斜率：y 对 x 回归（窗口不足 NaN）
Series regBeta(const Series& y, const Series& x, int n);
/// y 对 [0..n-1] 序列回归的斜率（REGBETA(y, SEQUENCE)）
Series regBetaSeq(const Series& y, int n);
/// 最近 n 根最低点距今的天数（0 = 今天最小；窗口不足 NaN）
Series lowDay(const Series& a, int n);
/// 最近 n 根最高点距今的天数（0 = 今天最大；窗口不足 NaN）
Series highDay(const Series& a, int n);

// ============================ 取值 ============================

/// 最后一个有限值（无则 nullopt）
std::optional<double> lastValid(const Series& a);
/// 最后一个值（可能 NaN）
double lastValue(const Series& a);

} // namespace st::factors::math
