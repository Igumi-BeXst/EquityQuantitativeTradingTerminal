#include "engine/journal/trade_journal.h"
#include "foundation/utils/datetime.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace st {

namespace {
/// FNV-1a 64 位哈希 → hex 字符串
std::string fnv1a(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}
std::string dirStr(Direction d) { return d == Direction::Buy ? "B" : "S"; }
}  // namespace

std::string TradeJournalEngine::entryFingerprint(const JournalEntry& e) const {
    return fnv1a(e.code.code() + "|" + utils::toDateTimeString(e.time)
                 + "|" + dirStr(e.direction) + "|" + std::to_string(e.volume));
}

std::string TradeJournalEngine::addEntry(const JournalEntry& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    JournalEntry copy = e;
    copy.id = "J" + std::to_string(nextId_++);
    entries_.push_back(std::move(copy));
    fingerprints_.insert(entryFingerprint(entries_.back()));
    return entries_.back().id;
}

bool TradeJournalEngine::updateEntry(const std::string& id, const JournalEntry& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& it : entries_) {
        if (it.id == id) {
            fingerprints_.erase(entryFingerprint(it));
            it = e;
            it.id = id;                 // 保留原 id
            fingerprints_.insert(entryFingerprint(it));
            return true;
        }
    }
    return false;
}

bool TradeJournalEngine::removeEntry(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == id) {
            fingerprints_.erase(entryFingerprint(*it));
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

void TradeJournalEngine::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    fingerprints_.clear();
}

std::string TradeJournalEngine::appendAuto(const Trade& t, const std::string& strategy) {
    JournalEntry e;
    e.code = t.code;
    e.type = JournalType::AutoTrade;
    e.direction = t.direction;
    e.price = t.price;
    e.volume = t.volume;
    e.fees = t.totalFee;
    e.strategy = strategy;
    e.time = t.time;
    e.name.clear();  // 名称由 UI 侧可选补填

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string fp = entryFingerprint(e);
    if (fingerprints_.count(fp)) return {};      // 重复，跳过
    e.id = "J" + std::to_string(nextId_++);
    entries_.push_back(std::move(e));
    fingerprints_.insert(fp);
    return entries_.back().id;
}

void TradeJournalEngine::restoreEntries(const std::vector<JournalEntry>& entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_ = entries;                          // 保留原 id
    fingerprints_.clear();
    int maxId = 0;
    for (const auto& e : entries_) {
        fingerprints_.insert(entryFingerprint(e));
        if (!e.id.empty() && e.id[0] == 'J') {
            const int n = std::atoi(e.id.c_str() + 1);
            if (n >= maxId) maxId = n;
        }
    }
    nextId_ = maxId + 1;                          // 后续新 id 不冲突
}

} // namespace st
