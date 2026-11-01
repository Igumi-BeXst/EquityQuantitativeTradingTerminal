#include "engine/screener/stock_screener.h"
#include "engine/screener/ranker.h"
#include "engine/screener/panel.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace st {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// 中位数（入参按值拷贝，可被 nth_element 重排）
double medianOf(std::vector<double> values) {
    if (values.empty()) return kNaN;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<long>(mid), values.end());
    if (values.size() % 2 == 1) return values[mid];
    const double hi = values[mid];
    std::nth_element(values.begin(), values.begin() + static_cast<long>(mid - 1), values.end());
    return (values[mid - 1] + hi) / 2.0;
}

/// MAD 去极值：截断到 中位数 ± 5 × 1.4826 × MAD（MAD=0 时不动）
void winsorizeMad(std::vector<double>& values) {
    if (values.size() < 3) return;
    const double med = medianOf(values);
    std::vector<double> deviations(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        deviations[i] = std::abs(values[i] - med);
    }
    const double mad = medianOf(std::move(deviations));
    if (!(mad > 0.0)) return;
    const double limit = 5.0 * 1.4826 * mad;
    for (double& v : values) v = std::clamp(v, med - limit, med + limit);
}

/// 标准正态分布函数 Φ(z)：z-score → 0~1 百分位
double normalCdf(double z) { return 0.5 * (1.0 + std::erf(z / std::sqrt(2.0))); }

}  // namespace

StockScreener::StockScreener() = default;
StockScreener::~StockScreener() = default;

void StockScreener::setConfig(const ScreenerConfig& config) {
    config_ = config;
}

void StockScreener::addFactor(std::shared_ptr<IFactor> factor, double weight) {
    factors_.emplace_back(std::move(factor), weight);
    weights_.emplace_back(factors_.back().first->name(), weight);
}

void StockScreener::addCondition(const Condition& condition) {
    filter_.addCondition(condition);
}

ScreenResult StockScreener::evaluate(const StockCode& code,
                                     const std::vector<Bar>& bars,
                                     const QuoteFundamentals* quote) {
    ScreenResult result;
    result.code = code;

    BarSeries series(bars);
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    ctx.quote = quote;

    // 逐股时间序列因子只计算原始值；面板因子留给 run() 的全池阶段
    for (const auto& [factor, weight] : factors_) {
        (void)weight;
        FactorResult fr;
        fr.name = factor->name();
        if (!factor->isPanel()) {
            fr.rawValue = factor->calculate(ctx);
        }
        fr.score = 0.0;
        result.factorResults.push_back(std::move(fr));
    }
    return result;
}

