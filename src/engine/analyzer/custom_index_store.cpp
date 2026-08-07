#include "engine/analyzer/custom_index_store.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace st {

namespace {

using json = nlohmann::json;

json toJson(const CustomIndex& idx) {
    json j;
    j["id"] = idx.id;
    j["name"] = idx.name;
    j["baseValue"] = idx.baseValue;
    if (idx.baseDate.has_value()) j["baseDate"] = utils::toDateString(*idx.baseDate);
    json cs = json::array();
    for (const auto& c : idx.constituents) {
        cs.push_back({
            {"code", c.code.fullCode()},
            {"name", c.name},
            {"weight", c.weight},
        });
    }
    j["constituents"] = std::move(cs);
    return j;
}

std::optional<CustomIndex> fromJson(const json& j) {
    if (!j.is_object()) return std::nullopt;
    CustomIndex idx;
    idx.id = j.value("id", "");
    idx.name = j.value("name", "");
    idx.baseValue = j.value("baseValue", 1000.0);
    if (j.contains("baseDate") && j["baseDate"].is_string()) {
        idx.baseDate = utils::parseDate(j["baseDate"].get<std::string>());
    }
    if (j.contains("constituents") && j["constituents"].is_array()) {
        for (const auto& cj : j["constituents"]) {
            if (!cj.is_object()) continue;
            IndexConstituent c;
            c.code = StockCode(cj.value("code", std::string{}));
            c.name = cj.value("name", std::string{});
            c.weight = cj.value("weight", 0.0);
            if (c.code.isValid()) idx.constituents.push_back(std::move(c));
        }
    }
    if (idx.id.empty() || idx.constituents.empty()) return std::nullopt;
    return idx;
}

}  // namespace

std::vector<CustomIndex> CustomIndexStore::load(const std::string& path) const {
    std::vector<CustomIndex> result;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return result;
    try {
        const json root = json::parse(ifs);
        if (root.is_array()) {
            for (const auto& j : root) {
                if (auto idx = fromJson(j)) result.push_back(std::move(*idx));
            }
        }
    } catch (const std::exception&) {
        return {};
    }
    return result;
}

bool CustomIndexStore::save(const std::string& path,
                            const std::vector<CustomIndex>& indexes) const {
    try {
        std::filesystem::path p(path);
        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }
        json arr = json::array();
        for (const auto& idx : indexes) arr.push_back(toJson(idx));
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << arr.dump(2);
        return static_cast<bool>(ofs);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace st
