// 动量策略 (Momentum Strategy)
// 买入过去N日涨幅最大的股票，持有M日后重新评估
#include "engine/strategy/istrategy.h"

namespace st {

class MomentumStrategy : public IStrategy {
public:
    void initialize() override {
        lookbackPeriod_ = 20;   // 过去20日动量
        holdPeriod_     = 5;    // 持仓5日后评估
        topN_           = 5;    // 选前5只
    }

    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        if (ctx.history == nullptr || ctx.history->size() < static_cast<size_t>(lookbackPeriod_)) {
            return;
        }

        // 计算动量: (当前收盘 - N日前收盘) / N日前收盘
        double momentum = (ctx.history->current().close -
                           ctx.history->lookback(lookbackPeriod_).close) /
                           ctx.history->lookback(lookbackPeriod_).close;

        // 动量 > 阈值 → 买入, 动量 < -阈值 → 卖出
        if (momentum > entryThreshold_ && !ctx.portfolio().find(ctx.currentCode)) {
            buy(ctx.portfolio().available() * 0.2); // 每次20%仓位
        } else if (momentum < exitThreshold_) {
            closePosition();
        }
    }

private:
    int lookbackPeriod_ = 20;
    int holdPeriod_ = 5;
    int topN_ = 5;
    double entryThreshold_ = 0.05;  // 5% 动量阈值
    double exitThreshold_ = -0.03;  // -3% 退出阈值
};

} // namespace st
