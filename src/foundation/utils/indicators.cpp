#include "foundation/utils/indicators.h"

#include <cmath>
#include <limits>

namespace st::indicators {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline double meanOf(const std::vector<double>& v, int start, int count) {
    double sum = 0.0;
    for (int i = start; i < start + count; ++i) sum += v[static_cast<size_t>(i)];
    return sum / count;
}
}  // namespace

std::vector<double> sma(const std::vector<double>& data, int period) {
    std::vector<double> out(data.size(), kNaN);
    if (period <= 0 || static_cast<int>(data.size()) < period) return out;

    double sum = 0.0;
    for (int i = 0; i < static_cast<int>(data.size()); ++i) {
        sum += data[static_cast<size_t>(i)];
        if (i >= period) sum -= data[static_cast<size_t>(i - period)];
        if (i >= period - 1) out[static_cast<size_t>(i)] = sum / period;
    }
    return out;
}

std::vector<double> ema(const std::vector<double>& data, int period) {
    std::vector<double> out(data.size(), kNaN);
    if (period <= 0 || static_cast<int>(data.size()) < period) return out;

    // 定位首个连续有限窗口 [start, start+period) 作种子（容忍输入前导 NaN，如 MACD dif）
    int start = -1;
    for (int i = 0; i + period <= static_cast<int>(data.size()); ++i) {
        bool ok = true;
        for (int j = 0; j < period; ++j) {
            if (!std::isfinite(data[static_cast<size_t>(i + j)])) { ok = false; break; }
        }
        if (ok) { start = i; break; }
    }
    if (start < 0) return out;

    const double alpha = 2.0 / (period + 1.0);
    double prev = meanOf(data, start, period);
    out[static_cast<size_t>(start + period - 1)] = prev;
    for (int i = start + period; i < static_cast<int>(data.size()); ++i) {
        prev = alpha * data[static_cast<size_t>(i)] + (1.0 - alpha) * prev;
        out[static_cast<size_t>(i)] = prev;
    }
    return out;
}

MacdResult macd(const std::vector<double>& closes, int fast, int slow, int signal) {
    MacdResult r;
    const auto fastEma = ema(closes, fast);
    const auto slowEma = ema(closes, slow);

    std::vector<double> dif(closes.size(), kNaN);
    for (size_t i = 0; i < closes.size(); ++i) {
        if (std::isfinite(fastEma[i]) && std::isfinite(slowEma[i])) {
            dif[i] = fastEma[i] - slowEma[i];
        }
    }
    r.dif = dif;

    r.dea = ema(dif, signal);  // 前导 NaN 的 ema 从首个有限值起算

    r.hist.resize(closes.size(), kNaN);
    for (size_t i = 0; i < closes.size(); ++i) {
        if (std::isfinite(r.dif[i]) && std::isfinite(r.dea[i])) {
            r.hist[i] = 2.0 * (r.dif[i] - r.dea[i]);
        }
    }
    return r;
}

std::vector<double> rsi(const std::vector<double>& closes, int period) {
    std::vector<double> out(closes.size(), kNaN);
    if (period <= 0 || static_cast<int>(closes.size()) < period + 1) return out;

    // 首个平均值段
    double avgGain = 0.0, avgLoss = 0.0;
    for (int i = 1; i <= period; ++i) {
        double diff = closes[static_cast<size_t>(i)] - closes[static_cast<size_t>(i - 1)];
        if (diff >= 0) avgGain += diff; else avgLoss -= diff;
    }
    avgGain /= period;
    avgLoss /= period;

    auto rsiValue = [&](double gain, double loss) {
        if (loss == 0.0) return 100.0;
        if (gain == 0.0) return 0.0;
        double rs = gain / loss;
        return 100.0 - 100.0 / (1.0 + rs);
    };

    out[static_cast<size_t>(period)] = rsiValue(avgGain, avgLoss);
    for (int i = period + 1; i < static_cast<int>(closes.size()); ++i) {
        double diff = closes[static_cast<size_t>(i)] - closes[static_cast<size_t>(i - 1)];
        double gain = diff >= 0 ? diff : 0.0;
        double loss = diff < 0 ? -diff : 0.0;
        avgGain = (avgGain * (period - 1) + gain) / period;   // Wilder 平滑
        avgLoss = (avgLoss * (period - 1) + loss) / period;
        out[static_cast<size_t>(i)] = rsiValue(avgGain, avgLoss);
    }
    return out;
}

BollResult boll(const std::vector<double>& closes, int period, double k) {
    BollResult r;
    r.mid = sma(closes, period);
    r.upper.resize(closes.size(), kNaN);
    r.lower.resize(closes.size(), kNaN);

    if (period <= 0 || static_cast<int>(closes.size()) < period) return r;

    for (int i = period - 1; i < static_cast<int>(closes.size()); ++i) {
        const double m = r.mid[static_cast<size_t>(i)];
        // 样本标准差
        double sqSum = 0.0;
        for (int j = i - period + 1; j <= i; ++j) {
            double d = closes[static_cast<size_t>(j)] - m;
            sqSum += d * d;
        }
        double sd = std::sqrt(sqSum / period);
        r.upper[static_cast<size_t>(i)] = m + k * sd;
        r.lower[static_cast<size_t>(i)] = m - k * sd;
    }
    return r;
}

} // namespace st::indicators
