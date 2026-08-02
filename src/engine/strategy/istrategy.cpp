#include "engine/strategy/istrategy.h"

namespace st {

void IStrategy::setTradingApi(TradingApi api) {
    api_ = std::move(api);
}

void IStrategy::buy(Volume shares) {
    if (api_.placeOrder && shares > 0) {
        api_.placeOrder(currentCode(), Direction::Buy, shares, 0.0);
    }
}

void IStrategy::sell(Volume shares) {
    if (api_.placeOrder && shares > 0) {
        api_.placeOrder(currentCode(), Direction::Sell, shares, 0.0);
    }
}

void IStrategy::buyByAmount(Amount amount) {
    if (api_.placeOrder && amount > 0) {
        api_.placeOrder(currentCode(), Direction::Buy, 0, amount);
    }
}

void IStrategy::sellAll() {
    if (api_.getPortfolio) {
        auto& pf = api_.getPortfolio();
        auto* pos = pf.find(currentCode());
        if (pos && pos->quantity > 0) {
            sell(pos->quantity);
        }
    }
}

const Portfolio& IStrategy::portfolio() const {
    static const Portfolio kEmpty;
    if (api_.getPortfolio) return api_.getPortfolio();
    return kEmpty;
}

const StockCode& IStrategy::currentCode() const {
    static const StockCode kUnknown(Market::Unknown, "");
    if (api_.getCurrentCode) return api_.getCurrentCode();
    return kUnknown;
}

} // namespace st
