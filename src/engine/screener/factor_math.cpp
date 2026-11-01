#include "engine/screener/factor_math.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

namespace st::factors::math {

namespace {

Series make(size_t n) { return Series(n, kNaN); }

template <typename F>
Series binaryOp(const Series& a, const Series& b, F f) {
    const size_t n = std::min(a.size(), b.size());
    Series out = make(n);
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(a[i]) || std::isnan(b[i])) continue;
        out[i] = f(a[i], b[i]);
    }
    return out;
}

template <typename F>
Series scalarOp(const Series& a, double k, F f) {
    Series out = make(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::isnan(a[i])) continue;
        out[i] = f(a[i], k);
    }
    return out;
}

template <typename F>
Series unaryOp(const Series& a, F f) {
    Series out = make(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::isnan(a[i])) continue;
        out[i] = f(a[i]);
    }
    return out;
}

/// 窗口前缀和（sum / count of finite），用于 O(n) 滚动算子
struct WindowPrefix {
    std::vector<double> sum;
    std::vector<double> sumSq;
    std::vector<int> count;

    explicit WindowPrefix(const Series& a) {
        const size_t n = a.size();
        sum.assign(n + 1, 0.0);
        sumSq.assign(n + 1, 0.0);
        count.assign(n + 1, 0);
        for (size_t i = 0; i < n; ++i) {
            const bool ok = std::isfinite(a[i]);
            sum[i + 1] = sum[i] + (ok ? a[i] : 0.0);
            sumSq[i + 1] = sumSq[i] + (ok ? a[i] * a[i] : 0.0);
            count[i + 1] = count[i] + (ok ? 1 : 0);
        }
    }
};

}  // namespace

// ============================ 基础序列 ============================

Series openSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) out[i] = bars[i].open;
    return out;
}

Series highSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) out[i] = bars[i].high;
    return out;
}

Series lowSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) out[i] = bars[i].low;
    return out;
}

Series closeSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) out[i] = bars[i].close;
    return out;
}

Series volumeSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) {
        out[i] = static_cast<double>(bars[i].volume);
    }
    return out;
}

Series amountSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) out[i] = bars[i].amount;
    return out;
}

Series vwapSeries(const BarSeries& bars) {
    Series out(bars.size(), kNaN);
    for (size_t i = 0; i < bars.size(); ++i) {
        const auto& b = bars[i];
        const double vol = static_cast<double>(b.volume);
        if (b.amount > 0.0 && vol > 0.0) {
            out[i] = b.amount / vol;
        } else if (b.high > 0.0 && b.low > 0.0 && b.close > 0.0) {
            out[i] = (b.high + b.low + b.close) / 3.0;
        }
    }
    return out;
}

Series returnsSeries(const Series& close) {
    Series out = make(close.size());
    for (size_t i = 1; i < close.size(); ++i) {
        if (std::isfinite(close[i]) && std::isfinite(close[i - 1]) &&
            close[i - 1] > 0.0) {
            out[i] = close[i] / close[i - 1] - 1.0;
        }
    }
    return out;
}

Series zeros(size_t n) { return Series(n, 0.0); }

// ============================ 逐元素运算 ============================

Series add(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x + y; });
}

Series sub(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x - y; });
}

Series mul(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x * y; });
}

Series div(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) {
        return y == 0.0 ? kNaN : x / y;
    });
}

Series addScalar(const Series& a, double k) {
    return scalarOp(a, k, [](double x, double y) { return x + y; });
}

Series subScalar(const Series& a, double k) {
    return scalarOp(a, k, [](double x, double y) { return x - y; });
}

Series mulScalar(const Series& a, double k) {
    return scalarOp(a, k, [](double x, double y) { return x * y; });
}

Series divScalar(const Series& a, double k) {
    return scalarOp(a, k, [](double x, double y) {
        return y == 0.0 ? kNaN : x / y;
    });
}

Series eabs(const Series& a) {
    return unaryOp(a, [](double x) { return std::abs(x); });
}

Series esign(const Series& a) {
    return unaryOp(a, [](double x) {
        return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
    });
}

Series elog(const Series& a) {
    return unaryOp(a, [](double x) {
        return x > 0.0 ? std::log(x) : kNaN;
    });
}

Series esqrt(const Series& a) {
    return unaryOp(a, [](double x) {
        return x >= 0.0 ? std::sqrt(x) : kNaN;
    });
}

Series epow(const Series& a, double p) {
    return unaryOp(a, [p](double x) { return std::pow(x, p); });
}

