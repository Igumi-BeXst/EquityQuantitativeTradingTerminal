#include "engine/screener/stock_screener.h"
#include "engine/screener/ranker.h"
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

ScreenResult StockScreener::evaluate(const StockCode& code) {
    ScreenResult result;
    result.code = code;

    // 从缓存加载日线
    auto start = config_.lookbackDays > 0
        ? utils::addTradingDays(config_.endDate, -config_.lookbackDays)
        : DateTime{};
    auto bars = cache_ ? cache_->getBars(code, config_.period) : std::vector<Bar>{};

    // 过滤时间范围
    std::vector<Bar> filtered;
    for (auto& bar : bars) {
        if (config_.lookbackDays <= 0 ||
            (bar.time >= start && bar.time <= config_.endDate)) {
            filtered.push_back(bar);
        }
    }
    BarSeries series(std::move(filtered));

    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    // 基本面快照（估值/财务因子用；缺失时因子内部降级为 nullopt）
    auto it = quotes_.find(code.fullCode());
    if (it != quotes_.end()) {
        ctx.quote = &it->second;
    }

    // 只计算原始值；分数留给 run() 的截面标准化阶段
    for (const auto& [factor, weight] : factors_) {
        (void)weight;
        FactorResult fr;
        fr.name = factor->name();
        fr.rawValue = factor->calculate(ctx);
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

    std::vector<ScreenResult> results;
    results.reserve(pool.size());

    // ① 逐只计算原始因子值（0~60%）
    for (size_t i = 0; i < pool.size(); ++i) {
        results.push_back(evaluate(pool[i]));
        if (progressCb_) {
            progressCb_(static_cast<double>(i + 1) / static_cast<double>(pool.size()) * 60.0);
        }
    }

    // ② 逐因子全池截面标准化（60~95%）：MAD 去极值 → z-score → 方向 → Φ(z)×100
    const size_t factorCount = factors_.size();
    const size_t resultCount = results.size();
    for (size_t fi = 0; fi < factorCount; ++fi) {
        std::vector<double> values;
        values.reserve(resultCount);
        std::vector<size_t> rows;
        rows.reserve(resultCount);
        for (size_t ri = 0; ri < resultCount; ++ri) {
            const auto& raw = results[ri].factorResults[fi].rawValue;
            if (raw.has_value() && std::isfinite(*raw)) {
                values.push_back(*raw);
                rows.push_back(ri);
            }
        }

        double mean = 0.0;
        double stdDev = 0.0;
        if (!values.empty()) {
            winsorizeMad(values);
            mean = std::accumulate(values.begin(), values.end(), 0.0) /
                   static_cast<double>(values.size());
            double sqSum = 0.0;
            for (double v : values) sqSum += (v - mean) * (v - mean);
            stdDev = std::sqrt(sqSum / static_cast<double>(values.size()));
        }

        const bool direct = factors_[fi].first->higherIsBetter();
        for (size_t ri = 0; ri < resultCount; ++ri) {
            auto& score = results[ri].factorResults[fi].score;
            const auto& raw = results[ri].factorResults[fi].rawValue;
            if (!raw.has_value() || !std::isfinite(*raw) || values.empty()) {
                score = 50.0;  // 缺失 → 中性
                continue;
            }
            if (!(stdDev > 1e-12)) {
                score = 50.0;  // 全池无区分度
                continue;
            }
            const double z = (*raw - mean) / stdDev;
            const double signedZ = direct ? z : -z;
            score = std::clamp(normalCdf(signedZ) * 100.0, 0.0, 100.0);
        }
        if (progressCb_) {
            progressCb_(60.0 + static_cast<double>(fi + 1) /
                                   static_cast<double>(factorCount) * 35.0);
        }
    }

    // ③ 条件筛选（作用于标准化分数）+ 加权总分
    std::vector<ScreenResult> passed;
    passed.reserve(results.size());
    for (auto& result : results) {
        if (!filter_.passes(result.factorResults)) continue;
        result.totalScore = Ranker::computeTotalScore(result.factorResults, weights_);
        passed.push_back(std::move(result));
    }

    // ④ 排名
    auto sorted = Ranker::topN(std::move(passed), config_.topN);
    if (progressCb_) progressCb_(100.0);

    LogManager::instance()->log(LogLevel::Info,
        "StockScreener: {} 只股票中筛选出 {} 只（截面标准化评分）",
        pool.size(), sorted.size());
    return sorted;
}

} // namespace st
