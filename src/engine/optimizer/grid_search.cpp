#include "engine/optimizer/grid_search.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "engine/strategy/templates/turtle_strategy.h"
#include "engine/strategy/templates/momentum_strategy.h"
#include "engine/strategy/templates/breakout_strategy.h"
#include "engine/strategy/templates/mean_reversion_strategy.h"
#include "engine/strategy/templates/rsi_strategy.h"
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
    if (id == "Momentum") {
        auto s = std::make_shared<MomentumStrategy>();
        s->lookbackPeriod_ = getParam("lookbackPeriod", 20);
        s->exitPeriod_ = getParam("exitPeriod", 10);
        return s;
    }
    if (id == "Breakout") {
        auto s = std::make_shared<BreakoutStrategy>();
        s->entryPeriod_ = getParam("entryPeriod", 20);
        s->exitPeriod_ = getParam("exitPeriod", 10);
        return s;
    }
    if (id == "MeanReversion") {
        auto s = std::make_shared<MeanReversionStrategy>();
        s->maPeriod_ = getParam("maPeriod", 20);
        s->deviationPct_ = getParam("deviationPct", 30);
        return s;
    }
    if (id == "Rsi") {
        auto s = std::make_shared<RsiStrategy>();
        s->buyLevel_ = getParam("buyLevel", 30);
        s->sellLevel_ = getParam("sellLevel", 70);
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
    const std::vector<std::pair<std::string, int>>& params,
    const std::function<void(double)>& subProgress) const {
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
    bcfg.keepEquitySnapshots = cfg.keepEquitySnapshots;

    BacktestEngine engine;
    engine.setConfig(bcfg);
    engine.setDataCache(cfg.cache);
    // 组合内子进度：回测引擎按日期推进（全市场 ~900 天 ≈ 900 点），
    // 由 run() 分摊到全局进度，避免进度条在每个组合上长时间停滞
    if (subProgress) {
        engine.setProgressCallback([subProgress](double p) {
            subProgress(p);
        });
    }
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
    // 每个组合当前的子进度（0~100，BacktestEngine 按日期推进）；互斥保护，
    // 多个并行 lane 各自写自己的槽位，进度回调汇总时读取
    std::vector<double> comboSub(combos.size(), 0.0);
    std::mutex subMutex;
    std::atomic<double> lastReported{0.0};   // 单调保护：进度只前进不倒退

    const auto reportProgress = [&]() {
        if (!progressCb) return;
        const int d = done.load();
        // 汇总：已完成组合数 + 各在跑组合子进度的平均值分摊
        // （并行 lane 各自推进自己的组合，只看第一个未完成组合会导致进度停滞）
        double subSum = 0.0;
        int running = 0;
        {
            std::lock_guard<std::mutex> lk(subMutex);
            for (size_t k = d; k < combos.size(); ++k) {
                subSum += comboSub[k];
                ++running;
            }
        }
        const double finishedWeight = static_cast<double>(d) / static_cast<double>(total);
        double pct = finishedWeight * 100.0;
        if (running > 0) {
            // 在跑组合的平均子进度 × 剩余组合占比
            const double avgSub = subSum / static_cast<double>(running);
            pct += avgSub * static_cast<double>(total - d) / static_cast<double>(total);
        }
        pct = std::min(pct, 100.0);
        // 单调：并行 lane 完成顺序与组合序号无关，直接报会短暂倒退（UI 上像卡住）
        double prev = lastReported.load();
        while (pct > prev && !lastReported.compare_exchange_weak(prev, pct)) {}
        if (pct <= prev) return;
        progressCb(pct);
    };

    auto worker = [&]() {
        while (true) {
            const size_t i = next.fetch_add(1);
            if (i >= combos.size()) break;
            // 子进度回调：更新本组合槽位并上报（组合完成前）
            results[i] = evaluateOne(cfg, combos[i], [&](double sub) {
                {
                    std::lock_guard<std::mutex> lk(subMutex);
                    comboSub[i] = sub;
                }
                reportProgress();
            });
            const int d = done.fetch_add(1) + 1;
            if (progressCb) {
                const double pct = std::min(
                    static_cast<double>(d) / static_cast<double>(total) * 100.0, 100.0);
                double prev = lastReported.load();
                while (pct > prev && !lastReported.compare_exchange_weak(prev, pct)) {}
                if (pct > prev) progressCb(pct);
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
