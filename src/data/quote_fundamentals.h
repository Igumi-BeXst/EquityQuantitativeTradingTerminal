#pragma once

#include "foundation/stock_code.h"

namespace st {

/// 个股基本面快照（行情派生，Data 层自包含，不依赖 Engine 类型）
///
/// 由东财行情快照（clist/ulist）填充：市值/股本单位为元/股，比率字段为百分比（%）。
/// 字段不可用（数据源不支持/停牌/该股无该项）时保持 0，UI 侧显示 "—"。
struct QuoteFundamentals {
    StockCode code;
    bool valid = false;

    // —— 交易/股本（行情派生）——
    double turnoverRate = 0.0;      // 换手率 %
    double turnoverRateReal = 0.0;  // 换手率(实) % = 换手率 × 流通股/总股本
    double peTtm = 0.0;             // 市盈(TTM)（部分源无值）
    double marketCap = 0.0;         // 总市值 元
    double floatCap = 0.0;          // 流通市值 元
    double totalShares = 0.0;       // 总股本 股
    double floatShares = 0.0;       // 流通股 股

    // —— 估值/盈利/成长/财务质量（东财行情快照扩展字段）——
    double peDynamic = 0.0;     // 市盈率(动态) 倍（东财 f9）
    double peStatic = 0.0;      // 市盈率(静态) 倍（东财 f115）
    double pb = 0.0;            // 市净率 倍（东财 f23）
    double roe = 0.0;           // 净资产收益率(加权) %（东财 f37）
    double revenue = 0.0;       // 营业总收入 元（东财 f40）
    double revenueYoY = 0.0;    // 营业总收入同比 %（东财 f41）
    double netProfit = 0.0;     // 净利润 元（东财 f45）
    double netProfitYoY = 0.0;  // 净利润同比 %（东财 f46）
    double grossMargin = 0.0;   // 毛利率 %（东财 f49）
    double debtRatio = 0.0;     // 资产负债率 %（东财 f57）
};

} // namespace st
