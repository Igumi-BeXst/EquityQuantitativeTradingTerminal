#pragma once

#include "foundation/stock_code.h"
#include "foundation/stock_info.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace st {

/// 股票搜索索引 — 支持代码/名称/拼音首字母模糊搜索
/// 用于全局搜索栏 (UI)
class StockSearchIndex {
public:
    /// 构建索引（传入全部股票信息）
    void build(const std::vector<StockInfo>& infos);

    /// 追加单条
    void add(const StockInfo& info);

    /// 模糊搜索，返回匹配的股票，最多 maxResults 条
    std::vector<StockInfo> search(const std::string& query, int maxResults = 10) const;

    /// 索引大小
    size_t size() const { return infos_.size(); }

    void clear() {
        infos_.clear();
        byPinyinInitials_.clear();
        byName_.clear();
    }

private:
    std::vector<StockInfo> infos_;

    // 拼音首字母 → 索引列表（小写）
    std::unordered_map<std::string, std::vector<size_t>> byPinyinInitials_;
    // 名称 → 索引列表
    std::unordered_map<std::string, std::vector<size_t>> byName_;

    static std::string toLower(const std::string& s);
    static bool contains(const std::string& haystack, const std::string& needle);
};

} // namespace st
