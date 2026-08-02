#include "engine/backtest/order_matcher.h"
#include <algorithm>

namespace st {

MatchResult OrderMatcher::match(const Order& order, Price marketPrice, Volume maxVolume) const {
    MatchResult result;
    if (!order.isActive() || marketPrice <= 0 || order.unfilled() <= 0) {
        return result;
    }

    Volume wantVolume = order.unfilled();
    Volume fillVolume = std::min(wantVolume, maxVolume);
    if (fillVolume <= 0) {
        // 无可成交量（资金/持仓不足）
        result.newStatus = OrderStatus::Pending;
        return result;
    }

    Price fillPrice = 0.0;
    switch (order.type) {
        case OrderType::Market:
            // 市价单：以市场价成交
            fillPrice = marketPrice;
            break;
        case OrderType::Limit: {
            // 限价单：买入时市场价≤限价才成交；卖出时市场价≥限价
            if (order.direction == Direction::Buy) {
                if (marketPrice > order.limitPrice) {
                    result.newStatus = OrderStatus::Pending;  // 未触发
                    return result;
                }
            } else {
                if (marketPrice < order.limitPrice) {
                    result.newStatus = OrderStatus::Pending;
                    return result;
                }
            }
            fillPrice = order.limitPrice;
            break;
        }
        default:
            return result;
    }

    result.filled = true;
    result.fillPrice = fillPrice;
    result.fillVolume = fillVolume;
    result.newStatus = (fillVolume < wantVolume)
        ? OrderStatus::Partial
        : OrderStatus::Filled;
    return result;
}

Trade OrderMatcher::buildTrade(const Order& order, Price fillPrice, Volume fillVolume) const {
    Trade trade;
    trade.orderId = order.id;
    trade.code = order.code;
    trade.direction = order.direction;
    trade.price = fillPrice;
    trade.volume = fillVolume;
    trade.amount = fillPrice * fillVolume;
    trade.time = order.updateTime;
    trade.strategyId = order.strategyId;
    return trade;
}

} // namespace st
