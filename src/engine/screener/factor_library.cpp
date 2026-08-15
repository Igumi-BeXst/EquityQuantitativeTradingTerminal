#include "engine/screener/factor_library.h"
#include "foundation/utils/datetime.h"
#include "core/log_manager.h"
#include <ta-lib/ta_libc.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace st {
namespace factors {

namespace {

/// 提取 close 价格数组
std::vector<double> closes(const BarSeries* bars) {
    if (!bars) return {};
    return bars->closes();
}

/// 提取 high 数组
std::vector<double> highs(const BarSeries* bars) {
    if (!bars) return {};
    return bars->highs();
}

/// 提取 low 数组
std::vector<double> lows(const BarSeries* bars) {
    if (!bars) return {};
    return bars->lows();
}

/// 提取 volume 数组
std::vector<double> volumes(const BarSeries* bars) {
    if (!bars) return {};
    auto vols = bars->volumes();
    std::vector<double> result;
    result.reserve(vols.size());
    for (auto v : vols) result.push_back(static_cast<double>(v));
    return result;
}

/// 数组标准差
double stdDev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                  static_cast<double>(values.size());
    double sum = 0.0;
    for (double v : values) sum += (v - mean) * (v - mean);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

/// 计算 SMA
std::vector<double> sma(const std::vector<double>& input, int period) {
    if (input.size() < static_cast<size_t>(period)) return {};
    std::vector<double> result(input.size(), 0.0);
    int begin = 0, outCount = 0;
    TA_RetCode ret = TA_SMA(0, static_cast<int>(input.size()) - 1, input.data(),
                            period, &begin, &outCount, result.data());
    if (ret != TA_SUCCESS) return {};
    // TA-Lib 输出前移 begin 位
    std::vector<double> aligned(input.size(), 0.0);
    for (int i = 0; i < outCount; ++i) {
        aligned[begin + i] = result[i];
    }
    return aligned;
}

/// 将原始值 clamp 到 [0, 100]
double clampScore(double v) {
    return v < 0.0 ? 0.0 : (v > 100.0 ? 100.0 : v);
}

/// 最大值（用于 ADX 等）
template<typename T>
T maxVal(const std::vector<T>& v) {
    return v.empty() ? T{} : *std::max_element(v.begin(), v.end());
}

} // namespace

// ============ 动量类 ============

std::optional<double> RocFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 21) return std::nullopt;
    double prev = c[c.size() - 21];
    if (prev == 0.0) return std::nullopt;
    // 20日动量 (%)
    return (c.back() / prev - 1.0) * 100.0;
}

std::optional<double> RsiFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 15) return std::nullopt;
    std::vector<double> out(c.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_RSI(0, static_cast<int>(c.size()) - 1, c.data(), 14, &begin, &outCount, out.data())
        != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];
}

std::optional<double> MacdHistFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 26) return std::nullopt;
    std::vector<double> macd(c.size(), 0.0);
    std::vector<double> signal(c.size(), 0.0);
    std::vector<double> hist(c.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_MACD(0, static_cast<int>(c.size()) - 1, c.data(), 12, 26, 9,
                &begin, &outCount, macd.data(), signal.data(), hist.data())
        != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return hist[outCount - 1];
}

// ============ 动量类（扩充） ============

std::optional<double> CciFactor::calculate(const FactorContext& ctx) const {
    auto h = highs(ctx.bars);
    auto l = lows(ctx.bars);
    auto c = closes(ctx.bars);
    if (h.size() < 15 || l.size() != h.size() || c.size() != h.size()) return std::nullopt;
    std::vector<double> out(h.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_CCI(0, static_cast<int>(h.size()) - 1, h.data(), l.data(), c.data(),
               14, &begin, &outCount, out.data()) != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];  // 典型 ±100，正值 = 强势
}

double CciFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    // CCI 100 → 100 分；-100 → 0 分
    return clampScore((*value + 100.0) / 2.0);
}

std::optional<double> WilliamsRFactor::calculate(const FactorContext& ctx) const {
    auto h = highs(ctx.bars);
    auto l = lows(ctx.bars);
    auto c = closes(ctx.bars);
    if (h.size() < 15 || l.size() != h.size() || c.size() != h.size()) return std::nullopt;
    std::vector<double> out(h.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_WILLR(0, static_cast<int>(h.size()) - 1, h.data(), l.data(), c.data(),
                 14, &begin, &outCount, out.data()) != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];  // -100~0，接近 0 = 超买（强势）
}

double WilliamsRFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    // -100 → 0 分；0 → 100 分
    return clampScore(100.0 + *value);
}

std::optional<double> BiasFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 7) return std::nullopt;
    auto ma6 = sma(c, 6);
    if (ma6.empty() || ma6.back() <= 0.0) return std::nullopt;
    // 乖离率 % = (close - MA6) / MA6 * 100
    return (c.back() / ma6.back() - 1.0) * 100.0;
}

double BiasFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    // 0 乖离 → 50 分；+20% → 70 分；-20% → 30 分（超买/超卖两方向）
    return clampScore(50.0 + *value);
}

std::optional<double> UpStreakFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 2) return std::nullopt;
    // 从最新往前数连涨天数（收盘价连续上涨）
    int streak = 0;
    for (size_t i = c.size() - 1; i >= 1; --i) {
        if (c[i] > c[i - 1]) {
            ++streak;
        } else {
            break;
        }
        if (i == 1) break;  // 防 size_t 下溢
    }
    return static_cast<double>(streak);
}

double UpStreakFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    return clampScore(*value * 20.0);  // 5 连涨 → 100 分
}

// ============ 波动类 ============

std::optional<double> VolatilityFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 2) return std::nullopt;
    std::vector<double> returns;
    returns.reserve(c.size() - 1);
    for (size_t i = 1; i < c.size(); ++i) {
        if (c[i - 1] > 0) {
            returns.push_back(c[i] / c[i - 1] - 1.0);
        }
    }
    // 年化波动率 (%) = 日波动率 * sqrt(252) * 100
    double dailyVol = stdDev(returns);
    return dailyVol * std::sqrt(252.0) * 100.0;
}

std::optional<double> AtRFactor::calculate(const FactorContext& ctx) const {
    auto h = highs(ctx.bars);
    auto l = lows(ctx.bars);
    auto c = closes(ctx.bars);
    if (h.size() < 15 || l.size() != h.size() || c.size() != h.size()) return std::nullopt;
    std::vector<double> out(h.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_ATR(0, static_cast<int>(h.size()) - 1, h.data(), l.data(), c.data(),
               14, &begin, &outCount, out.data()) != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];
}

std::optional<double> MaxDrawdownFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 2) return std::nullopt;
    double peak = c[0], maxDD = 0.0;
    for (double price : c) {
        if (price > peak) peak = price;
        double dd = (peak - price) / peak;
        if (dd > maxDD) maxDD = dd;
    }
    return maxDD * 100.0;  // %
}

// ============ 质量类 ============

std::optional<double> MAAlignmentFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 60) return std::nullopt;
    auto ma5 = sma(c, 5);
    auto ma20 = sma(c, 20);
    auto ma60 = sma(c, 60);
    if (ma5.empty() || ma20.empty() || ma60.empty()) return std::nullopt;
    // 多头排列: MA5 > MA20 > MA60 → 强，否则弱
    double v5 = ma5.back(), v20 = ma20.back(), v60 = ma60.back();
    if (v5 > v20 && v20 > v60) return 100.0;  // 强多头
    if (v5 > v20) return 75.0;                // 偏多
    if (v5 < v20 && v20 < v60) return 0.0;    // 空头排列
    return 25.0;                              // 偏空
}

std::optional<double> AdxFactor::calculate(const FactorContext& ctx) const {
    auto h = highs(ctx.bars);
    auto l = lows(ctx.bars);
    auto c = closes(ctx.bars);
    if (h.size() < 28 || l.size() != h.size() || c.size() != h.size()) return std::nullopt;
    std::vector<double> out(h.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_ADX(0, static_cast<int>(h.size()) - 1, h.data(), l.data(), c.data(),
               14, &begin, &outCount, out.data()) != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];  // 0~100
}

// ============ 波动类（扩充） ============

std::optional<double> BollPosFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 21) return std::nullopt;
    // 布林带 (20, 2σ)：现价在带内的相对位置 0~1（下轨 0，上轨 1）
    auto ma20 = sma(c, 20);
    if (ma20.empty()) return std::nullopt;
    const size_t n = c.size();
    double sumSq = 0.0;
    for (size_t i = n - 20; i < n; ++i) {
        double d = c[i] - ma20.back();
        sumSq += d * d;
    }
    double sd = std::sqrt(sumSq / 20.0);
    if (sd <= 0.0) return std::nullopt;
    double upper = ma20.back() + 2.0 * sd;
    double lower = ma20.back() - 2.0 * sd;
    if (upper <= lower) return std::nullopt;
    double pos = (c.back() - lower) / (upper - lower);
    return pos < 0.0 ? 0.0 : (pos > 1.0 ? 1.0 : pos);
}