std::vector<ScreenResult> StockScreener::run(const std::vector<StockCode>& pool) {
    if (!cache_) {
        LogManager::instance()->log(LogLevel::Warn, "StockScreener: 未设置数据源");
        return {};
    }
    if (factors_.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "StockScreener: 未设置因子");
        return {};
    }
    if (pool.empty()) return {};

    // ① 逐股过滤日线（为面板构建与逐股因子共用，只读一次缓存）
    const auto start = config_.lookbackDays > 0
        ? utils::addTradingDays(config_.endDate, -config_.lookbackDays)
        : DateTime{};
    const size_t n = pool.size();
    std::vector<std::vector<Bar>> filtered(n);
    for (size_t i = 0; i < n; ++i) {
        auto bars = cache_->getBars(pool[i], config_.period);
        auto& out = filtered[i];
        out.reserve(bars.size());
        for (auto& bar : bars) {
            if (config_.lookbackDays <= 0 ||
                (bar.time >= start && bar.time <= config_.endDate)) {
                out.push_back(std::move(bar));
            }
        }
    }

    // ② 面板因子需要全池 PanelData（基准指数/风格因子）
    bool hasPanel = false;
    for (const auto& [factor, weight] : factors_) {
        (void)weight;
        if (factor->isPanel()) { hasPanel = true; break; }
    }
    PanelData panel;
    if (hasPanel) {
        panel = buildPanel(pool, filtered, quotes_, benchmarkBars_);
        if (!panel.valid) {
            LogManager::instance()->log(LogLevel::Warn,
                "StockScreener: 面板数据构建为空（{} 只股票），面板因子降级", n);
        }
    }

    std::vector<ScreenResult> results;
    results.reserve(n);

    // ③ 逐股时间序列因子（0~50%）
    for (size_t i = 0; i < n; ++i) {
        const QuoteFundamentals* quote = nullptr;
        auto it = quotes_.find(pool[i].fullCode());
        if (it != quotes_.end()) quote = &it->second;
        results.push_back(evaluate(pool[i], filtered[i], quote));
        if (progressCb_) {
            progressCb_(static_cast<double>(i + 1) / static_cast<double>(n) * 50.0);
        }
    }

    // ④ 面板因子：全池一次计算，按股票对齐写入原始值（50~60%）
    const size_t factorCount = factors_.size();
    for (size_t fi = 0; fi < factorCount; ++fi) {
        const auto& factor = factors_[fi].first;
        if (!factor->isPanel()) continue;
        std::vector<double> values;
        if (panel.valid) values = factor->calculatePanel(panel);
        for (size_t ri = 0; ri < n; ++ri) {
            if (ri < values.size() && std::isfinite(values[ri])) {
                results[ri].factorResults[fi].rawValue = values[ri];
            } else {
                results[ri].factorResults[fi].rawValue.reset();
            }
        }
        if (progressCb_) {
            progressCb_(60.0);
        }
    }

    // ⑤ 逐因子全池截面标准化（60~95%）：MAD 去极值 → z-score → 方向 → Φ(z)×100
    for (size_t fi = 0; fi < factorCount; ++fi) {
        std::vector<double> values;
        values.reserve(n);
        std::vector<size_t> rows;
        rows.reserve(n);
        for (size_t ri = 0; ri < n; ++ri) {
            const auto& raw = results[ri].factorResults[fi].rawValue;
            if (raw.has_value() && std::isfinite(*raw)) {
                values.push_back(*raw);
                rows.push_back(ri);
            }
        }

        double mean = 0.0;
        double stdDev = 0.0;
        std::vector<double> winsorizedByRow(n, kNaN);  // 去极值后按结果行对齐
        if (!values.empty()) {
            winsorizeMad(values);
            for (size_t k = 0; k < values.size(); ++k) {
                winsorizedByRow[rows[k]] = values[k];
            }
            mean = std::accumulate(values.begin(), values.end(), 0.0) /
                   static_cast<double>(values.size());
            double sqSum = 0.0;
            for (double v : values) sqSum += (v - mean) * (v - mean);
            stdDev = std::sqrt(sqSum / static_cast<double>(values.size()));
        }

        const bool direct = factors_[fi].first->higherIsBetter();
        for (size_t ri = 0; ri < n; ++ri) {
            auto& score = results[ri].factorResults[fi].score;
            const auto& raw = results[ri].factorResults[fi].rawValue;
            if (!raw.has_value() || !std::isfinite(*raw) || values.empty() ||
                !std::isfinite(winsorizedByRow[ri])) {
                score = 50.0;  // 缺失 → 中性
                continue;
            }
            if (!(stdDev > 1e-12)) {
                score = 50.0;  // 全池无区分度
                continue;
            }
            // 注意：z-score 必须使用去极值后的值；若用原始值，极端离群点仍会
            // 在 σ 已被 winsorize 缩小后产生巨大 z，使该股拿满 100 分。
            const double z = (winsorizedByRow[ri] - mean) / stdDev;
            const double signedZ = direct ? z : -z;
            score = std::clamp(normalCdf(signedZ) * 100.0, 0.0, 100.0);
        }
        if (progressCb_) {
            progressCb_(60.0 + static_cast<double>(fi + 1) /
                                   static_cast<double>(factorCount) * 35.0);
        }
    }

    // ⑥ 条件筛选 + 加权总分 + 排名
    std::vector<ScreenResult> passed;
    passed.reserve(results.size());
    for (auto& result : results) {
        if (!filter_.passes(result.factorResults)) continue;
        result.totalScore = Ranker::computeTotalScore(result.factorResults, weights_);
        passed.push_back(std::move(result));
    }

    auto sorted = Ranker::topN(std::move(passed), config_.topN);
    if (progressCb_) progressCb_(100.0);

    LogManager::instance()->log(LogLevel::Info,
        "StockScreener: {} 只股票中筛选出 {} 只（{} 个因子，面板={}，截面标准化）",
        pool.size(), sorted.size(), factorCount, hasPanel ? "是" : "否");
    return sorted;
}

} // namespace st
