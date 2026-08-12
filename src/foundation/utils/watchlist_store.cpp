#include "foundation/utils/watchlist_store.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace st {

using json = nlohmann::json;

std::vector<StockCode> WatchlistStore::load(const std::string& path) {
    std::vector<StockCode> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    json j;
    try { in >> j; } catch (...) { return out; }
    if (!j.is_object() || !j.contains("codes") || !j["codes"].is_array()) return out;
    for (const auto& c : j["codes"]) {
        if (!c.is_string()) continue;
        try {
            StockCode sc(c.get<std::string>());
            if (sc.isValid()) out.push_back(std::move(sc));
        } catch (...) { /* 跳过坏条目 */ }
    }
    return out;
}

void WatchlistStore::save(const std::string& path, const std::vector<StockCode>& codes) {
    json j;
    j["codes"] = json::array();
    for (const auto& c : codes) j["codes"].push_back(c.fullCode());
    std::ofstream out(path, std::ios::binary);
    if (out) out << j.dump(2);
}

} // namespace st
