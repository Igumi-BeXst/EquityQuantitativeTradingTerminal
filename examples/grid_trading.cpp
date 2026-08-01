// 网格交易策略 (Grid Trading)
// 在价格区间内设置多个买卖网格，低买高卖
#include "engine/strategy/istrategy.h"

namespace st {

class GridTradingStrategy : public IStrategy {
public:
    void initialize() override {
        gridLevels_    = 10;       // 10个网格
        upperPrice_    = 10.0;     // 价格上限
        lowerPrice_    = 8.0;      // 价格下限
        gridSize_      = (upperPrice_ - lowerPrice_) / gridLevels_;
        positionPerGrid_ = 0.1;    // 每个网格用10%资金
    }

    void onStart() override {
        // 记录初始基准价格
        basePrice_ = upperPrice_;
    }

    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        double currentPrice = ctx.currentBar.close;

        // 高于上一个网格价：卖出
        if (currentPrice >= basePrice_ + gridSize_) {
            sell(ctx.portfolio().available() * positionPerGrid_);
            basePrice_ = currentPrice;
        }
        // 低于下一个网格价：买入
        else if (currentPrice <= basePrice_ - gridSize_) {
            buy(ctx.portfolio().available() * positionPerGrid_);
            basePrice_ = currentPrice;
        }
    }

private:
    int gridLevels_ = 10;
    double upperPrice_ = 10.0;
    double lowerPrice_ = 8.0;
    double gridSize_ = 0.2;
    double positionPerGrid_ = 0.1;
    double basePrice_ = 10.0;
};

} // namespace st
