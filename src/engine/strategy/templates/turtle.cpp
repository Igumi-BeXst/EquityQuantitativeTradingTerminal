// 海龟交易策略实现
#include "engine/strategy/templates/turtle_strategy.h"
#include <algorithm>

namespace st {

void TurtleStrategy::initialize() {
    // 参数已在构造默认值中设置
}

void TurtleStrategy::onStart() {}
void TurtleStrategy::onStop() {}

void TurtleStrategy::onBar(const StrategyContext& ctx) {
    if (!ctx.history || ctx.history->size() < static_cast<size_t>(entryPeriod_ + 1)) {
        return;
    }

    const auto& series = *ctx.history;
    double close = series.current().close;

    // 计算唐奇安通道（不含当前 bar）
    double entryHigh = 0.0, exitLow = std::numeric_limits<double>::max();
    for (int i = 1; i <= entryPeriod_; ++i) {
        entryHigh = std::max(entryHigh, series.lookback(i).high);
    }
    for (int i = 1; i <= exitPeriod_; ++i) {
        exitLow = std::min(exitLow, series.lookback(i).low);
    }

    bool inPosition = hasPosition(ctx.portfolio);

    if (!inPosition && close > entryHigh) {
        // 突破买入（用可用资金 90%）
        auto available = ctx.portfolio->available();
        if (available > 1000) {
            buyByAmount(available * 0.9);
        }
    } else if (inPosition && close < exitLow) {
        // 跌破止损线，清仓
        sellAll();
    }
}

bool TurtleStrategy::hasPosition(const Portfolio* pf) const {
    if (!pf || pf->positions.empty()) return false;
    for (const auto& pos : pf->positions) {
        if (pos.quantity > 0) return true;
    }
    return false;
}

} // namespace st
