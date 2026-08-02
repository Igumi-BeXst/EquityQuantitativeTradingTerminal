#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <vector>

namespace st {

/// 市场宽度统计
struct MarketBreadthData {
    int advancing = 0;    // 上涨家数
    int declining = 0;    // 下跌家数
    int unchanged = 0;    // 平盘家数
    int newHighs = 0;     // 创52周新高
    int newLows = 0;      // 创52周新低

    double advanceRatio() const {  // 涨跌比
        if (advancing + declining == 0) return 0.0;
        return static_cast<double>(advancing) /
               static_cast<double>(advancing + declining);
    }
};

/// 市场宽度 — 判断市场整体情绪
///
/// 统计涨跌家数、创新高/新低数，辅助判断牛熊。
class MarketBreadth {
public:
    /// 统计市场宽度
    /// @param inputs code → bars（需足够历史判断52周高低）
    static MarketBreadthData calculate(
        const std::vector<std::pair<StockCode, BarSeries>>& inputs);

    /// 腾落线 ADL = 上涨家数 - 下跌家数（单日）
    static int adl(const MarketBreadthData& data) {
        return data.advancing - data.declining;
    }
};

} // namespace st
