#pragma once

#include "engine/backtest/backtest_engine.h"
#include "engine/backtest/fee_calculator.h"
#include "data/data_cache.h"
#include "foundation/types.h"
#include "foundation/stock_code.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace st {

/// 压力测试窗口（历史极端行情片段）
struct StressWindow {
    std::string id;            // "crash_2015"
    std::string name;          // "2015-06 股灾"
    DateTime startDate;
    DateTime endDate;
};

/// 压力测试配置
struct StressTestConfig {
    std::string strategyId;
    std::vector<std::pair<std::string, int>> params;
    std::vector<StockCode> symbols;
    Amount initialCapital = 100000.0;
    BarPeriod period = BarPeriod::Daily;
    FeeConfig feeConfig;
    DataCache* cache = nullptr;
    DateTime baselineStart;    // 全期基线起
    DateTime baselineEnd;      // 全期基线止
};

/// 单个窗口的回测结果
struct StressTestResult {
    std::string windowId;
    std::string windowName;
    bool success = false;
    std::string error;
    Performance performance;
    std::vector<double> equityCurve;
};

/// 压力测试输出：各窗口 + 全期基线
struct StressTestOutput {
    std::vector<StressTestResult> windows;
    StressTestResult baseline;
};

/// 压力测试 — 历史极端行情片段回放，评估策略抗压能力
class StressTest {
public:
    void setProgressCallback(std::function<void(double)> cb);

    /// 阻塞式，Worker 线程调用；先跑全期基线，再逐窗口跑
    StressTestOutput run(const StressTestConfig& cfg,
                         const std::vector<StressWindow>& windows);

    /// 预设 A 股极端行情窗口
    static std::vector<StressWindow> defaultWindows();

private:
    std::function<void(double)> progressCb_;
};

} // namespace st
