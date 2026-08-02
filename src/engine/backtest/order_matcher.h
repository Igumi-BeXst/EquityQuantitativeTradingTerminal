#pragma once

#include "foundation/order.h"
#include "foundation/types.h"
#include <optional>

namespace st {

/// 撮合结果
struct MatchResult {
    bool filled = false;          // 是否成交
    Price fillPrice = 0.0;        // 成交价
    Volume fillVolume = 0;        // 成交量
    OrderStatus newStatus = OrderStatus::Pending;
};

/// 订单撮合器 — 根据订单类型和市场价决定成交
///
/// 回测简化假设：以指定价格（通常是下一Bar开盘价）全额成交市价单；
/// 限价单需市场价满足条件才成交。
class OrderMatcher {
public:
    /// 尝试撮合订单
    /// @param order 待撮合订单
    /// @param marketPrice 当前市场价格（回测：下一Bar开盘价）
    /// @param maxVolume 最大可成交量（受资金/持仓限制）
    /// @return 撮合结果
    MatchResult match(const Order& order, Price marketPrice, Volume maxVolume) const;

    /// 撮合后的成交记录构造
    Trade buildTrade(const Order& order, Price fillPrice, Volume fillVolume) const;
};

} // namespace st
