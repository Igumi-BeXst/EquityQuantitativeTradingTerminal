#pragma once

#include "engine/strategy/istrategy.h"

namespace st {

/// 收盘突破策略 — 收盘价突破 N 日最高收盘买入，跌破 M 日最低收盘离场
///
/// 与海龟（唐奇安盘中高低价）区分：用**收盘价**确认突破，减少假突破。
class BreakoutStrategy : public IStrategy {
public:
    BreakoutStrategy() = default;

    std::string name() const override { return "Breakout"; }
    void initialize() override;
    void onStart() override;
    void onStop() override;
    void onBar(const StrategyContext& ctx) override;

    int entryPeriod_ = 20;  // 突破回看（最高收盘）
    int exitPeriod_ = 10;   // 离场回看（最低收盘）
};

} // namespace st
