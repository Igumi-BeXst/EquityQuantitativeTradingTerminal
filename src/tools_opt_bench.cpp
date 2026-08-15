// 全市场参数优化耗时基准（P10 第三十一轮验证用）
// 用法: opt_bench [股票数]   → 连接 TDX，拉 N 只股票日线，跑小网格优化，打印耗时
// 目的：量化 Debug vs Release 的性能差异（用户报告全市场优化卡住）。
// 默认 300 只（拉数据 + 优化在 Debug 下也几分钟内完成）；传 0 = 全市场。
#include "data/tdx/tdx_provider.h"
#include "data/data_cache.h"
#include "engine/optimizer/grid_search.h"
#include "engine/backtest/fee_calculator.h"
#include "core/log_manager.h"
#include "foundation/enums.h"
#include "foundation/utils/datetime.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <memory>
#include <vector>

using namespace st;

int main(int argc, char** argv) {
    int poolLimit = 300;
    if (argc > 1) {
        poolLimit = std::atoi(argv[1]);
        if (poolLimit < 0) poolLimit = 0;
    }

    LogManager::instance()->init("logs/opt_bench.log");
    TdxProvider tdx;
    if (!tdx.connect()) {
        std::printf("TDX 连接失败\n");
        return 1;
    }
    for (int i = 0; i < 50 && !tdx.isConnected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!tdx.isConnected()) {
        std::printf("TDX 未就绪\n");
        return 1;
    }

    // 股票池（默认前 N 只可交易 A 股）
    std::vector<StockCode> pool;
    for (auto m : {Market::SH, Market::SZ}) {
        for (const auto& s : tdx.getStockList(m)) {
            if (tdx::isTradableAShare(s.code)) pool.push_back(s.code);
            if (poolLimit > 0 && static_cast<int>(pool.size()) >= poolLimit) break;
        }
        if (poolLimit > 0 && static_cast<int>(pool.size()) >= poolLimit) break;
    }
    std::printf("股票池: %zu 只%s\n", pool.size(),
                poolLimit > 0 ? "（前 N 只）" : "（全市场）");

    // 拉日线（2023-01-01 ~ 今）
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
    std::printf("拉数据 %zu 只 %d bar: %lld ms\n", pool.size(), bars,
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // 小网格：快线 5~11 step2（4 值）× 慢线 20~30 step5（3 值）= 12 组合
    GridSearchConfig cfg;
    cfg.strategyId = "MACross";
    cfg.ranges = {{"fastPeriod", 5, 11, 2}, {"slowPeriod", 20, 30, 5}};
    cfg.symbols = pool;
    cfg.startDate = start;
    cfg.endDate = end;
    cfg.initialCapital = 100000.0;
    cfg.feeConfig = FeeConfig::defaultAShare();
    cfg.objective = Objective::TotalReturn;
    cfg.cache = cache.get();
    cfg.parallelLanes = 2;

    GridSearchOptimizer opt;
    opt.setProgressCallback([](double p) {
        if (static_cast<int>(p) % 25 == 0)
            std::printf("  进度 %.0f%%\n", p);
    });
    auto t2 = std::chrono::steady_clock::now();
    auto results = opt.run(cfg);
    auto t3 = std::chrono::steady_clock::now();
    std::printf("优化 %zu 组合: %lld ms (%.1f s)\n", results.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() / 1000.0);
    if (!results.empty()) {
        std::printf("最优目标值: %.2f\n", results.front().objectiveValue);
    }
    return 0;
}
