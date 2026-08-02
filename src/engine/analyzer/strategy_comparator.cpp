#include "engine/analyzer/strategy_comparator.h"
#include "engine/optimizer/grid_search.h"
#include <algorithm>
#include <utility>

namespace st {

void StrategyComparator::setProgressCallback(std::function<void(double)> cb) {
    progressCb_ = std::move(cb);
}

std::vector<ComparisonItemResult> StrategyComparator::run(const ComparisonConfig& cfg) {
    std::vector<ComparisonItemResult> results;
    if (!cfg.cache || cfg.symbols.empty() || cfg.items.empty()) return results;

    const int total = static_cast<int>(cfg.items.size());
    results.reserve(static_cast<size_t>(total));

    for (int i = 0; i < total; ++i) {
        const auto& item = cfg.items[static_cast<size_t>(i)];
        ComparisonItemResult r;
        r.item = item;

        auto strategy = GridSearchOptimizer::makeStrategy(item.strategyId, item.params);
        if (strategy) {
            BacktestConfig bcfg;
            bcfg.symbols = cfg.symbols;
            bcfg.startDate = cfg.startDate;
            bcfg.endDate = cfg.endDate;
            bcfg.initialCapital = cfg.initialCapital;
            bcfg.period = cfg.period;
            bcfg.feeConfig = cfg.feeConfig;

            BacktestEngine engine;
            engine.setConfig(bcfg);
            engine.setDataCache(cfg.cache);
            engine.addStrategy(std::move(strategy));
            auto result = engine.run();

            r.success = result.success;
            r.error = result.error;
            r.performance = result.performance;
            r.equityCurve = result.performance.equityCurve;
            r.trades = result.trades;
        }

        if (progressCb_) {
            progressCb_(static_cast<double>(i + 1) / static_cast<double>(total) * 100.0);
        }
        results.push_back(std::move(r));
    }

    // 按总收益降序
    std::sort(results.begin(), results.end(),
              [](const ComparisonItemResult& a, const ComparisonItemResult& b) {
                  return a.performance.totalReturn > b.performance.totalReturn;
              });
    return results;
}

} // namespace st
