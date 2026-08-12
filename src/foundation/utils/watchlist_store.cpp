#include "foundation/utils/watchlist_store.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace st {

using json = nlohmann::json;

std::vector<WatchlistStore::Entry> WatchlistStore::load(const std::string& path) {
    std::vector<Entry> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    json j;
    try { in >> j; } catch (...) { return out; }
    if (!j.is_object()) return out;
    // 新版格式 {"items":[{"code","name"},...]}
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& it : j["items"]) {
            if (!it.is_object() || !it.contains("code") || !it["code"].is_string()) continue;
            try {
                Entry e;
                e.code = StockCode(it["code"].get<std::string>());
                if (!e.code.isValid()) continue;
                if (it.contains("name") && it["name"].is_string()) e.name = it["name"].get<std::string>();
                out.push_back(std::move(e));
            } catch (...) { /* 跳过坏条目 */ }
        }
        return out;
    }
    // 旧格式 {"codes":["SH600000",...]}（无名称，name 为空）
    if (j.contains("codes") && j["codes"].is_array()) {
        for (const auto& c : j["codes"]) {
            if (!c.is_string()) continue;
            try {
                Entry e;
                e.code = StockCode(c.get<std::string>());
                if (e.code.isValid()) out.push_back(std::move(e));
            } catch (...) { /* 跳过坏条目 */ }
        }
    }
    return out;
}

void WatchlistStore::save(const std::string& path, const std::vector<Entry>& entries) {
    json j;
    j["items"] = json::array();
    for (const auto& e : entries) {
        json item;
        item["code"] = e.code.fullCode();
        if (!e.name.empty()) item["name"] = e.name;
        j["items"].push_back(std::move(item));
    }
    std::ofstream out(path, std::ios::binary);
    if (out) out << j.dump(2);
}

} // namespace st
