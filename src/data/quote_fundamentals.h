#pragma once

#include "foundation/stock_code.h"

namespace st {

/// 个股基本面快照（行情派生，Data 层自包含，不依赖 Engine 类型）
///
/// 由东财 clist 行情接口填充：市值/股本单位为元/股，换手率/市盈为百分比/倍。
/// 字段不可用（数据源不支持/停牌）时保持 0，UI 侧显示 "—"。
struct QuoteFundamentals {
    StockCode code;
    bool valid = false;

    double turnoverRate = 0.0;      // 换手率 %
    double turnoverRateReal = 0.0;  // 换手率(实) % = 换手率 × 流通股/总股本
    double peStatic = 0.0;          // 市盈(静)
    double peTtm = 0.0;             // 市盈(TTM)（clist 无可靠值 → 常为 0，UI 显示 "—"）
    double marketCap = 0.0;         // 总市值 元
    double floatCap = 0.0;          // 流通市值 元
    double totalShares = 0.0;       // 总股本 股
    double floatShares = 0.0;       // 流通股 股
};

} // namespace st
