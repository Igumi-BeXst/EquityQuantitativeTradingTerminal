#include "engine/market/market_engine.h"
#include "core/log_manager.h"

namespace st {

MarketEngine::MarketEngine() = default;
MarketEngine::~MarketEngine() = default;

std::vector<std::pair<StockCode, BarSeries>> MarketEngine::loadSeries(
    const std::vector<StockCode>& pool) {
    std::vector<std::pair<StockCode, BarSeries>> result;
    result.reserve(pool.size());
    if (!cache_) return result;

    for (const auto& code : pool) {
        auto bars = cache_->getBars(code, BarPeriod::Daily);
        if (bars.empty()) continue;
        result.emplace_back(code, BarSeries(std::move(bars)));
    }
    return result;
}

std::vector<MarketRankItem> MarketEngine::buildGainersBoard(
    const std::vector<StockCode>& pool, int topN) {
    auto series = loadSeries(pool);
    auto board = MarketScanner::scan(series, 0);
    return MarketScanner::gainers(board, topN);
}

std::vector<MarketRankItem> MarketEngine::buildLosersBoard(
    const std::vector<StockCode>& pool, int topN) {
    auto series = loadSeries(pool);
    auto board = MarketScanner::scan(series, 0);
    return MarketScanner::losers(board, topN);
}

MarketBreadthData MarketEngine::calculateBreadth(const std::vector<StockCode>& pool) {
    auto series = loadSeries(pool);
    return MarketBreadth::calculate(series);
}

} // namespace st
