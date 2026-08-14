#pragma once

#include "engine/strategy/istrategy.h"

namespace st {

/// RSI 策略 — RSI 超卖买入，超买离场
///
/// RSI(N) < buyLevel 买入；RSI(N) > sellLevel 清仓。period 固定 14。
class RsiStrategy : public IStrategy {
public:
    RsiStrategy() = default;

    std::string name() const override { return "Rsi"; }
    void initialize() override;
    void onStart() override;
    void onStop() override;
    void onBar(const StrategyContext& ctx) override;

    int period_ = 14;      // RSI 周期（固定默认）
    int buyLevel_ = 30;    // 超卖买入线
    int sellLevel_ = 70;   // 超买卖出线
};

} // namespace st
