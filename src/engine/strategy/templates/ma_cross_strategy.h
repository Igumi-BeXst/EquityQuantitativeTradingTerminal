#pragma once

#include "engine/strategy/istrategy.h"

namespace st {

/// 双均线交叉策略 — 金叉买入，死叉卖出
///
/// 快线上穿慢线 → 买入；快线下穿慢线 → 清仓
class MACrossStrategy : public IStrategy {
public:
    MACrossStrategy() = default;

    std::string name() const override { return "MACross"; }
    void initialize() override;
    void onStart() override;
    void onStop() override;
    void onBar(const StrategyContext& ctx) override;

    // 可配置参数
    int fastPeriod_ = 5;
    int slowPeriod_ = 20;

private:
    double sma(const BarSeries& s, int period, int offset) const;
};

} // namespace st
