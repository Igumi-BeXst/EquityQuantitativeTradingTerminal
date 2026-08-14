// 动量策略实现
#include "engine/strategy/templates/momentum_strategy.h"
#include "engine/strategy/templates/strategy_helpers.h"

namespace st {

void MomentumStrategy::initialize() {}
void MomentumStrategy::onStart() {}
void MomentumStrategy::onStop() {}

void MomentumStrategy::onBar(const StrategyContext& ctx) {
    if (!ctx.history ||
        ctx.history->size() < static_cast<size_t>(lookbackPeriod_ + 1)) {
        return;
    }
    const auto& s = *ctx.history;
    const double close = s.current().close;

    // N 日收益率
    const double base = s.lookback(lookbackPeriod_).close;
    if (base <= 0.0) return;
    const double momentumPct = (close / base - 1.0) * 100.0;

    const bool inPosition = st::detail::hasPosition(ctx.portfolio);
    if (!inPosition && momentumPct > static_cast<double>(thresholdPct_) / 10.0) {
        auto available = ctx.portfolio->available();
        if (available > 1000) {
            buyByAmount(available * 0.9);
        }
    } else if (inPosition) {
        const double ma = st::detail::smaAt(s, exitPeriod_);
        if (ma > 0.0 && close < ma) {
            sellAll();
        }
    }
}

} // namespace st
