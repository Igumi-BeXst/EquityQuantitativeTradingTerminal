// 收盘突破策略实现
#include "engine/strategy/templates/breakout_strategy.h"
#include "engine/strategy/templates/strategy_helpers.h"

#include <limits>

namespace st {

void BreakoutStrategy::initialize() {}
void BreakoutStrategy::onStart() {}
void BreakoutStrategy::onStop() {}

void BreakoutStrategy::onBar(const StrategyContext& ctx) {
    if (!ctx.history ||
        ctx.history->size() < static_cast<size_t>(entryPeriod_ + 1)) {
        return;
    }
    const auto& s = *ctx.history;
    const double close = s.current().close;

    // 前 N 根最高收盘 / 前 M 根最低收盘（不含当前 bar）
    double entryHigh = 0.0;
    double exitLow = std::numeric_limits<double>::max();
    for (int i = 1; i <= entryPeriod_; ++i) {
        entryHigh = std::max(entryHigh, s.lookback(i).close);
    }
    for (int i = 1; i <= exitPeriod_; ++i) {
        exitLow = std::min(exitLow, s.lookback(i).close);
    }

    const bool inPosition = st::detail::hasPosition(ctx.portfolio);
    if (!inPosition && close > entryHigh) {
        auto available = ctx.portfolio->available();
        if (available > 1000) {
            buyByAmount(available * 0.9);
        }
    } else if (inPosition && close < exitLow) {
        sellAll();
    }
}

} // namespace st
