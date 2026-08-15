#pragma once

#include "engine/backtest/backtest_engine.h"
#include "engine/backtest/fee_calculator.h"
#include "engine/strategy/istrategy.h"
#include "data/data_cache.h"
#include "foundation/types.h"
#include "foundation/stock_code.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <atomic>
#include <mutex>

namespace st {

/// 参数优化目标函数
enum class Objective : uint8_t {
    TotalReturn,     // 总收益率 (最大化)
    SharpeRatio,     // 夏普比率 (最大化)
    MaxDrawdown,     // 最大回撤 (最小化)
    CalmarRatio,     // 卡玛比率 (最大化)
    ProfitFactor,    // 盈亏比 (最大化)
};

/// 单个参数范围（整数参数）
struct ParamRange {
    std::string name;   // "fastPeriod"/"slowPeriod"/"entryPeriod"/"exitPeriod"
    int from = 0;
    int to = 0;
    int step = 1;
};

/// 网格搜索配置
struct GridSearchConfig {
    std::string strategyId;            // "MACross"/"Turtle"
    std::vector<ParamRange> ranges;    // 1~2 个参数范围
    std::vector<StockCode> symbols;
    DateTime startDate;
    DateTime endDate;
    Amount initialCapital = 100000.0;
    BarPeriod period = BarPeriod::Daily;
    FeeConfig feeConfig;
    Objective objective = Objective::TotalReturn;
    DataCache* cache = nullptr;        // 复用已加载缓存，禁止重复 IO
    int parallelLanes = 1;             // 并行通道数（测试设 1 保证确定性）
};

/// 单组参数的回测结果
struct GridSearchResult {
    std::vector<std::pair<std::string, int>> params;   // 参数名→值
    double objectiveValue = 0.0;
    Performance performance;
    std::vector<double> equityCurve;
    bool success = false;
};

/// 网格搜索优化器 — 参数组合穷举 + 并行回测 + 目标排序
///
/// 在 Worker 线程调用（阻塞式）。内部用 std::async 并行跑组合，
/// 共享只读 DataCache（不触发任何网络 IO）。
/// 进度回调：组合粒度 → 每个组合内按股票数细分上报（progress 0~100，
/// 已在组合间分摊：comboIdx*100/total + 组合内股票进度/total），
/// 避免全市场大池时进度条长时间停滞。
class GridSearchOptimizer {
public:
    void setProgressCallback(std::function<void(double)> cb);

    /// 返回按目标值排序的结果（MaxDrawdown 升序，其余降序）
    std::vector<GridSearchResult> run(const GridSearchConfig& cfg);

    /// 生成参数组合（笛卡尔积）
    static std::vector<std::vector<std::pair<std::string, int>>>
    generateCombinations(const GridSearchConfig& cfg);

    /// 按策略 id + 参数构造策略对象（参数名不匹配时用默认值）
    static std::shared_ptr<IStrategy> makeStrategy(
        const std::string& id,
        const std::vector<std::pair<std::string, int>>& params);

    /// 从绩效提取目标函数值
    static double objectiveValue(const Performance& p, Objective o);

    /// 目标是否越小越好（当前仅 MaxDrawdown）
    static bool objectiveMinimized(Objective o);

private:
    /// 评估单组参数。subProgress: 组合内子进度回调（0~100，可空）
    GridSearchResult evaluateOne(
        const GridSearchConfig& cfg,
        const std::vector<std::pair<std::string, int>>& params,
        const std::function<void(double)>& subProgress = {}) const;

    std::function<void(double)> progressCb_;
};

} // namespace st
