#pragma once

#include "foundation/stock_code.h"
#include <string>
#include <vector>

namespace st {

/// 自选股持久化 — watchlist.json（config 目录，仿 scheduled_task_store 的 foundation 层 store 先例）
class WatchlistStore {
public:
    /// 加载（文件不存在/非法 JSON/缺 codes 字段 → 空列表）
    static std::vector<StockCode> load(const std::string& path);
    /// 保存（UTF-8 JSON：{"codes": [fullCode...]}）
    static void save(const std::string& path, const std::vector<StockCode>& codes);
};

} // namespace st