std::optional<double> AmplitudeFactor::calculate(const FactorContext& ctx) const {
    auto h = highs(ctx.bars);
    auto l = lows(ctx.bars);
    auto c = closes(ctx.bars);
    if (h.size() < 21 || l.size() != h.size() || c.size() != h.size()) return std::nullopt;
    // 20 日均振幅 % = (high-low)/prevClose 的均值
    double sum = 0.0;
    int count = 0;
    for (size_t i = h.size() - 20; i < h.size(); ++i) {
        if (i == 0 || c[i - 1] <= 0.0) continue;
        sum += (h[i] - l[i]) / c[i - 1] * 100.0;
        ++count;
    }
    if (count <= 0) return std::nullopt;
    return sum / static_cast<double>(count);
}

// ============ 质量类（扩充） ============

std::optional<double> MaCrossFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 21) return std::nullopt;
    auto ma5 = sma(c, 5);
    auto ma20 = sma(c, 20);
    if (ma5.empty() || ma20.empty()) return std::nullopt;
    // 金叉状态：MA5 > MA20 → 100；MA5 < MA20 → 0；相等 → 50
    double v5 = ma5.back(), v20 = ma20.back();
    if (v5 > v20) return 100.0;
    if (v5 < v20) return 0.0;
    return 50.0;
}

std::optional<double> PricePosFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 2) return std::nullopt;
    // 52 周价格位置：现价在近 250 日高低区间中的位置 0~100
    const size_t n = std::min<size_t>(c.size(), 250);
    double lo = c[c.size() - n], hi = c[c.size() - n];
    for (size_t i = c.size() - n; i < c.size(); ++i) {
        lo = std::min(lo, c[i]);
        hi = std::max(hi, c[i]);
    }
    if (hi <= lo) return 50.0;
    return (c.back() - lo) / (hi - lo) * 100.0;
}

std::optional<double> MaSlopeFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    if (c.size() < 26) return std::nullopt;
    auto ma20 = sma(c, 20);
    if (ma20.empty()) return std::nullopt;
    const size_t n = c.size();
    // 最近 5 日 MA20 斜率（每日涨幅 %）
    if (n < 6 || ma20[n - 6] <= 0.0) return std::nullopt;
    return (ma20[n - 1] / ma20[n - 6] - 1.0) * 100.0 / 5.0;
}

double MaSlopeFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    // 斜率 ±0.5%/日 → 0/100 分
    return clampScore(50.0 + *value * 100.0);
}

// ============ 量价类 ============

std::optional<double> VolumeRatioFactor::calculate(const FactorContext& ctx) const {
    auto v = volumes(ctx.bars);
    if (v.size() < 6) return std::nullopt;
    // 量比 = 当日量 / 前5日均量
    double avg5 = 0.0;
    for (size_t i = v.size() - 6; i < v.size() - 1; ++i) {
        avg5 += v[i];
    }
    avg5 /= 5.0;
    if (avg5 <= 0) return std::nullopt;
    return v.back() / avg5;
}

std::optional<double> TurnoverFactor::calculate(const FactorContext& ctx) const {
    if (!ctx.bars || ctx.bars->empty()) return std::nullopt;
    return ctx.bars->current().turnoverRate * 100.0;  // %
}

std::optional<double> ObvFactor::calculate(const FactorContext& ctx) const {
    if (!ctx.bars || ctx.bars->size() < 2) return std::nullopt;
    // OBV 累计: 收盘涨则加量，跌则减量
    auto c = closes(ctx.bars);
    auto v = volumes(ctx.bars);
    if (c.size() != v.size()) return std::nullopt;
    double obv = 0.0;
    for (size_t i = 1; i < c.size(); ++i) {
        if (c[i] > c[i - 1]) obv += v[i];
        else if (c[i] < c[i - 1]) obv -= v[i];
    }
    return obv;
}

// ============ 量价类（扩充） ============

std::optional<double> MfiFactor::calculate(const FactorContext& ctx) const {
    auto h = highs(ctx.bars);
    auto l = lows(ctx.bars);
    auto c = closes(ctx.bars);
    auto v = volumes(ctx.bars);
    if (h.size() < 15 || l.size() != h.size() || c.size() != h.size() ||
        v.size() != h.size()) {
        return std::nullopt;
    }
    std::vector<double> out(h.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_MFI(0, static_cast<int>(h.size()) - 1, h.data(), l.data(), c.data(),
               v.data(), 14, &begin, &outCount, out.data()) != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];  // 0~100，高 = 资金流入
}

