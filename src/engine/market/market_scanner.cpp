#include "engine/market/market_scanner.h"
#include <algorithm>

namespace st {

double MarketScanner::calculateChangePct(const BarSeries& bars) {
    if (bars.size() < 2) return 0.0;
    double prevClose = bars.lookback(1).close;
    if (prevClose <= 0) return 0.0;
    return (bars.current().close / prevClose - 1.0) * 100.0;
}

std::vector<MarketRankItem> MarketScanner::scan(
    const std::vector<std::pair<StockCode, BarSeries>>& inputs, int topN) {
    std::vector<MarketRankItem> items;
    items.reserve(inputs.size());

    for (const auto& [code, bars] : inputs) {
        MarketRankItem item;
        item.code = code;
        item.price = bars.empty() ? 0.0 : bars.current().close;
        item.changePct = calculateChangePct(bars);
        item.turnover = bars.empty() ? 0.0 : bars.current().turnoverRate * 100.0;
        items.push_back(std::move(item));
    }

    // 按涨跌幅降序
    std::sort(items.begin(), items.end(),
        [](const MarketRankItem& a, const MarketRankItem& b) {
            return a.changePct > b.changePct;
        });

    if (topN > 0 && static_cast<size_t>(topN) < items.size()) {
        items.resize(static_cast<size_t>(topN));
    }
    return items;
}

std::vector<MarketRankItem> MarketScanner::gainers(const std::vector<MarketRankItem>& items, int topN) {
    auto sorted = items;
    std::sort(sorted.begin(), sorted.end(),
        [](const MarketRankItem& a, const MarketRankItem& b) {
            return a.changePct > b.changePct;
        });
    if (topN > 0 && static_cast<size_t>(topN) < sorted.size()) {
        sorted.resize(static_cast<size_t>(topN));
    }
    return sorted;
}

std::vector<MarketRankItem> MarketScanner::losers(const std::vector<MarketRankItem>& items, int topN) {
    auto sorted = items;
    std::sort(sorted.begin(), sorted.end(),
        [](const MarketRankItem& a, const MarketRankItem& b) {
            return a.changePct < b.changePct;
        });
    if (topN > 0 && static_cast<size_t>(topN) < sorted.size()) {
        sorted.resize(static_cast<size_t>(topN));
    }
    return sorted;
}

} // namespace st
