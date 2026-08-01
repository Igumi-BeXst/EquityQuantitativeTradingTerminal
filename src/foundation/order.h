#pragma once

#include "foundation/types.h"
#include "foundation/enums.h"
#include "foundation/stock_code.h"
#include <string>

namespace st {

/// Order placed by strategy
struct Order {
    OrderId     id;
    StockCode   code;
    Direction   direction   = Direction::Buy;
    OrderType   type        = OrderType::Market;
    OrderStatus status      = OrderStatus::Pending;
    Price       limitPrice  = 0.0;   // 限价（限价单使用）
    Volume      volume      = 0;     // 委托数量
    Volume      filledVol   = 0;     // 已成交数量
    Price       avgFillPrice = 0.0;  // 成交均价
    DateTime    createTime;
    DateTime    updateTime;
    StrategyId  strategyId;          // 策略来源

    [[nodiscard]] bool isFilled() const {
        return status == OrderStatus::Filled;
    }
    [[nodiscard]] bool isActive() const {
        return status == OrderStatus::Pending || status == OrderStatus::Partial;
    }
    [[nodiscard]] Volume unfilled() const {
        return volume - filledVol;
    }
};

/// Trade/Execution record
struct Trade {
    TradeId     id;
    OrderId     orderId;
    StockCode   code;
    Direction   direction = Direction::Buy;
    Price       price     = 0.0;
    Volume      volume    = 0;
    Amount      amount    = 0.0;    // 成交金额
    Amount      commission = 0.0;   // 佣金
    Amount      stampTax   = 0.0;   // 印花税
    Amount      otherFees  = 0.0;   // 其他费用
    Amount      totalFee   = 0.0;   // 总费用
    DateTime    time;
    StrategyId  strategyId;

    [[nodiscard]] Amount netAmount() const { return amount + totalFee; } // buy: positive cost, sell: negative revenue
};

/// Order status update event
struct OrderUpdate {
    OrderId     orderId;
    OrderStatus oldStatus = OrderStatus::Pending;
    OrderStatus newStatus = OrderStatus::Pending;
    Volume      filledVol = 0;
    Price       avgPrice  = 0.0;
    DateTime    time;
};

} // namespace st
