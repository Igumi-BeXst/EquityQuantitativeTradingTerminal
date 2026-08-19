#include "engine/journal/trade_journal_store.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>

namespace st {

/// JournalEntry → JSON（type/direction 存数字；code 存 fullCode；time 存完整日期时间字符串）
void to_json(nlohmann::json& j, const JournalEntry& e) {
    j = {
        {"id",        e.id},
        {"code",      e.code.fullCode()},
        {"name",      e.name},
        {"type",      static_cast<int>(e.type)},
        {"direction", static_cast<int>(e.direction)},
        {"price",     e.price},
        {"volume",    e.volume},
        {"fees",      e.fees},
        {"strategy",  e.strategy},
        {"note",      e.note},
        {"time",      utils::toDateTimeString(e.time)}
    };
}

/// JSON → JournalEntry
void from_json(const nlohmann::json& j, JournalEntry& e) {
    j.at("id").get_to(e.id);
    e.code = StockCode(j.at("code").get<std::string>());
    j.at("name").get_to(e.name);
    e.type      = static_cast<JournalType>(j.at("type").get<int>());
    e.direction = static_cast<Direction>(j.at("direction").get<int>());
    j.at("price").get_to(e.price);
    j.at("volume").get_to(e.volume);
    j.at("fees").get_to(e.fees);
    j.at("strategy").get_to(e.strategy);
    j.at("note").get_to(e.note);
    e.time = utils::parseDateTime(j.at("time").get<std::string>());
}

namespace {

/// 默认 A 股费率（佣金万2.5/最低5/印花万五0.0005/过户十万分之二）
FeeConfig defaultFeeConfig() {
    FeeConfig cfg;
    cfg.commissionRate  = 0.00025;
    cfg.minCommission   = 5.0;
    cfg.stampTaxRate    = 0.0005;
    cfg.transferFeeRate = 0.00002;
    return cfg;
}

/// 写入条目数组（紧凑 JSON，减小体积）
bool writeEntries(const std::string& path, const std::vector<JournalEntry>& entries) {
    try {
        std::filesystem::path p(path);
        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entries) {
            nlohmann::json je;
            to_json(je, e);
            arr.push_back(std::move(je));
        }
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << arr.dump(-1);   // 紧凑 JSON，不缩进
        return static_cast<bool>(ofs);
    } catch (const std::exception&) {
        return false;
    }
}

/// 读取条目数组；文件缺失/损坏返回空数组
std::vector<JournalEntry> readEntries(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return {};
    try {
        auto j = nlohmann::json::parse(ifs);
        if (!j.is_array()) return {};
        std::vector<JournalEntry> entries;
        for (const auto& item : j) {
            JournalEntry e;
            from_json(item, e);
            entries.push_back(std::move(e));
        }
        return entries;
    } catch (const std::exception&) {
        return {};
    }
}

} // anonymous namespace

bool TradeJournalStore::load(const std::string& path, TradeJournalEngine& engine) const {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    try {
        auto j = nlohmann::json::parse(ifs);
        if (!j.is_array()) return false;
        std::vector<JournalEntry> entries;
        for (const auto& item : j) {
            JournalEntry e;
            from_json(item, e);
            entries.push_back(std::move(e));
        }
        engine.restoreEntries(entries); // 保留原 id + 重建指纹集 + 更新 nextId_
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool TradeJournalStore::save(const std::string& path, const TradeJournalEngine& engine,
                             size_t maxEntries) const {
    auto entries = engine.entries();
    if (entries.size() <= maxEntries) {
        return writeEntries(path, entries);
    }

    // 超出上限：把最旧的 excess 条归档到 <path>.archive，主文件只保留最近 maxEntries 条
    const size_t archiveCount = entries.size() - maxEntries;
    std::vector<JournalEntry> archived(entries.begin(), entries.begin() + archiveCount);
    std::vector<JournalEntry> remaining(entries.begin() + archiveCount, entries.end());

    const std::string archivePath = path + ".archive";
    auto existing = readEntries(archivePath);
    std::set<std::string> ids;
    for (const auto& e : existing) ids.insert(e.id);
    for (auto& e : archived) {
        if (ids.insert(e.id).second) {
            existing.push_back(std::move(e));
        }
    }

    return writeEntries(archivePath, existing) && writeEntries(path, remaining);
}

FeeConfig TradeJournalStore::loadFeeConfig(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return defaultFeeConfig();
    try {
        auto j = nlohmann::json::parse(ifs);
        FeeConfig cfg;
        cfg.commissionRate  = j.value("commissionRate",  0.00025);
        cfg.minCommission   = j.value("minCommission",   5.0);
        cfg.stampTaxRate    = j.value("stampTaxRate",    0.0005);
        cfg.transferFeeRate = j.value("transferFeeRate", 0.00002);
        return cfg;
    } catch (const std::exception&) {
        return defaultFeeConfig();
    }
}

bool TradeJournalStore::saveFeeConfig(const std::string& path, const FeeConfig& cfg) {
    try {
        std::filesystem::path p(path);
        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }
        nlohmann::json j;
        j["commissionRate"]  = cfg.commissionRate;
        j["minCommission"]   = cfg.minCommission;
        j["stampTaxRate"]    = cfg.stampTaxRate;
        j["transferFeeRate"] = cfg.transferFeeRate;
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2);
        return static_cast<bool>(ofs);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace st
