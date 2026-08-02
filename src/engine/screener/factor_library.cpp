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
    if (h.size() < 28) return std::nullopt;
    std::vector<double> out(h.size(), 0.0);
    int begin = 0, outCount = 0;
    if (TA_ADX(0, static_cast<int>(h.size()) - 1, h.data(), l.data(), c.data(),
               14, &begin, &outCount, out.data()) != TA_SUCCESS) {
        return std::nullopt;
    }
    if (outCount <= 0) return std::nullopt;
    return out[outCount - 1];  // 0~100
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

// ============ 默认因子集 ============

std::vector<std::pair<std::shared_ptr<IFactor>, double>> defaultFactorSet() {
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> result;
    // 动量
    result.emplace_back(std::make_shared<RocFactor>(), 1.0);
    result.emplace_back(std::make_shared<RsiFactor>(), 0.5);
    result.emplace_back(std::make_shared<MacdHistFactor>(), 0.5);
    // 波动
    result.emplace_back(std::make_shared<VolatilityFactor>(), 0.3);
    result.emplace_back(std::make_shared<AtRFactor>(), 0.2);
    result.emplace_back(std::make_shared<MaxDrawdownFactor>(), 0.3);
    // 质量
    result.emplace_back(std::make_shared<MAAlignmentFactor>(), 1.0);
    result.emplace_back(std::make_shared<AdxFactor>(), 0.3);
    // 量价
    result.emplace_back(std::make_shared<VolumeRatioFactor>(), 0.3);
    result.emplace_back(std::make_shared<TurnoverFactor>(), 0.2);
    result.emplace_back(std::make_shared<ObvFactor>(), 0.2);
    return result;
}

} // namespace factors
} // namespace st
