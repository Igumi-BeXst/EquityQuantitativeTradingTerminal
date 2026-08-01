#pragma once
#include "foundation/bar.h"
#include "foundation/order.h"
#include "foundation/portfolio.h"
namespace st {
struct StrategyContext { const Bar* currentBar = nullptr; const BarSeries* history = nullptr; };
class IStrategy { public: virtual ~IStrategy() = default;
    virtual void initialize() = 0; virtual void onStart() = 0; virtual void onStop() = 0;
    virtual void onBar(const StrategyContext& ctx) = 0;
protected: void buy(double) {} void sell(double) {} Portfolio portfolio() const { return {}; } };
}
