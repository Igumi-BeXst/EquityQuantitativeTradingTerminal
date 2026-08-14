// 均值回归策略实现
#include "engine/strategy/templates/mean_reversion_strategy.h"
#include "engine/strategy/templates/strategy_helpers.h"

namespace st {

void MeanReversionStrategy::initialize() {}
void MeanReversionStrategy::onStart() {}
void MeanReversionStrategy::onStop() {}

void MeanReversionStrategy::onBar(const StrategyContext& ctx) {
    if (!ctx.history ||
        ctx.history->size() < static_cast<size_t>(maPeriod_ + 1)) {
        return;
    }
    const auto& s = *ctx.history;
    const double close = s.current().close;
    const double ma = st::detail::smaAt(s, maPeriod_);
    if (ma <= 0.0) return;

    const double deviationPct = (close / ma - 1.0) * 100.0;
    const bool inPosition = st::detail::hasPosition(ctx.portfolio);

    if (!inPosition && deviationPct < -static_cast<double>(deviationPct_) / 10.0) {
        auto available = ctx.portfolio->available();
        if (available > 1000) {
            buyByAmount(available * 0.9);
        }
    } else if (inPosition && deviationPct >= 0.0) {
        sellAll();  // 回归均线上方离场
    }
}

} // namespace st
