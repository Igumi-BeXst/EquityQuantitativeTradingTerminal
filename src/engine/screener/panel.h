#pragma once

#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "data/quote_fundamentals.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace st {

/// 面板数据：股票 × 日期 对齐矩阵（行 = 日期，列 = 股票，行主序 t*N+i）
///
/// 由全池日线 + 基本面快照 + 基准指数一次性构建，供面板因子（横截面 RANK、
/// 基准指数、风格因子等）计算；无效值统一 NaN。
struct PanelData {
    std::vector<StockCode> stocks;
    std::vector<DateTime> dates;

    std::vector<double> open;      // 开盘价矩阵
    std::vector<double> high;      // 最高价
    std::vector<double> low;       // 最低价
    std::vector<double> close;     // 收盘价
    std::vector<double> volume;    // 成交量
    std::vector<double> amount;    // 成交额
    std::vector<double> vwap;      // 成交额/成交量（不可用回退典型价）
    std::vector<double> ret;       // 日收益率
    std::vector<double> marketCap; // 总市值 = 总股本 × 当日收盘

    std::vector<double> benchOpen;  // 基准指数开盘（长度 T）
    std::vector<double> benchClose; // 基准指数收盘（长度 T）
    std::vector<double> benchRet;   // 基准指数日收益（长度 T）

    std::vector<double> mkt; // 市场因子：当日全池等权平均收益（长度 T）
    std::vector<double> smb; // 规模因子：小市值组收益 - 大市值组收益（长度 T）
    std::vector<double> hml; // 价值因子：高 B/P 组收益 - 低 B/P 组收益（长度 T）

    bool valid = false;

    [[nodiscard]] size_t rows() const { return dates.size(); }
    [[nodiscard]] size_t cols() const { return stocks.size(); }
    [[nodiscard]] bool empty() const { return rows() == 0 || cols() == 0; }
};

/// 构建面板：日期取全池并集；缺数据格 NaN；基准指数按日期对齐。
///
/// 市值：优先 QuoteFundamentals.totalShares × 当日收盘；总股本缺失时用
/// 快照市值 / 最新收盘反推股本。市场/规模/价值风格因子在内部按日构造
/// （SMB 需 ≥4 只有效市值，HML 需 ≥4 只有效 PB）。
PanelData buildPanel(const std::vector<StockCode>& stocks,
                     const std::vector<std::vector<Bar>>& barsByStock,
                     const std::unordered_map<std::string, QuoteFundamentals>& quotes,
                     const std::vector<Bar>& benchmarkBars);

} // namespace st
