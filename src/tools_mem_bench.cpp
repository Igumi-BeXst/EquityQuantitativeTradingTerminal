// 全市场回测内存验证（P10 第三十二轮用）
// 拉全市场日线 → 单次 BacktestEngine 回测（keepEquitySnapshots=false），
// 打印峰值工作集内存 + 耗时，对比优化前后
#include "data/tdx/tdx_provider.h"
#include "data/data_cache.h"
#include "engine/backtest/backtest_engine.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "engine/backtest/fee_calculator.h"
#include "core/log_manager.h"
#include "foundation/enums.h"
#include "foundation/utils/datetime.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <memory>

using namespace st;

static size_t peakWorkingSetMB() {
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize / (1024 * 1024);
    }
    return 0;
}

int main() {
    LogManager::instance()->init("logs/mem_bench.log");
    TdxProvider tdx;
    tdx.connect();
    for (int i = 0; i < 50 && !tdx.isConnected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::vector<StockCode> pool;
    for (auto m : {Market::SH, Market::SZ}) {
        for (const auto& s : tdx.getStockList(m)) {
            if (tdx::isTradableAShare(s.code)) pool.push_back(s.code);
        }
    }
    std::printf("全市场池: %zu 只\n", pool.size());

    auto cache = std::make_shared<DataCache>();
    const auto start = utils::parseDate("2023-01-01");
    const auto end = utils::now();
    auto t0 = std::chrono::steady_clock::now();
    int bars = 0;
    for (const auto& code : pool) {
        auto b = tdx.getBars(code, BarPeriod::Daily, start, end);
        bars += static_cast<int>(b.size());
        if (!b.empty()) cache->cacheBars(code, BarPeriod::Daily, std::move(b));
    }
    auto t1 = std::chrono::steady_clock::now();
    std::printf("拉数据 %d bar: %lld ms, 峰值内存 %zu MB\n", bars,
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
        peakWorkingSetMB());

    BacktestConfig cfg;
    cfg.symbols = pool;
    cfg.startDate = start;
    cfg.endDate = end;
    cfg.initialCapital = 100000.0;
    cfg.period = BarPeriod::Daily;
    cfg.feeConfig = FeeConfig::defaultAShare();
    cfg.keepEquitySnapshots = false;   // 优化后

    BacktestEngine engine;
    engine.setConfig(cfg);
    engine.setDataCache(cache.get());
    engine.addStrategy(std::make_shared<MACrossStrategy>());
    auto t2 = std::chrono::steady_clock::now();
    auto result = engine.run();
    auto t3 = std::chrono::steady_clock::now();
    std::printf("回测: 总收益 %.2f%% 交易日 %d, 耗时 %lld ms, 峰值内存 %zu MB\n",
        result.performance.totalReturn, result.barCount,
        std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count(),
        peakWorkingSetMB());
    return 0;
}
