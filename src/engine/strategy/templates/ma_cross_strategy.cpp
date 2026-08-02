// 双均线交叉策略实现
#include "engine/strategy/templates/ma_cross_strategy.h"

namespace st {

void MACrossStrategy::initialize() {}

void MACrossStrategy::onStart() {}
void MACrossStrategy::onStop() {}

void MACrossStrategy::onBar(const StrategyContext& ctx) {
    if (!ctx.history || ctx.history->size() < static_cast<size_t>(slowPeriod_ + 1)) {
        return;
    }
    const auto& s = *ctx.history;
    double maFast = sma(s, fastPeriod_, 0);
    double maSlow = sma(s, slowPeriod_, 0);
    double prevFast = sma(s, fastPeriod_, 1);
    double prevSlow = sma(s, slowPeriod_, 1);

    bool goldenCross = prevFast <= prevSlow && maFast > maSlow;
    bool deathCross = prevFast >= prevSlow && maFast < maSlow;

    if (goldenCross && ctx.portfolio->available() > 1000) {
        buyByAmount(ctx.portfolio->available() * 0.9);
    } else if (deathCross) {
        // 清仓
        for (const auto& pos : ctx.portfolio->positions) {
            if (pos.quantity > 0) sellAll();
        }
    }
}

double MACrossStrategy::sma(const BarSeries& s, int period, int offset) const {
    // offset=0 用最近 period 根 (lookback 1..period)
    // offset=1 用往前偏移一档 (lookback 2..period+1)
    if (s.size() < static_cast<size_t>(period + offset + 1)) return 0.0;
    double sum = 0.0;
    for (int i = 1; i <= period; ++i) {
        sum += s.lookback(i + offset).close;
    }
    return sum / period;
}

} // namespace st
