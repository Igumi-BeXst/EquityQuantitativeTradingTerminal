#include "data/stock_search_index.h"
#include <algorithm>
#include <cctype>

namespace st {

std::string StockSearchIndex::toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

bool StockSearchIndex::contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void StockSearchIndex::build(const std::vector<StockInfo>& infos) {
    clear();
    for (const auto& info : infos) {
        add(info);
    }
}

void StockSearchIndex::add(const StockInfo& info) {
    if (!info.isValid()) return;
    size_t idx = infos_.size();
    infos_.push_back(info);

    if (!info.pinyinInitials.empty()) {
        byPinyinInitials_[toLower(info.pinyinInitials)].push_back(idx);
    }
    if (!info.name.empty()) {
        byName_[info.name].push_back(idx);
    }
}

std::vector<StockInfo> StockSearchIndex::search(const std::string& query, int maxResults) const {
    std::vector<StockInfo> result;
    if (query.empty()) return result;

    std::string q = toLower(query);
    size_t seenCount = 0;

    // 1. 精确代码匹配（优先）
    for (size_t i = 0; i < infos_.size(); ++i) {
        const auto& info = infos_[i];
        if (contains(toLower(info.code.code()), q)) {
            result.push_back(info);
            if (++seenCount >= static_cast<size_t>(maxResults)) return result;
        }
    }

    // 2. 名称匹配
    for (size_t i = 0; i < infos_.size(); ++i) {
        const auto& info = infos_[i];
        if (contains(info.name, q)) {
            result.push_back(info);
            if (++seenCount >= static_cast<size_t>(maxResults)) return result;
        }
    }

    // 3. 拼音首字母匹配（如 "gzmt"）
    for (size_t i = 0; i < infos_.size(); ++i) {
        const auto& info = infos_[i];
        if (contains(toLower(info.pinyinInitials), q)) {
            result.push_back(info);
            if (++seenCount >= static_cast<size_t>(maxResults)) return result;
        }
    }

    // 4. 拼音全拼匹配（如 "guzhoumaotai"）
    for (size_t i = 0; i < infos_.size(); ++i) {
        const auto& info = infos_[i];
        if (contains(toLower(info.pinyin), q)) {
            result.push_back(info);
            if (++seenCount >= static_cast<size_t>(maxResults)) return result;
        }
    }

    return result;
}

} // namespace st
