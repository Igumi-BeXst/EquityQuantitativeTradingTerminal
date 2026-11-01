#include "engine/screener/panel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace st {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double meanOfRow(const std::vector<double>& matrix, size_t row, size_t cols,
                 const std::vector<size_t>& idx) {
    if (idx.empty()) return kNaN;
    double sum = 0.0;
    size_t n = 0;
    for (size_t i : idx) {
        const double v = matrix[row * cols + i];
        if (std::isfinite(v)) {
            sum += v;
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : kNaN;
}

}  // namespace

PanelData buildPanel(const std::vector<StockCode>& stocks,
                     const std::vector<std::vector<Bar>>& barsByStock,
                     const std::unordered_map<std::string, QuoteFundamentals>& quotes,
                     const std::vector<Bar>& benchmarkBars) {
    PanelData panel;
    panel.stocks = stocks;
    const size_t n = stocks.size();
    if (n == 0 || barsByStock.size() < n) return panel;

    // ① 全池日期并集（升序去重）
    std::vector<DateTime> dates;
    for (size_t i = 0; i < n; ++i) {
        for (const auto& bar : barsByStock[i]) {
            if (bar.time != DateTime{}) dates.push_back(bar.time);
        }
    }
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
    if (dates.empty()) return panel;
    const size_t t = dates.size();

    std::map<DateTime, size_t> dateIndex;
    for (size_t k = 0; k < t; ++k) dateIndex[dates[k]] = k;

    const size_t cells = t * n;
    auto fill = [cells](std::vector<double>& v) { v.assign(cells, kNaN); };
    fill(panel.open);
    fill(panel.high);
    fill(panel.low);
    fill(panel.close);
    fill(panel.volume);
    fill(panel.amount);
    fill(panel.vwap);
    fill(panel.ret);
    fill(panel.marketCap);
    panel.dates = dates;

    // ② 逐股票对齐 OHLCV / VWAP
    std::vector<double> shares(n, kNaN);
    std::vector<double> bp(n, kNaN);  // 账面市值比 B/P（当前快照 PB 的倒数）
    for (size_t i = 0; i < n; ++i) {
        const auto& bars = barsByStock[i];
        double lastClose = kNaN;
        for (const auto& bar : bars) {
            auto it = dateIndex.find(bar.time);
            if (it == dateIndex.end()) continue;
            const size_t row = it->second * n + i;
            panel.open[row] = bar.open > 0.0 ? bar.open : kNaN;
            panel.high[row] = bar.high > 0.0 ? bar.high : kNaN;
            panel.low[row] = bar.low > 0.0 ? bar.low : kNaN;
            panel.close[row] = bar.close > 0.0 ? bar.close : kNaN;
            panel.volume[row] = static_cast<double>(bar.volume);
            panel.amount[row] = bar.amount;
            if (bar.amount > 0.0 && bar.volume > 0) {
                panel.vwap[row] = bar.amount / static_cast<double>(bar.volume);
            } else if (bar.high > 0.0 && bar.low > 0.0 && bar.close > 0.0) {
                panel.vwap[row] = (bar.high + bar.low + bar.close) / 3.0;
            }
            if (bar.close > 0.0) lastClose = bar.close;
        }

        auto qit = quotes.find(stocks[i].fullCode());
        if (qit != quotes.end() && qit->second.valid) {
            const auto& q = qit->second;
            if (q.totalShares > 0.0) {
                shares[i] = q.totalShares;
            } else if (q.marketCap > 0.0 && std::isfinite(lastClose) && lastClose > 0.0) {
                shares[i] = q.marketCap / lastClose;  // 反推股本
            }
            if (q.pb > 0.0) bp[i] = 1.0 / q.pb;
        }
    }

    // ③ 收益率 / 市值（逐股票跨行）
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k < t; ++k) {
            const size_t row = k * n + i;
            if (k > 0) {
                const double c = panel.close[row];
                const double prev = panel.close[(k - 1) * n + i];
                if (std::isfinite(c) && std::isfinite(prev) && prev > 0.0) {
                    panel.ret[row] = c / prev - 1.0;
                }
            }
            if (std::isfinite(shares[i]) && std::isfinite(panel.close[row])) {
                panel.marketCap[row] = shares[i] * panel.close[row];
            }
        }
    }

    // ④ 基准指数对齐
    panel.benchOpen.assign(t, kNaN);
    panel.benchClose.assign(t, kNaN);
    panel.benchRet.assign(t, kNaN);
    if (!benchmarkBars.empty()) {
        std::map<DateTime, const Bar*> benchByDate;
        for (const auto& bar : benchmarkBars) benchByDate[bar.time] = &bar;
        for (size_t k = 0; k < t; ++k) {
            auto it = benchByDate.find(dates[k]);
            if (it == benchByDate.end()) continue;
            const Bar* bar = it->second;
            if (bar->open > 0.0) panel.benchOpen[k] = bar->open;
            if (bar->close > 0.0) panel.benchClose[k] = bar->close;
            if (k > 0 && std::isfinite(panel.benchClose[k]) &&
                std::isfinite(panel.benchClose[k - 1]) && panel.benchClose[k - 1] > 0.0) {
                panel.benchRet[k] = panel.benchClose[k] / panel.benchClose[k - 1] - 1.0;
            }
        }
    }

    // ⑤ 风格因子（逐日截面）
    panel.mkt.assign(t, kNaN);
    panel.smb.assign(t, kNaN);
    panel.hml.assign(t, kNaN);
    for (size_t k = 0; k < t; ++k) {
        std::vector<size_t> retValid;
        retValid.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const double r = panel.ret[k * n + i];
            if (std::isfinite(r)) retValid.push_back(i);
        }
        panel.mkt[k] = meanOfRow(panel.ret, k, n, retValid);

        // SMB：按市值排序，小组收益 - 大组收益（市值优先用前一日，缺失用当日）
        std::vector<std::pair<double, size_t>> capIdx;
        for (size_t i : retValid) {
            double cap = k > 0 ? panel.marketCap[(k - 1) * n + i] : kNaN;
            if (!std::isfinite(cap)) cap = panel.marketCap[k * n + i];
            if (std::isfinite(cap) && cap > 0.0) capIdx.emplace_back(cap, i);
        }
        if (capIdx.size() >= 4) {
            std::sort(capIdx.begin(), capIdx.end(),
                      [](const std::pair<double, size_t>& a,
                         const std::pair<double, size_t>& b) { return a.first < b.first; });
            const size_t half = capIdx.size() / 2;
            std::vector<size_t> small;
            std::vector<size_t> big;
            small.reserve(half);
            big.reserve(half);
            for (size_t m = 0; m < half; ++m) small.push_back(capIdx[m].second);
            for (size_t m = capIdx.size() - half; m < capIdx.size(); ++m) {
                big.push_back(capIdx[m].second);
            }
            const double smallMean = meanOfRow(panel.ret, k, n, small);
            const double bigMean = meanOfRow(panel.ret, k, n, big);
            if (std::isfinite(smallMean) && std::isfinite(bigMean)) {
                panel.smb[k] = smallMean - bigMean;
            }
        }

        // HML：按 B/P 排序，高 B/P 组收益 - 低 B/P 组收益
        std::vector<size_t> bpIdx;
        for (size_t i : retValid) {
            if (std::isfinite(bp[i]) && bp[i] > 0.0) bpIdx.push_back(i);
        }
        if (bpIdx.size() >= 4) {
            std::sort(bpIdx.begin(), bpIdx.end(),
                      [&](size_t a, size_t b) { return bp[a] < bp[b]; });
            const size_t half = bpIdx.size() / 2;
            std::vector<size_t> lowBp(bpIdx.begin(), bpIdx.begin() + static_cast<long>(half));
            std::vector<size_t> highBp(bpIdx.end() - static_cast<long>(half), bpIdx.end());
            const double highMean = meanOfRow(panel.ret, k, n, highBp);
            const double lowMean = meanOfRow(panel.ret, k, n, lowBp);
            if (std::isfinite(highMean) && std::isfinite(lowMean)) {
                panel.hml[k] = highMean - lowMean;
            }
        }
    }

    panel.valid = !panel.close.empty();
    return panel;
}

} // namespace st
