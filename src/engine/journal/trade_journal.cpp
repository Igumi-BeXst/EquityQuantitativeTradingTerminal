#include "engine/journal/trade_journal.h"
#include "foundation/utils/datetime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <map>

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

// =============================================================================
// JournalStats / computeStats — 纯函数统计（不依赖 UI/线程，可单测）
// =============================================================================

namespace {

/// FIFO 成本批次：买入价含费用摊销
struct CostLot {
    double costPerShare;   // 含买入费摊销的单位成本
    Volume volume;
};

/// 单笔已实现回合（卖出完成匹配时的盈亏）
struct RoundTrip {
    double pnl;
    double totalCost = 0.0;   // 匹配仓位的买入总成本（含买入费），用于计算 pnlPct
    DateTime time;
    std::string codeName;
    StockCode code;
};

/// 对 entries 按 code+type 组内 FIFO 配对，返回已实现回合列表
/// 关键语义：买卖必须同组（同 JournalType）才配对，跨组不配对
std::vector<RoundTrip> computeRoundTrips(const std::vector<JournalEntry>& entries) {
    // 按时间升序排序（副本），保证 FIFO 时间序
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const JournalEntry& a, const JournalEntry& b) { return a.time < b.time; });

    // 按 (code, type) 分队列 — key = "code|type"
    std::map<std::string, std::vector<CostLot>> queues;
    std::vector<RoundTrip> trips;

    for (const auto& e : sorted) {
        std::string key = e.code.fullCode() + "|"
                         + std::to_string(static_cast<int>(e.type));

        if (e.direction == Direction::Buy) {
            // 买入：成本 = 成交额 + 买入费（单位成本里摊销）
            double totalCost = e.price * static_cast<double>(e.volume) + e.fees;
            double cps = (e.volume > 0) ? totalCost / static_cast<double>(e.volume)
                                        : e.price;
            queues[key].push_back({cps, e.volume});
        } else {
            // 卖出：FIFO 从队首扣减，计算回合盈亏
            auto& queue = queues[key];
            Volume needVol = e.volume;
            double roundPnl = 0.0;
            double totalBuyCost = 0.0;  // 累计匹配仓位的买入成本
            bool matched = false;

            while (needVol > 0 && !queue.empty()) {
                auto& lot = queue.front();
                Volume matchedVol = std::min(lot.volume, needVol);
                // 卖出费按成交量比例分摊到本批次
                double sellFeePortion = e.fees
                    * (static_cast<double>(matchedVol) / static_cast<double>(e.volume));
                double pnl = (e.price - lot.costPerShare)
                                 * static_cast<double>(matchedVol)
                             - sellFeePortion;
                roundPnl += pnl;
                totalBuyCost += lot.costPerShare * static_cast<double>(matchedVol);

                lot.volume -= matchedVol;
                needVol -= matchedVol;
                matched = true;

                if (lot.volume == 0) {
                    queue.erase(queue.begin());
                }
            }

            if (matched) {
                trips.push_back({roundPnl, totalBuyCost, e.time, e.name, e.code});
            }
        }
    }

    return trips;
}

/// 从一组 entries 计算单组统计 — 抽出为静态 helper 供 sim/manual/overall/byStrategy 复用
GroupStats groupStatsFor(const std::vector<JournalEntry>& entries) {
    auto trips = computeRoundTrips(entries);
    // 按卖出时间升序排列（computeRoundTrips 已按 entry 时间序处理，trips 自然有序；
    // 显式排序以防条目乱序）
    std::sort(trips.begin(), trips.end(),
              [](const RoundTrip& a, const RoundTrip& b) { return a.time < b.time; });

    GroupStats gs;
    gs.count = static_cast<int>(trips.size());

    double grossProfit = 0.0;
    double grossLoss = 0.0;

    for (const auto& t : trips) {
        if (t.pnl > 0.0) {
            gs.wins++;
            grossProfit += t.pnl;
        } else if (t.pnl < 0.0) {
            gs.losses++;
            grossLoss += std::abs(t.pnl);
        }
        // pnl == 0 不算赢也不算亏
    }

    gs.winRate = (gs.count > 0) ? static_cast<double>(gs.wins) / static_cast<double>(gs.count)
                                : 0.0;
    gs.totalPnl = grossProfit - grossLoss;

    // profitFactor：盈利总额 / 亏损总额；亏损为 0 时返回 0
    if (grossLoss > 0.0) {
        gs.profitFactor = grossProfit / grossLoss;
    } else {
        gs.profitFactor = 0.0;
    }

    // cumPnl + maxDrawdown：按时间升序累加回合盈亏
    double cum = 0.0;
    double peak = 0.0;
    double maxDD = 0.0;
    gs.cumPnl.reserve(trips.size());
    for (const auto& t : trips) {
        cum += t.pnl;
        gs.cumPnl.push_back(cum);
        if (cum > peak) peak = cum;
        double dd = peak - cum;
        if (dd > maxDD) maxDD = dd;
    }
    gs.maxDrawdown = maxDD;

    return gs;
}

}  // anonymous namespace

JournalStats computeStats(const std::vector<JournalEntry>& entries) {
    JournalStats out;

    // 按类型拆分
    std::vector<JournalEntry> sim, manual;
    for (const auto& e : entries) {
        if (e.type == JournalType::AutoTrade) {
            sim.push_back(e);
        } else if (e.type == JournalType::ManualNote) {
            manual.push_back(e);
        }
        // Signal 类型暂时不归入 sim/manual 分组
    }

    out.sim = groupStatsFor(sim);
    out.manual = groupStatsFor(manual);
    out.overall = groupStatsFor(entries);  // 整体重算；内部按 type 分队列，不跨组配对

    // --- 月度收益 ---
    auto allTrips = computeRoundTrips(entries);
    std::map<std::string, double> monthlyMap;
    for (const auto& t : allTrips) {
        std::string ym = utils::toDateString(t.time).substr(0, 7); // "YYYY-MM"
        monthlyMap[ym] += t.pnl;
    }
    for (const auto& [ym, pnl] : monthlyMap) {
        out.monthly.push_back({ym, pnl});
    }
    // std::map 自动按 ym 升序，符合测试预期

    // --- 已实现盈亏（per-code） ---
    std::map<std::string, RealizedPnl> realizedMap;
    std::map<std::string, double> totalCostMap;
    for (const auto& t : allTrips) {
        const std::string key = t.code.fullCode();
        auto& r = realizedMap[key];
        r.code = t.code;
        if (!t.codeName.empty() && r.name.empty()) {
            r.name = t.codeName;
        }
        r.pnl += t.pnl;
        r.roundTrips++;
        totalCostMap[key] += t.totalCost;
    }
    for (auto& [key, r] : realizedMap) {
        if (totalCostMap[key] > 0.0) {
            r.pnlPct = r.pnl / totalCostMap[key] * 100.0;
        }
        out.realized.push_back(std::move(r));
    }

    // --- 按策略分组 ---
    std::map<std::string, std::vector<JournalEntry>> strategyMap;
    for (const auto& e : entries) {
        if (!e.strategy.empty()) {
            strategyMap[e.strategy].push_back(e);
        }
    }
    for (const auto& [name, vec] : strategyMap) {
        out.byStrategy.push_back({name, groupStatsFor(vec)});
    }

    // pairs 留空，Task 3 填充
    return out;
}

} // namespace st
