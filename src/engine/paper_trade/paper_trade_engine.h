#pragma once

#include "engine/strategy/istrategy.h"
#include "engine/backtest/fee_calculator.h"
#include "engine/backtest/order_matcher.h"
#include "foundation/portfolio.h"
#include "foundation/tick.h"
#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>

namespace st {

/// 模拟交易配置
struct PaperTradeConfig {
    Amount initialCapital = 100000.0;
    FeeConfig feeConfig;
    double slippage = 0.001;      // 滑点比例 (0.1%)
};

/// 模拟交易引擎 — 实时行情驱动
///
/// 与 BacktestEngine 共用 IStrategy 接口，但由实时 Quote/Tick 驱动。
/// 收到行情 → 触发 onBar → 策略下单 → 以当前价+滑点撮合。
///
/// 交易数据每日保存到 SQLite（P1 Data 层），此处先实现内存版。
class PaperTradeEngine {
public:
    PaperTradeEngine();
    ~PaperTradeEngine();

    void setConfig(const PaperTradeConfig& config);

    /// 添加策略
    void addStrategy(std::shared_ptr<IStrategy> strategy);

    /// 播种历史（启动前用历史日线填充，趋势策略可立即计算均线）
    void seedHistory(const StockCode& code, const std::vector<Bar>& bars);

    /// 行情驱动入口 — 收到实时报价时调用
    void onQuote(const StockCode& code, Price price, DateTime time);

    /// 手动开/关
    void start();
    void stop();
    bool isRunning() const { return running_; }

    /// 当前组合
    const Portfolio& portfolio() const { return portfolio_; }

    /// 累计成交
    const std::vector<Trade>& trades() const { return trades_; }

    /// 今日盈亏
    double todayPnl() const { return todayPnl_; }

private:
    void submitOrder(StockCode code, Direction dir, Volume vol);
    void executeTrade(StockCode code, Direction dir, Volume vol, Price price, DateTime time);
    Position* findPosition(const StockCode& code);

    PaperTradeConfig config_;
    bool running_ = false;
    Portfolio portfolio_;
    std::vector<Trade> trades_;
    double todayPnl_ = 0.0;
    int nextOrderId_ = 1;

    std::vector<std::shared_ptr<IStrategy>> strategies_;
    FeeCalculator feeCalculator_;
    std::unique_ptr<OrderMatcher> matcher_;
    std::optional<Order> pendingOrder_;   // 挂起的市价单
    StockCode currentCode_;               // 当前行情代码
    Price lastPrice_ = 0.0;               // 最近报价（按金额下单换算用）
    std::map<std::string, std::vector<Bar>> history_;  // code -> 报价历史
};

} // namespace st
