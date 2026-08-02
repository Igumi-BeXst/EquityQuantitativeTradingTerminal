#include "engine/market/market_breadth.h"
#include <algorithm>

namespace st {

MarketBreadthData MarketBreadth::calculate(
    const std::vector<std::pair<StockCode, BarSeries>>& inputs) {
    MarketBreadthData data;

    for (const auto& [code, bars] : inputs) {
        if (bars.size() < 2) continue;

        double prevClose = bars.lookback(1).close;
        double current = bars.current().close;

        // 涨跌统计
        if (current > prevClose) data.advancing++;
        else if (current < prevClose) data.declining++;
        else data.unchanged++;

        // 52周高低（需约250根bar，简化用全部历史高低）
        if (bars.size() >= 20) {
            double high = bars.current().high;
            double low = bars.current().low;
            bool isHigh = true, isLow = true;
            for (int i = 1; i < static_cast<int>(bars.size()); ++i) {
                if (bars.lookback(i).high >= high) isHigh = false;
                if (bars.lookback(i).low <= low) isLow = false;
            }
            if (isHigh) data.newHighs++;
            if (isLow) data.newLows++;
        }
    }
    return data;
}

} // namespace st