Series emax(const Series& a, const Series& b) {
    const size_t n = std::min(a.size(), b.size());
    Series out = make(n);
    for (size_t i = 0; i < n; ++i) {
        const bool va = !std::isnan(a[i]);
        const bool vb = !std::isnan(b[i]);
        if (va && vb) out[i] = std::max(a[i], b[i]);
        else if (va) out[i] = a[i];
        else if (vb) out[i] = b[i];
    }
    return out;
}

Series emin(const Series& a, const Series& b) {
    const size_t n = std::min(a.size(), b.size());
    Series out = make(n);
    for (size_t i = 0; i < n; ++i) {
        const bool va = !std::isnan(a[i]);
        const bool vb = !std::isnan(b[i]);
        if (va && vb) out[i] = std::min(a[i], b[i]);
        else if (va) out[i] = a[i];
        else if (vb) out[i] = b[i];
    }
    return out;
}

Series emaxScalar(const Series& a, double k) {
    Series out = make(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isnan(a[i])) out[i] = std::max(a[i], k);
    }
    return out;
}

Series eminScalar(const Series& a, double k) {
    Series out = make(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isnan(a[i])) out[i] = std::min(a[i], k);
    }
    return out;
}

Series egt(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x > y ? 1.0 : 0.0; });
}

Series elt(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x < y ? 1.0 : 0.0; });
}

Series ege(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x >= y ? 1.0 : 0.0; });
}

Series ele(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x <= y ? 1.0 : 0.0; });
}

Series eeq(const Series& a, const Series& b) {
    return binaryOp(a, b, [](double x, double y) { return x == y ? 1.0 : 0.0; });
}

Series ewhere(const Series& cond, const Series& a, const Series& b) {
    const size_t n = std::min({cond.size(), a.size(), b.size()});
    Series out = make(n);
    for (size_t i = 0; i < n; ++i) {
        const bool pickA = !std::isnan(cond[i]) && cond[i] > 0.0;
        out[i] = pickA ? a[i] : b[i];
    }
    return out;
}

// ============================ 时间序列算子 ============================

Series delay(const Series& a, int n) {
    if (n <= 0) return a;
    Series out = make(a.size());
    if (static_cast<size_t>(n) >= a.size()) return out;
    for (size_t i = static_cast<size_t>(n); i < a.size(); ++i) {
        out[i] = a[i - static_cast<size_t>(n)];
    }
    return out;
}

Series delta(const Series& a, int n) { return sub(a, delay(a, n)); }

Series tsSum(const Series& a, int n) {
    if (n <= 0) return make(a.size());
    const WindowPrefix p(a);
    Series out = make(a.size());
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w; i <= a.size(); ++i) {
        if (p.count[i] - p.count[i - w] == n) out[i - 1] = p.sum[i] - p.sum[i - w];
    }
    return out;
}

Series tsMean(const Series& a, int n) {
    if (n <= 0) return make(a.size());
    Series sum = tsSum(a, n);
    return divScalar(sum, static_cast<double>(n));
}

Series tsStd(const Series& a, int n) {
    if (n <= 0) return make(a.size());
    const WindowPrefix p(a);
    Series out = make(a.size());
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w; i <= a.size(); ++i) {
        if (p.count[i] - p.count[i - w] != n) continue;
        const double mean = (p.sum[i] - p.sum[i - w]) / static_cast<double>(n);
        const double meanSq = (p.sumSq[i] - p.sumSq[i - w]) / static_cast<double>(n);
        const double var = meanSq - mean * mean;
        if (var >= 0.0) out[i - 1] = std::sqrt(var);
    }
    return out;
}

Series tsMax(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 0) return out;
    const WindowPrefix p(a);
    std::deque<size_t> dq;  // 单调递减
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isnan(a[i])) {
            while (!dq.empty() && a[dq.back()] <= a[i]) dq.pop_back();
            dq.push_back(i);
        }
        if (!dq.empty() && dq.front() + static_cast<size_t>(n) <= i) dq.pop_front();
        if (i + 1 >= static_cast<size_t>(n) && p.count[i + 1] - p.count[i + 1 - n] == n) {
            out[i] = a[dq.front()];
        }
    }
    return out;
}

Series tsMin(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 0) return out;
    const WindowPrefix p(a);
    std::deque<size_t> dq;  // 单调递增
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isnan(a[i])) {
            while (!dq.empty() && a[dq.back()] >= a[i]) dq.pop_back();
            dq.push_back(i);
        }
        if (!dq.empty() && dq.front() + static_cast<size_t>(n) <= i) dq.pop_front();
        if (i + 1 >= static_cast<size_t>(n) && p.count[i + 1] - p.count[i + 1 - n] == n) {
            out[i] = a[dq.front()];
        }
    }
    return out;
}

