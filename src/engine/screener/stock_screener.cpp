#include "engine/screener/stock_screener.h"
#include "engine/screener/ranker.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"

namespace st {

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

    // 计算各因子
    for (const auto& [factor, weight] : factors_) {
        FactorResult fr;
        fr.name = factor->name();
        fr.rawValue = factor->calculate(ctx);
        fr.score = factor->toScore(fr.rawValue);
        result.factorResults.push_back(std::move(fr));
    }

    result.totalScore = Ranker::computeTotalScore(result.factorResults, weights_);
    return result;
}

std::vector<ScreenResult> StockScreener::run(const std::vector<StockCode>& pool) {
    if (!cache_) {
        LogManager::instance()->log(LogLevel::Warn, "StockScreener: 未设置数据源");
        return {};
    }

    std::vector<ScreenResult> results;
    results.reserve(pool.size());

    int processed = 0;
    for (const auto& code : pool) {
        auto result = evaluate(code);

        // 条件筛选
        if (filter_.passes(result.factorResults)) {
            results.push_back(std::move(result));
        }

        processed++;
        if (progressCb_) {
            progressCb_(static_cast<double>(processed) / pool.size() * 100.0);
        }
    }

    // 排名
    auto sorted = Ranker::topN(std::move(results), config_.topN);

    LogManager::instance()->log(LogLevel::Info,
        "StockScreener: {} 只股票中筛选出 {} 只", pool.size(), sorted.size());
    return sorted;
}

} // namespace st
