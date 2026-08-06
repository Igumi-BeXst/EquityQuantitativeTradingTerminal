#include "engine/analyzer/chip_distribution.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace st {

namespace {

/// 把 x 夹到 [lo, hi]
inline int clampTo(int x, int lo, int hi) {
    return std::max(lo, std::min(hi, x));
}

}  // namespace

ChipDistResult ChipDistribution::compute(const std::vector<Bar>& bars, double floatShares) {
    ChipDistResult result;
    if (bars.empty()) {
        result.error = "empty bars";
        return result;
    }

    // 1. 过滤合法 bar，确定价格范围
    std::vector<Bar> valid;
    valid.reserve(bars.size());
    double minLow = std::numeric_limits<double>::max();
    double maxHigh = 0.0;
    for (const auto& b : bars) {
        if (!b.isValid() || b.volume <= 0) continue;
        valid.push_back(b);
        minLow = std::min(minLow, b.low);
        maxHigh = std::max(maxHigh, b.high);
    }
    if (valid.empty()) {
        result.error = "no valid bars";
        return result;
    }

    // 2. 价位桶（价格区间加 1% 内边距）
    const int k = kDefaultBins;
    double lo = minLow - (maxHigh - minLow) * 0.01;
    double hi = maxHigh + (maxHigh - minLow) * 0.01;
    if (hi - lo < 1e-9) hi = lo + 1.0;  // 单价格保护
    const double span = hi - lo;
    std::vector<double> binCenter(k);
    for (int i = 0; i < k; ++i) binCenter[i] = lo + span * (i + 0.5) / k;
    std::vector<double> chips(k, 0.0);

    const bool decay = floatShares > 0.0;

    // 3. 逐日：换手率衰减 + 三角分布累加
    for (const auto& b : valid) {
        const double turnover =
            decay ? std::min(1.0, std::max(0.0, static_cast<double>(b.volume) / floatShares)) : 0.0;
        if (decay && turnover > 0.0) {
            const double remain = 1.0 - turnover;
            for (auto& c : chips) c *= remain;
        }

        const double typical = b.typical();  // (high+low+close)/3
        const double halfW = std::max(1e-9, (b.high - b.low) / 2.0);
        const int i0 = clampTo(static_cast<int>((b.low - lo) / span * k), 0, k - 1);
        const int i1 = clampTo(static_cast<int>((b.high - lo) / span * k), 0, k - 1);

        double sumW = 0.0;
        std::vector<double> w(k, 0.0);
        for (int i = i0; i <= i1; ++i) {
            if (binCenter[i] < b.low || binCenter[i] > b.high) continue;
            const double dist = std::abs(binCenter[i] - typical);
            const double wi = dist >= halfW ? 0.0 : 1.0 - dist / halfW;
            w[i] = wi;
            sumW += wi;
        }
        if (sumW <= 0.0) {
            // 防御：typical 恰在区间外（数据异常），全部归入最接近 typical 的桶
            int best = i0;
            double bestD = std::numeric_limits<double>::max();
            for (int i = i0; i <= i1; ++i) {
                const double d = std::abs(binCenter[i] - typical);
                if (d < bestD) { bestD = d; best = i; }
            }
            chips[best] += static_cast<double>(b.volume);
            continue;
        }

        const double volShare = static_cast<double>(b.volume) / sumW;
        for (int i = i0; i <= i1; ++i) {
            if (w[i] > 0.0) chips[i] += w[i] * volShare;
        }
    }

    // 4. 组装非零桶（桶索引升序即价格升序）
    double total = 0.0;
    for (int i = 0; i < k; ++i) {
        if (chips[i] <= 0.0) continue;
        result.points.push_back({binCenter[i], chips[i]});
        total += chips[i];
    }
    if (result.points.empty()) {
        result.error = "no chips";
        return result;
    }
    if (total <= 0.0) {
        result.error = "zero total chips";
        return result;
    }

    // 衰减模式把总量归一化为流通股本；纯量模式保持原始量（UI 相对显示）
    const double scale = decay ? floatShares / total : 1.0;
    if (scale != 1.0) {
        for (auto& p : result.points) p.shares *= scale;
        total *= scale;
    }
    result.totalChips = total;
    result.minPrice = result.points.front().price;
    result.maxPrice = result.points.back().price;

    // 平均成本
    double costSum = 0.0;
    for (const auto& p : result.points) costSum += p.price * p.shares;
    result.avgCost = costSum / total;

    // 获利盘（现价 = 最后一根合法 bar 收盘）
    const double currentPrice = valid.back().close;
    double below = 0.0;
    for (const auto& p : result.points) {
        if (p.price <= currentPrice) below += p.shares;
    }
    result.profitRatio = below / total;

    // 成本区间（筹码加权分位 P5/P15/P85/P95）+ 集中度
    double cdf = 0.0;
    double p5 = result.points.front().price;
    double p15 = result.points.front().price;
    double p85 = result.points.back().price;
    double p95 = result.points.back().price;
    bool gotP5 = false, gotP15 = false, gotP85 = false;
    for (const auto& p : result.points) {
        cdf += p.shares;
        const double frac = cdf / total;
        if (!gotP5 && frac >= 0.05) { p5 = p.price; gotP5 = true; }
        if (!gotP15 && frac >= 0.15) { p15 = p.price; gotP15 = true; }
        if (!gotP85 && frac >= 0.85) { p85 = p.price; gotP85 = true; }
        if (frac >= 0.95) { p95 = p.price; break; }
    }
    result.costLow = p5;
    result.costHigh = p95;
    result.costLow70 = p15;
    result.costHigh70 = p85;
    const double denom = p95 + p5;
    result.concentration = denom > 1e-9 ? (p95 - p5) / denom : 0.0;

    result.success = true;
    return result;
}

