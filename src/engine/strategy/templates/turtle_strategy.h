#pragma once

#include "engine/strategy/istrategy.h"
#include <limits>

namespace st {

/// 海龟交易策略 — 唐奇安通道突破
///
/// 入场: 收盘价突破 N 日最高 → 买入
/// 出场: 收盘价跌破 M 日最低 → 清仓
class TurtleStrategy : public IStrategy {
public:
    TurtleStrategy() = default;

    std::string name() const override { return "Turtle"; }
    void initialize() override;
    void onStart() override;
    void onStop() override;
    void onBar(const StrategyContext& ctx) override;

    // 可配置参数
    int entryPeriod_ = 20;  // 入场通道周期
    int exitPeriod_  = 10;  // 出场通道周期

private:
    bool hasPosition(const Portfolio* pf) const;
};

} // namespace st
