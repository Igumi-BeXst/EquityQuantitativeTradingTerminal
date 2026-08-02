#pragma once

#include "engine/strategy/istrategy.h"
#include "engine/backtest/fee_calculator.h"
#include "engine/backtest/order_matcher.h"
#include "engine/backtest/performance.h"
#include "data/data_cache.h"
#include "foundation/portfolio.h"
#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace st {

/// 回测配置
struct BacktestConfig {
    std::vector<StockCode> symbols;   // 股票池
    DateTime startDate;
    DateTime endDate;
    Amount initialCapital = 100000.0; // 初始资金
    BarPeriod period = BarPeriod::Daily;
    FeeConfig feeConfig;              // 费率
    int positionLimitPercent = 100;   // 单只股票最大仓位 (%)
};

/// 回测结果
struct BacktestResult {
    bool success = false;
    std::string error;                // 失败原因

    Performance performance;          // 绩效指标
    Portfolio finalPortfolio;         // 期末组合
    std::vector<Trade> trades;        // 全部成交记录
    std::vector<Portfolio> equitySnapshots; // 每日净值快照

    // 统计
    int barCount = 0;
    double commissionTotal = 0.0;
    double totalFees = 0.0;
};

/// 事件驱动回测引擎
///
/// 流程:
///   1. 从 DataCache 加载所有股票 Bar 数据
///   2. 构建全局时间轴（按日期对齐）
///   3. 逐 Bar 驱动各策略 onBar
///   4. 撮合策略订单（下一Bar开盘价成交）
///   5. 更新资金/持仓 → 记录净值
///
/// 成交假设: 市价单以下一 Bar 开盘价成交（简化）
class BacktestEngine {
public:
    BacktestEngine();
    ~BacktestEngine();

    /// 设置回测配置
    void setConfig(const BacktestConfig& config);

    /// 注入数据源
    void setDataCache(DataCache* cache) { cache_ = cache; }

    /// 添加策略
    void addStrategy(std::shared_ptr<IStrategy> strategy);

    /// 执行回测
    BacktestResult run();

    /// 进度回调
    void setProgressCallback(std::function<void(double)> cb) { progressCb_ = std::move(cb); }

private:
    /// 内部账户 — 管理资金和持仓
    class Account;

    void initStrategies();
    void processBar(const Bar& bar);
    void matchOrders(const StockCode& code, Price openPrice);
    void updatePortfolio(const StockCode& code);
    Portfolio snapshot() const;

    BacktestConfig config_;
    DataCache* cache_ = nullptr;
    std::vector<std::shared_ptr<IStrategy>> strategies_;
    std::unique_ptr<Account> account_;
    std::unique_ptr<OrderMatcher> matcher_;
    FeeCalculator feeCalculator_;
    BacktestResult result_;
    std::function<void(double)> progressCb_;
    int nextOrderId_ = 1;
    std::vector<Trade> trades_;
    StockCode currentCode_;   // 当前正在处理的股票（策略查询用）
    DateTime currentTime_;    // 当前 bar 时间（下单时间戳）
    mutable Portfolio cachedPortfolio_;  // getPortfolio 缓存（实例成员，线程隔离）
};

} // namespace st
