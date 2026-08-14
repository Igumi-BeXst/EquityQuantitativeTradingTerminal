#pragma once

#include "engine/strategy/istrategy.h"

namespace st {

/// 均值回归策略 — 收盘低于均线 X% 买入（超跌反弹），回到均线上方离场
///
/// deviation = close / SMA(N) - 1；< -deviationPct 买入；>= 0 卖出。
class MeanReversionStrategy : public IStrategy {
public:
    MeanReversionStrategy() = default;

    std::string name() const override { return "MeanReversion"; }
    void initialize() override;
    void onStart() override;
    void onStop() override;
    void onBar(const StrategyContext& ctx) override;

    int maPeriod_ = 20;      // 均线周期
    int deviationPct_ = 30;  // 超跌阈值（千分数，30 = 3%）
};

} // namespace st
