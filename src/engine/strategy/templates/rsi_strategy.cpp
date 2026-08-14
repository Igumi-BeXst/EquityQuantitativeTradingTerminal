// RSI 策略实现
#include "engine/strategy/templates/rsi_strategy.h"
#include "foundation/utils/indicators.h"

#include <cmath>

namespace st {

void RsiStrategy::initialize() {}
void RsiStrategy::onStart() {}
void RsiStrategy::onStop() {}

void RsiStrategy::onBar(const StrategyContext& ctx) {
    if (!ctx.history ||
        ctx.history->size() < static_cast<size_t>(period_ * 2)) {
        return;
    }
    const auto closes = ctx.history->closes();
    const auto rsiSeries = st::indicators::rsi(closes, period_);
    if (rsiSeries.empty() || !std::isfinite(rsiSeries.back())) return;
    const double rsi = rsiSeries.back();

    const bool inPosition = [&] {
        if (!ctx.portfolio || ctx.portfolio->positions.empty()) return false;
        for (const auto& pos : ctx.portfolio->positions) {
            if (pos.quantity > 0) return true;
        }
        return false;
    }();

    if (!inPosition && rsi < buyLevel_) {
        auto available = ctx.portfolio->available();
        if (available > 1000) {
            buyByAmount(available * 0.9);
        }
    } else if (inPosition && rsi > sellLevel_) {
        sellAll();
    }
}

} // namespace st
