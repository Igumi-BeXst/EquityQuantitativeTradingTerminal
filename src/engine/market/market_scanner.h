#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <string>
#include <vector>

namespace st {

/// 行情扫描结果 — 单只股票的涨跌幅信息
struct MarketRankItem {
    StockCode code;
    std::string name;
    Price price = 0.0;        // 最新价
    double changePct = 0.0;   // 涨跌幅 (%)
    double turnover = 0.0;    // 换手率 (%)
};

/// 市场扫描器 — 计算股票涨跌幅并排序
///
/// 输入每只股票的日线序列（需至少 2 根 bar），
/// 输出按涨跌幅排序的榜单。
class MarketScanner {
public:
    /// 计算单只股票的涨跌幅
    /// @param bars 日线序列
    /// @return 涨跌幅 (%)；数据不足返回 0
    static double calculateChangePct(const BarSeries& bars);

    /// 从多只股票的 bar 序列生成榜单
    /// @param inputs code → bars
    /// @param topN 取前 N 名（0 = 全部）
    /// @return 按涨跌幅降序
    static std::vector<MarketRankItem> scan(
        const std::vector<std::pair<StockCode, BarSeries>>& inputs, int topN = 0);

    /// 涨幅榜（降序）
    static std::vector<MarketRankItem> gainers(const std::vector<MarketRankItem>& items, int topN = 0);

    /// 跌幅榜（升序）
    static std::vector<MarketRankItem> losers(const std::vector<MarketRankItem>& items, int topN = 0);
};

} // namespace st
