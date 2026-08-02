#pragma once

#include "engine/market/market_scanner.h"
#include "engine/market/market_breadth.h"
#include "data/data_cache.h"
#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <vector>
#include <memory>

namespace st {

/// 行情引擎 — 聚合市场扫描、市场宽度
///
/// 提供涨幅榜/跌幅榜、市场情绪统计等功能。
class MarketEngine {
public:
    MarketEngine();
    ~MarketEngine();

    void setDataCache(DataCache* cache) { cache_ = cache; }

    /// 从股票池生成涨幅榜（依赖缓存中的日线数据）
    std::vector<MarketRankItem> buildGainersBoard(
        const std::vector<StockCode>& pool, int topN = 30);

    /// 跌幅榜
    std::vector<MarketRankItem> buildLosersBoard(
        const std::vector<StockCode>& pool, int topN = 30);

    /// 市场宽度统计
    MarketBreadthData calculateBreadth(const std::vector<StockCode>& pool);

private:
    /// 加载股票池的日线序列
    std::vector<std::pair<StockCode, BarSeries>> loadSeries(
        const std::vector<StockCode>& pool);

    DataCache* cache_ = nullptr;
};

} // namespace st
