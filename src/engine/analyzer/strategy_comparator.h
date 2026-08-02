#pragma once

#include "engine/backtest/backtest_engine.h"
#include "engine/backtest/fee_calculator.h"
#include "engine/strategy/istrategy.h"
#include "data/data_cache.h"
#include "foundation/types.h"
#include "foundation/stock_code.h"
#include "foundation/order.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace st {

/// 单个策略的对比配置
struct ComparisonItem {
    std::string label;                       // 显示名（如 "MA5/20"、"Turtle 20/10"）
    std::string strategyId;                  // "MACross"/"Turtle"
    std::vector<std::pair<std::string, int>> params;
};

/// 策略对比配置
struct ComparisonConfig {
    std::vector<ComparisonItem> items;       // 2~4 个
    std::vector<StockCode> symbols;
    DateTime startDate;
    DateTime endDate;
    Amount initialCapital = 100000.0;
    BarPeriod period = BarPeriod::Daily;
    FeeConfig feeConfig;
    DataCache* cache = nullptr;              // 复用已加载缓存
};

/// 单个策略的对比结果
struct ComparisonItemResult {
    ComparisonItem item;
    bool success = false;
    std::string error;
    Performance performance;
    std::vector<double> equityCurve;
    std::vector<Trade> trades;
};

/// 策略对比器 — 同一数据范围内多策略同时回测，按总收益降序返回
class StrategyComparator {
public:
    void setProgressCallback(std::function<void(double)> cb);

    /// 阻塞式，Worker 线程调用；返回按 totalReturn 降序的结果
    std::vector<ComparisonItemResult> run(const ComparisonConfig& cfg);

private:
    std::function<void(double)> progressCb_;
};

} // namespace st