Series tsRank(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 1) return out;
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w - 1; i < a.size(); ++i) {
        int valid = 0;
        int less = 0;
        bool windowOk = true;
        const double cur = a[i];
        if (std::isnan(cur)) continue;
        for (size_t j = i + 1 - w; j <= i; ++j) {
            if (std::isnan(a[j])) { windowOk = false; break; }
            ++valid;
            if (a[j] < cur) ++less;
        }
        if (!windowOk || valid < 2) continue;
        out[i] = static_cast<double>(less) / static_cast<double>(valid - 1);
    }
    return out;
}

Series count(const Series& cond, int n) {
    if (n <= 0) return make(cond.size());
    Series ones = make(cond.size());
    for (size_t i = 0; i < cond.size(); ++i) {
        ones[i] = (!std::isnan(cond[i]) && cond[i] > 0.0) ? 1.0 : 0.0;
    }
    return tsSum(ones, n);
}

Series sumIf(const Series& values, const Series& cond, int n) {
    const size_t len = std::min(values.size(), cond.size());
    Series masked = make(len);
    for (size_t i = 0; i < len; ++i) {
        if (!std::isnan(cond[i]) && cond[i] > 0.0) masked[i] = values[i];
        else masked[i] = 0.0;
    }
    return tsSum(masked, n);
}

Series corr(const Series& x, const Series& y, int n) {
    const size_t len = std::min(x.size(), y.size());
    Series out = make(len);
    if (n <= 1) return out;
    std::vector<double> px(len + 1, 0.0), py(len + 1, 0.0);
    std::vector<double> pxx(len + 1, 0.0), pyy(len + 1, 0.0), pxy(len + 1, 0.0);
    std::vector<int> pc(len + 1, 0);
    for (size_t i = 0; i < len; ++i) {
        const bool ok = !std::isnan(x[i]) && !std::isnan(y[i]);
        const double xv = ok ? x[i] : 0.0;
        const double yv = ok ? y[i] : 0.0;
        px[i + 1] = px[i] + xv;
        py[i + 1] = py[i] + yv;
        pxx[i + 1] = pxx[i] + xv * xv;
        pyy[i + 1] = pyy[i] + yv * yv;
        pxy[i + 1] = pxy[i] + xv * yv;
        pc[i + 1] = pc[i] + (ok ? 1 : 0);
    }
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w; i <= len; ++i) {
        if (pc[i] - pc[i - w] != n) continue;
        const double sx = px[i] - px[i - w];
        const double sy = py[i] - py[i - w];
        const double sxx = pxx[i] - pxx[i - w];
        const double syy = pyy[i] - pyy[i - w];
        const double sxy = pxy[i] - pxy[i - w];
        const double cnt = static_cast<double>(n);
        const double varX = sxx / cnt - (sx / cnt) * (sx / cnt);
        const double varY = syy / cnt - (sy / cnt) * (sy / cnt);
        if (varX <= 0.0 || varY <= 0.0) continue;
        const double cov = sxy / cnt - (sx / cnt) * (sy / cnt);
        out[i - 1] = cov / std::sqrt(varX * varY);
    }
    return out;
}

Series covar(const Series& x, const Series& y, int n) {
    const size_t len = std::min(x.size(), y.size());
    Series out = make(len);
    if (n <= 1) return out;
    std::vector<double> px(len + 1, 0.0), py(len + 1, 0.0), pxy(len + 1, 0.0);
    std::vector<int> pc(len + 1, 0);
    for (size_t i = 0; i < len; ++i) {
        const bool ok = !std::isnan(x[i]) && !std::isnan(y[i]);
        const double xv = ok ? x[i] : 0.0;
        const double yv = ok ? y[i] : 0.0;
        px[i + 1] = px[i] + xv;
        py[i + 1] = py[i] + yv;
        pxy[i + 1] = pxy[i] + xv * yv;
        pc[i + 1] = pc[i] + (ok ? 1 : 0);
    }
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w; i <= len; ++i) {
        if (pc[i] - pc[i - w] != n) continue;
        const double sx = px[i] - px[i - w];
        const double sy = py[i] - py[i - w];
        const double sxy = pxy[i] - pxy[i - w];
        const double cnt = static_cast<double>(n);
        out[i - 1] = sxy / cnt - (sx / cnt) * (sy / cnt);
    }
    return out;
}

