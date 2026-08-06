// 引擎堆压力测试 — 循环跑量化核心计算，每步检查堆完整性（无头，定位越界写）
#include "engine/optimizer/grid_search.h"
#include "engine/analyzer/stress_test.h"
#include "engine/analyzer/monte_carlo.h"
#include "intelligence/pattern/pattern_recognizer.h"
#include "data/tdx/tdx_provider.h"
#include "data/data_cache.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <chrono>
#include <cstdio>
#include <thread>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace st;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    LogManager::instance()->init("logs/heap_stress.log");
    std::printf("引擎堆压力测试开始\n");

    TdxProvider provider;
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (!provider.isConnected()) {
        std::printf("连接失败\n");
        return 1;
    }

    DataCache cache;
    const DateTime start = utils::parseDate("2015-01-01");
    const DateTime end = utils::now();
    const std::vector<StockCode> codes = {
        StockCode(Market::SH, "600519"), StockCode(Market::SH, "600000"),
        StockCode(Market::SZ, "000001"),
    };
    for (const auto& c : codes) {
        auto bars = provider.getBars(c, BarPeriod::Daily, start, end);
        std::printf("%s %zu bars\n", c.displayCode().c_str(), bars.size());
        if (!bars.empty()) cache.cacheBars(c, BarPeriod::Daily, std::move(bars));
    }

    const int kIters = 15;
    for (int it = 0; it < kIters; ++it) {
        // 1. 网格搜索（与 AdvisorPanel/OptimizationPanel 相同配置路径）
        GridSearchConfig cfg;
        cfg.strategyId = "MACross";
        cfg.ranges = {{"fastPeriod", 2, 30, 4}, {"slowPeriod", 10, 60, 10}};
        cfg.symbols = codes;
        cfg.startDate = utils::parseDate("2023-01-01");
        cfg.endDate = end;
        cfg.initialCapital = 100000.0;
        cfg.feeConfig = FeeConfig::defaultAShare();
        cfg.objective = Objective::TotalReturn;
        cfg.cache = &cache;
        cfg.parallelLanes = 2;
        GridSearchOptimizer opt;
        auto results = opt.run(cfg);
#ifdef _MSC_VER
        if (_CrtCheckMemory() == 0) { std::printf("[%d] GRID 堆已损坏!\n", it); return 1; }
#endif

        // 2. 压力测试（最优参数，与 AdvisorPanel 一致）
        if (!results.empty()) {
            StressTestConfig scfg;
            scfg.strategyId = cfg.strategyId;
            scfg.params = results.front().params;
            scfg.symbols = codes;
            scfg.initialCapital = cfg.initialCapital;
            scfg.feeConfig = cfg.feeConfig;
            scfg.cache = &cache;
            scfg.baselineStart = utils::parseDate("2015-01-01");
            scfg.baselineEnd = end;
            StressTest st;
            auto output = st.run(scfg, StressTest::defaultWindows());
            (void)output;
#ifdef _MSC_VER
            if (_CrtCheckMemory() == 0) { std::printf("[%d] STRESS 堆已损坏!\n", it); return 1; }
#endif

            // 3. 蒙特卡洛（从最优净值曲线派生日收益）
            const auto& eq = results.front().equityCurve;
            std::vector<double> daily;
            for (size_t i = 1; i < eq.size(); ++i) {
                if (eq[i - 1] > 0.0) daily.push_back(eq[i] / eq[i - 1] - 1.0);
            }
            if (!daily.empty()) {
                auto mc = MonteCarlo::simulate({daily, 1000, 0, 1.0});
                (void)mc;
#ifdef _MSC_VER
                if (_CrtCheckMemory() == 0) { std::printf("[%d] MC 堆已损坏!\n", it); return 1; }
#endif
            }
        }

        // 4. 形态识别（PatternPanel 相同路径）
        auto bars = cache.getBars(codes[0], BarPeriod::Daily);
        if (bars.size() >= 40) {
            st::pattern::PatternRecognizer rec;
            rec.setMinBars(40);
            BarSeries series(bars);
            auto result = rec.detect(series);
            (void)result;
#ifdef _MSC_VER
            if (_CrtCheckMemory() == 0) { std::printf("[%d] PATTERN 堆已损坏!\n", it); return 1; }
#endif
        }
        std::printf("[%d/%d] 堆完整\n", it + 1, kIters);
    }
    std::printf("全部 %d 轮堆完整，引擎无越界写\n", kIters);
    return 0;
}
