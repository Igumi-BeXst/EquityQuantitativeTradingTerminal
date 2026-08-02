#include "engine/optimizer/grid_search.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "engine/strategy/templates/turtle_strategy.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <future>

namespace st {

void GridSearchOptimizer::setProgressCallback(std::function<void(double)> cb) {
    progressCb_ = std::move(cb);
}

std::vector<std::vector<std::pair<std::string, int>>>
GridSearchOptimizer::generateCombinations(const GridSearchConfig& cfg) {
    std::vector<std::vector<std::pair<std::string, int>>> result;
    std::vector<std::pair<std::string, int>> current;

    std::function<void(size_t)> dfs = [&](size_t idx) {
        if (idx >= cfg.ranges.size()) {
            result.push_back(current);
            return;
        }
        const auto& r = cfg.ranges[idx];
        const int step = std::max(1, r.step);
        for (int v = r.from; v <= r.to; v += step) {
            current.emplace_back(r.name, v);
            dfs(idx + 1);
            current.pop_back();
        }
    };
    dfs(0);
    return result;
}

std::shared_ptr<IStrategy> GridSearchOptimizer::makeStrategy(
    const std::string& id,
    const std::vector<std::pair<std::string, int>>& params) {
    const auto getParam = [&](const char* name, int def) {
        for (const auto& [key, val] : params) {
            if (key == name) return val;
        }
        return def;
    };

    if (id == "MACross") {
        auto s = std::make_shared<MACrossStrategy>();
        s->fastPeriod_ = getParam("fastPeriod", 5);
        s->slowPeriod_ = getParam("slowPeriod", 20);
        return s;
    }
    if (id == "Turtle") {
        auto s = std::make_shared<TurtleStrategy>();
        s->entryPeriod_ = getParam("entryPeriod", 20);
        s->exitPeriod_ = getParam("exitPeriod", 10);
        return s;
    }
    return nullptr;
}

double GridSearchOptimizer::objectiveValue(const Performance& p, Objective o) {
    switch (o) {
        case Objective::TotalReturn: return p.totalReturn;
        case Objective::SharpeRatio: return p.sharpeRatio;
        case Objective::MaxDrawdown: return p.maxDrawdown;
        case Objective::CalmarRatio: return p.calmarRatio;
        case Objective::ProfitFactor: return p.profitFactor;
    }
    return 0.0;
}

bool GridSearchOptimizer::objectiveMinimized(Objective o) {
    return o == Objective::MaxDrawdown;
}

GridSearchResult GridSearchOptimizer::evaluateOne(
    const GridSearchConfig& cfg,
    const std::vector<std::pair<std::string, int>>& params) const {
    GridSearchResult r;
    r.params = params;

    auto strategy = makeStrategy(cfg.strategyId, params);
    if (!strategy) return r;

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
    r.performance = result.performance;
    r.equityCurve = result.performance.equityCurve;
    r.objectiveValue = result.success
        ? objectiveValue(result.performance, cfg.objective) : 0.0;
    return r;
}

std::vector<GridSearchResult> GridSearchOptimizer::run(const GridSearchConfig& cfg) {
    std::vector<GridSearchResult> results;
    if (!cfg.cache || cfg.symbols.empty()) return results;

    auto combos = generateCombinations(cfg);
    if (combos.empty()) return results;
    results.resize(combos.size());

    const int total = static_cast<int>(combos.size());
    const int lanes = std::max(1, std::min(cfg.parallelLanes, total));
    auto progressCb = progressCb_;
    std::atomic<size_t> next{0};
    std::atomic<int> done{0};

    auto worker = [&]() {
        while (true) {
            const size_t i = next.fetch_add(1);
            if (i >= combos.size()) break;
            results[i] = evaluateOne(cfg, combos[i]);
            const int d = done.fetch_add(1) + 1;
            if (progressCb) {
                progressCb(static_cast<double>(d) / static_cast<double>(total) * 100.0);
            }
        }
    };

    std::vector<std::future<void>> futures;
    for (int l = 1; l < lanes; ++l) {
        futures.push_back(std::async(std::launch::async, worker));
    }
    worker();  // leader 线程参与干活，避免池线程递归等待死锁
    for (auto& f : futures) f.get();

    // 按目标排序：MaxDrawdown 升序（越小越好），其余降序
    const bool minimized = objectiveMinimized(cfg.objective);
    std::sort(results.begin(), results.end(),
              [minimized](const GridSearchResult& a, const GridSearchResult& b) {
                  if (minimized) return a.objectiveValue < b.objectiveValue;
                  return a.objectiveValue > b.objectiveValue;
              });
    return results;
}

} // namespace st