Series decayLinear(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 0) return out;
    const size_t w = static_cast<size_t>(n);
    double wsum = 0.0;
    for (size_t k = 1; k <= w; ++k) wsum += static_cast<double>(k);
    for (size_t i = w - 1; i < a.size(); ++i) {
        double sum = 0.0;
        bool ok = true;
        for (size_t k = 0; k < w; ++k) {
            const double v = a[i - k];
            if (std::isnan(v)) { ok = false; break; }
            sum += v * static_cast<double>(w - k);  // 越近权重越大
        }
        if (ok) out[i] = sum / wsum;
    }
    return out;
}

Series wma(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 0) return out;
    const size_t w = static_cast<size_t>(n);
    double wsum = 0.0;
    for (size_t k = 1; k <= w; ++k) wsum += static_cast<double>(k);
    for (size_t i = w - 1; i < a.size(); ++i) {
        double sum = 0.0;
        bool ok = true;
        for (size_t k = 0; k < w; ++k) {
            const double v = a[i - k];
            if (std::isnan(v)) { ok = false; break; }
            sum += v * static_cast<double>(w - k);
        }
        if (ok) out[i] = sum / wsum;
    }
    return out;
}

Series smaCn(const Series& a, int n, int m) {
    Series out = make(a.size());
    if (n <= 0 || m <= 0) return out;
    double prev = kNaN;
    for (size_t i = 0; i < a.size(); ++i) {
        const double x = a[i];
        if (std::isnan(x)) continue;
        prev = std::isnan(prev)
            ? x
            : (static_cast<double>(m) * x + static_cast<double>(n - m) * prev) /
                  static_cast<double>(n);
        out[i] = prev;
    }
    return out;
}

Series regBeta(const Series& y, const Series& x, int n) {
    const size_t len = std::min(x.size(), y.size());
    Series out = make(len);
    if (n <= 1) return out;
    std::vector<double> px(len + 1, 0.0), py(len + 1, 0.0);
    std::vector<double> pxx(len + 1, 0.0), pxy(len + 1, 0.0);
    std::vector<int> pc(len + 1, 0);
    for (size_t i = 0; i < len; ++i) {
        const bool ok = !std::isnan(x[i]) && !std::isnan(y[i]);
        const double xv = ok ? x[i] : 0.0;
        const double yv = ok ? y[i] : 0.0;
        px[i + 1] = px[i] + xv;
        py[i + 1] = py[i] + yv;
        pxx[i + 1] = pxx[i] + xv * xv;
        pxy[i + 1] = pxy[i] + xv * yv;
        pc[i + 1] = pc[i] + (ok ? 1 : 0);
    }
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w; i <= len; ++i) {
        if (pc[i] - pc[i - w] != n) continue;
        const double sx = px[i] - px[i - w];
        const double sy = py[i] - py[i - w];
        const double sxx = pxx[i] - pxx[i - w];
        const double sxy = pxy[i] - pxy[i - w];
        const double cnt = static_cast<double>(n);
        const double denom = cnt * sxx - sx * sx;
        if (std::abs(denom) <= 1e-15) continue;
        out[i - 1] = (cnt * sxy - sx * sy) / denom;
    }
    return out;
}

Series regBetaSeq(const Series& y, int n) {
    Series x(y.size(), kNaN);
    for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<double>(i);
    return regBeta(y, x, n);
}

Series lowDay(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 0) return out;
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w - 1; i < a.size(); ++i) {
        bool ok = true;
        size_t extreme = i;
        for (size_t j = i + 1 - w; j <= i; ++j) {
            if (std::isnan(a[j])) { ok = false; break; }
            if (a[j] <= a[extreme]) extreme = j;  // 并列取最近
        }
        if (ok) out[i] = static_cast<double>(i - extreme);
    }
    return out;
}

Series highDay(const Series& a, int n) {
    Series out = make(a.size());
    if (n <= 0) return out;
    const size_t w = static_cast<size_t>(n);
    for (size_t i = w - 1; i < a.size(); ++i) {
        bool ok = true;
        size_t extreme = i;
        for (size_t j = i + 1 - w; j <= i; ++j) {
            if (std::isnan(a[j])) { ok = false; break; }
            if (a[j] >= a[extreme]) extreme = j;  // 并列取最近
        }
        if (ok) out[i] = static_cast<double>(i - extreme);
    }
    return out;
}

// ============================ 取值 ============================

std::optional<double> lastValid(const Series& a) {
    for (auto it = a.rbegin(); it != a.rend(); ++it) {
        if (!std::isnan(*it)) return *it;
    }
    return std::nullopt;
}

double lastValue(const Series& a) {
    return a.empty() ? kNaN : a.back();
}

} // namespace st::factors::math
