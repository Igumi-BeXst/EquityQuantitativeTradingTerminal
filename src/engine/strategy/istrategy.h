#pragma once

#include "foundation/bar.h"
#include "foundation/order.h"
#include "foundation/portfolio.h"
#include "foundation/tick.h"
#include "foundation/stock_code.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace st {

/// 策略上下文 — 引擎在每次回调时注入
struct StrategyContext {
    const StockCode*  currentCode = nullptr;  // 当前处理的股票
    const Bar*        currentBar  = nullptr;  // 当前 K 线
    const BarSeries*  history     = nullptr;  // 该股票的历史序列（含当前 bar）
    const Portfolio*  portfolio   = nullptr;  // 当前组合快照
    BarPeriod         period      = BarPeriod::Daily;
};

/// 交易回调 — 引擎执行交易后回调
struct TradeContext {
    const Trade* trade = nullptr;
};

/// 策略基类 — 策略引擎统一通过此接口驱动
///
/// 子类需实现 initialize() 和 onBar()。
/// 下单通过 buy/sell 等受保护方法，引擎负责撮合。
class IStrategy {
public:
    virtual ~IStrategy() = default;

    /// 策略唯一标识
    virtual std::string name() const = 0;

    /// 生命周期
    virtual void initialize() = 0;   // 初始化参数
    virtual void onStart() = 0;      // 回测/模拟开始
    virtual void onStop() = 0;       // 结束

    /// 核心入口 — 每个 Bar 触发一次
    virtual void onBar(const StrategyContext& ctx) = 0;

    /// 可选事件钩子
    virtual void onTrade(const TradeContext& ctx) { (void)ctx; }

    // --- 由引擎注入的接口（引擎调用）---
    struct TradingApi {
        std::function<void(StockCode, Direction, Volume, Amount)> placeOrder;
        std::function<const Portfolio&()> getPortfolio;
        std::function<const StockCode&()> getCurrentCode;
    };
    void setTradingApi(TradingApi api);

protected:
    // --- 下单 API（由引擎实现，策略调用）---
    void buy(Volume shares);             // 按股数买入
    void sell(Volume shares);            // 按股数卖出
    void buyByAmount(Amount amount);     // 按金额买入（自动计算股数）
    void sellAll();                      // 清仓

    // --- 查询 API ---
    const Portfolio& portfolio() const;
    const StockCode& currentCode() const;

private:
    TradingApi api_;
};

} // namespace st
