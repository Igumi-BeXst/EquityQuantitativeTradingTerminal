#pragma once

#include "engine/strategy/istrategy.h"

namespace st {

/// 动量策略 — N 日收益率突破阈值买入，收盘跌破 M 日均线离场
///
/// momentum = close / close[N日前] - 1；> thresholdPct 买入（趋势确认）；
/// 收盘 < SMA(M) 清仓（趋势破坏）。
class MomentumStrategy : public IStrategy {
public:
    MomentumStrategy() = default;

    std::string name() const override { return "Momentum"; }
    void initialize() override;
    void onStart() override;
    void onStop() override;
    void onBar(const StrategyContext& ctx) override;

    int lookbackPeriod_ = 20;   // 动量回看（N 日收益）
    int exitPeriod_ = 10;       // 离场均线周期
    int thresholdPct_ = 50;     // 动量阈值（千分数，50 = 5%）
};

} // namespace st
