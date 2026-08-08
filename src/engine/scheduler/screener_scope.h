#pragma once

#include "foundation/stock_code.h"
#include <string>
#include <vector>

namespace st {

class IDataProvider;

/// 选股/抓数据范围解析 — 从 task.target JSON 解析出股票列表
/// target 示例：
///   {"scope":"all"}                        → 全部 A 股
///   {"scope":"sector","sector":"BK0475"}   → 某板块（TDX 板块指数代码）
///   {"scope":"last"}                       → 复用上次手动选股配置
class ScopeResolver {
public:
    /// 取全部 A 股
    static std::vector<StockCode> allAShares(IDataProvider* provider);

    /// 取某板块成分（TDX 板块指数；v1 简化：板块代码本身作为单标的池）
    static std::vector<StockCode> sectorStocks(IDataProvider* provider,
                                               const std::string& sectorCode);

    /// 解析 target JSON → 股票池
    /// lastConfig：上次手动选股的股票列表（scope=last 时使用；空则回退 all）
    static std::vector<StockCode> resolve(const std::string& targetJson,
                                          IDataProvider* provider,
                                          const std::vector<StockCode>& lastConfig);
};

} // namespace st
