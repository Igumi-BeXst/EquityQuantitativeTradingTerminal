#include "engine/analyzer/stress_test.h"
#include "engine/optimizer/grid_search.h"
#include "foundation/utils/datetime.h"
#include <utility>

namespace st {

void StressTest::setProgressCallback(std::function<void(double)> cb) {
    progressCb_ = std::move(cb);
}

std::vector<StressWindow> StressTest::defaultWindows() {
    return {
        {"crash_2015",    "2015-06 股灾",   utils::parseDate("2015-06-01"),
         utils::parseDate("2015-08-31")},
        {"circuit_2016",  "2016-01 熔断",   utils::parseDate("2016-01-04"),
         utils::parseDate("2016-02-29")},
        {"bear_2018",     "2018 熊市",      utils::parseDate("2018-01-01"),
         utils::parseDate("2018-12-31")},
        {"covid_2020",    "2020-02 疫情",   utils::parseDate("2020-02-03"),
         utils::parseDate("2020-03-31")},
        {"microcap_2024", "2024-02 微盘股", utils::parseDate("2024-01-02"),
         utils::parseDate("2024-02-29")},
    };
}

StressTestOutput StressTest::run(const StressTestConfig& cfg,
                                 const std::vector<StressWindow>& windows) {
    StressTestOutput out;
    if (!cfg.cache || cfg.symbols.empty()) return out;

    auto runWindow = [&](const std::string& id, const std::string& name,
                         DateTime start, DateTime end) -> StressTestResult {
        StressTestResult r;
        r.windowId = id;
        r.windowName = name;

        auto strategy = GridSearchOptimizer::makeStrategy(cfg.strategyId, cfg.params);
        if (!strategy) return r;

        BacktestConfig bcfg;
        bcfg.symbols = cfg.symbols;
        bcfg.startDate = start;
        bcfg.endDate = end;
        bcfg.initialCapital = cfg.initialCapital;
        bcfg.period = cfg.period;
        bcfg.feeConfig = cfg.feeConfig;
        // 全市场大池内存优化：净值曲线引擎内部累积，不存每日 Portfolio 快照
        bcfg.keepEquitySnapshots = false;

        BacktestEngine engine;
        engine.setConfig(bcfg);
        engine.setDataCache(cfg.cache);
        // 窗口内子进度（引擎按日期推进）：分摊到全局，避免进度条停滞
        if (progressCb_) {
            const int idx = static_cast<int>(out.windows.size()) + 1;  // 0=基线
            const double total = static_cast<double>(windows.size()) + 1.0;
            engine.setProgressCallback([this, idx, total](double p) {
                progressCb_((static_cast<double>(idx) + p / 100.0) / total * 100.0);
            });
        }
        engine.addStrategy(std::move(strategy));
        auto result = engine.run();

        r.success = result.success;
        r.error = result.error;
        r.performance = result.performance;
        r.equityCurve = result.performance.equityCurve;
        return r;
    };

    // 全期基线
    out.baseline = runWindow("baseline", "全期基线",
                             cfg.baselineStart, cfg.baselineEnd);

    // 逐窗口
    const double total = static_cast<double>(windows.size()) + 1.0;
    double done = 1.0;
    for (const auto& w : windows) {
        out.windows.push_back(runWindow(w.id, w.name, w.startDate, w.endDate));
        ++done;
        if (progressCb_) progressCb_(done / total * 100.0);
    }
    if (windows.empty() && progressCb_) progressCb_(100.0);
    return out;
}

} // namespace st