TransactionDist TransactionDistribution::fromTicks(const std::vector<Tick>& ticks, int bins) {
    TransactionDist out;
    if (ticks.empty() || bins <= 0) return out;

    double minP = std::numeric_limits<double>::max();
    double maxP = 0.0;
    for (const auto& t : ticks) {
        if (!t.isValid()) continue;
        minP = std::min(minP, t.price);
        maxP = std::max(maxP, t.price);
    }
    if (minP > maxP) return out;  // 全无效

    double lo = minP, hi = maxP;
    if (hi - lo < 1e-9) hi = lo + 1.0;
    const double span = hi - lo;

    std::vector<double> counts(bins, 0.0);
    Volume total = 0;
    for (const auto& t : ticks) {
        if (!t.isValid()) continue;
        const int idx = clampTo(static_cast<int>((t.price - lo) / span * bins), 0, bins - 1);
        counts[idx] += static_cast<double>(t.volume);
        total += t.volume;
    }

    for (int i = 0; i < bins; ++i) {
        if (counts[i] <= 0.0) continue;
        out.points.push_back({lo + span * (i + 0.5) / bins,
                              static_cast<Volume>(counts[i])});
    }
    out.totalVolume = total;
    out.success = !out.points.empty();
    return out;
}

TransactionDist TransactionDistribution::fromIntraday(const IntradayData& data, int bins) {
    if (data.points.empty() || bins <= 0) return {};

    // 分时 points.volume 为累计量 → 差分得每分成交量，再走逐笔分桶
    std::vector<Tick> ticks;
    ticks.reserve(data.points.size());
    Volume prev = 0;
    for (const auto& pt : data.points) {
        const Volume v = pt.volume - prev;
        prev = pt.volume;
        if (pt.price <= 0 || v <= 0) continue;
        Tick t;
        t.price = pt.price;
        t.volume = v;
        ticks.push_back(t);
    }
    return fromTicks(ticks, bins);
}

RangeStatResult RangeStats::compute(const std::vector<Bar>& bars, double floatShares) {
    RangeStatResult r;
    std::vector<Bar> valid;
    valid.reserve(bars.size());
    for (const auto& b : bars) {
        if (b.isValid()) valid.push_back(b);
    }
    if (valid.empty()) {
        r.error = "no valid bars";
        return r;
    }

    r.barCount = static_cast<int>(valid.size());
    r.startPrice = valid.front().open;
    r.endPrice = valid.back().close;
    if (r.startPrice > 0.0) {
        r.changePct = (r.endPrice - r.startPrice) / r.startPrice * 100.0;
    }

    r.high = valid.front().high;
    r.low = valid.front().low;
    double vol = 0.0, amt = 0.0;
    for (const auto& b : valid) {
        r.high = std::max(r.high, b.high);
        r.low = std::min(r.low, b.low);
        vol += static_cast<double>(b.volume);
        amt += b.amount;
    }
    if (r.startPrice > 0.0) r.amplitudePct = (r.high - r.low) / r.startPrice * 100.0;
    r.totalVolume = vol;
    r.totalAmount = amt;
    r.avgPrice = vol > 0.0 ? amt / vol : 0.0;
    r.turnoverPct = (floatShares > 0.0 && vol > 0.0) ? vol / floatShares * 100.0 : 0.0;
    r.success = true;
    return r;
}

} // namespace st