std::optional<double> VolPriceFactor::calculate(const FactorContext& ctx) const {
    auto c = closes(ctx.bars);
    auto v = volumes(ctx.bars);
    if (c.size() < 6 || v.size() != c.size()) return std::nullopt;
    // 量价配合：近 5 日「涨放量/跌缩量」占比（%）
    int good = 0, total = 0;
    for (size_t i = c.size() - 5; i < c.size(); ++i) {
        if (i == 0 || c[i - 1] <= 0.0 || v[i - 1] <= 0.0) continue;
        bool up = c[i] > c[i - 1];
        bool volUp = v[i] > v[i - 1];
        if (up == volUp) ++good;  // 涨放量 或 跌缩量 = 健康
        ++total;
    }
    if (total <= 0) return std::nullopt;
    return static_cast<double>(good) / static_cast<double>(total) * 100.0;
}

// ============ 估值类（需 ctx.quote） ============

std::optional<double> PeTtmFactor::calculate(const FactorContext& ctx) const {
    if (!ctx.quote || !ctx.quote->valid || ctx.quote->peTtm <= 0.0) return std::nullopt;
    return ctx.quote->peTtm;
}

double PeTtmFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    // 低 PE 高分：PE 10 → 90 分；PE 50 → 50 分；PE 100 → 0 分
    return clampScore(100.0 - *value);
}

std::optional<double> MarketCapFactor::calculate(const FactorContext& ctx) const {
    if (!ctx.quote || !ctx.quote->valid || ctx.quote->marketCap <= 0.0) return std::nullopt;
    return ctx.quote->marketCap;
}

double MarketCapFactor::toScore(std::optional<double> value) const {
    if (!value.has_value()) return 50.0;
    // 小市值偏好（对数压缩）：1000 亿 → ~55 分；100 亿 → ~70 分；10 亿 → ~85 分
    double logCap = std::log10(*value);
    return clampScore(115.0 - logCap * 5.0);
}

// ============ 默认因子集 ============

std::vector<std::pair<std::shared_ptr<IFactor>, double>> defaultFactorSet() {
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> result;
    // 动量
    result.emplace_back(std::make_shared<RocFactor>(), 1.0);
    result.emplace_back(std::make_shared<RsiFactor>(), 0.5);
    result.emplace_back(std::make_shared<MacdHistFactor>(), 0.5);
    result.emplace_back(std::make_shared<CciFactor>(), 0.3);
    result.emplace_back(std::make_shared<WilliamsRFactor>(), 0.3);
    result.emplace_back(std::make_shared<BiasFactor>(), 0.3);
    result.emplace_back(std::make_shared<UpStreakFactor>(), 0.2);
    // 波动
    result.emplace_back(std::make_shared<VolatilityFactor>(), 0.3);
    result.emplace_back(std::make_shared<AtRFactor>(), 0.2);
    result.emplace_back(std::make_shared<MaxDrawdownFactor>(), 0.3);
    result.emplace_back(std::make_shared<BollPosFactor>(), 0.3);
    result.emplace_back(std::make_shared<AmplitudeFactor>(), 0.2);
    // 质量
    result.emplace_back(std::make_shared<MAAlignmentFactor>(), 1.0);
    result.emplace_back(std::make_shared<AdxFactor>(), 0.3);
    result.emplace_back(std::make_shared<MaCrossFactor>(), 0.4);
    result.emplace_back(std::make_shared<PricePosFactor>(), 0.3);
    result.emplace_back(std::make_shared<MaSlopeFactor>(), 0.3);
    // 量价
    result.emplace_back(std::make_shared<VolumeRatioFactor>(), 0.3);
    result.emplace_back(std::make_shared<TurnoverFactor>(), 0.2);
    result.emplace_back(std::make_shared<ObvFactor>(), 0.2);
    result.emplace_back(std::make_shared<MfiFactor>(), 0.3);
    result.emplace_back(std::make_shared<VolPriceFactor>(), 0.3);
    // 估值（依赖基本面快照注入；无数据时缺失折减为中性 50 分）
    result.emplace_back(std::make_shared<PeTtmFactor>(), 0.3);
    result.emplace_back(std::make_shared<MarketCapFactor>(), 0.2);
    return result;
}

} // namespace factors
} // namespace st
