#pragma once

#include "foundation/stock_code.h"
#include <string>
#include <vector>

namespace st {

/// 自选股持久化 — watchlist.json（config 目录，仿 scheduled_task_store 的 foundation 层 store 先例）
class WatchlistStore {
public:
    /// 自选股条目（名称缓存：重启后即时显示，无需等异步解析）
    struct Entry {
        StockCode code;
        std::string name;  // 可能为空（旧格式/解析前）
    };

    /// 加载（文件不存在/非法 JSON → 空列表；兼容旧格式 {"codes":[...]}，name 为空）
    static std::vector<Entry> load(const std::string& path);
    /// 保存（UTF-8 JSON：{"items":[{"code":"SH600000","name":"浦发银行"},...]}）
    static void save(const std::string& path, const std::vector<Entry>& entries);
};

} // namespace st
