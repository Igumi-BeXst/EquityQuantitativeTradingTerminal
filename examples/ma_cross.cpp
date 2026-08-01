// 双均线交叉策略 (Dual Moving Average Crossover)
#include "engine/strategy/istrategy.h"

namespace st {

class MACrossStrategy : public IStrategy {
public:
    void initialize() override {
        // 默认参数: 5日快线, 20日慢线
        fastPeriod_ = 5;
        slowPeriod_ = 20;
    }

    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        if (ctx.history == nullptr || ctx.history->size() < static_cast<size_t>(slowPeriod_)) {
            return;
        }

        // 计算均线
        double fastMA = calcMA(*ctx.history, fastPeriod_);
        double slowMA = calcMA(*ctx.history, slowPeriod_);
        double prevFastMA = calcMAPrev(*ctx.history, fastPeriod_);
        double prevSlowMA = calcMAPrev(*ctx.history, slowPeriod_);

        bool goldenCross = (prevFastMA <= prevSlowMA) && (fastMA > slowMA);   // 金叉
        bool deathCross  = (prevFastMA >= prevSlowMA) && (fastMA < slowMA);   // 死叉

        if (goldenCross) {
            buy(ctx.portfolio().available() * 0.8); // 买入80%仓位
        } else if (deathCross) {
            closePosition();                        // 全部卖出
        }
    }

private:
    int fastPeriod_ = 5;
    int slowPeriod_ = 20;

    double calcMA(const BarSeries& bars, int period) const {
        if (period <= 0 || bars.size() < static_cast<size_t>(period)) return 0.0;
        double sum = 0.0;
        for (int i = 0; i < period; ++i) {
            sum += bars.lookback(i).close;
        }
        return sum / period;
    }

    double calcMAPrev(const BarSeries& bars, int period) const {
        if (period <= 0 || bars.size() < static_cast<size_t>(period + 1)) return 0.0;
        double sum = 0.0;
        for (int i = 0; i < period; ++i) {
            sum += bars.lookback(i + 1).close;
        }
        return sum / period;
    }
};

} // namespace st
