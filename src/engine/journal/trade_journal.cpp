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
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        JournalEntry copy = e;
        copy.id = "J" + std::to_string(nextId_++);
        entries_.push_back(std::move(copy));
        fingerprints_.insert(entryFingerprint(entries_.back()));
        id = entries_.back().id;
    }
    if (onChange_) onChange_();
    return id;
}

bool TradeJournalEngine::updateEntry(const std::string& id, const JournalEntry& e) {
    bool updated = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& it : entries_) {
            if (it.id == id) {
                fingerprints_.erase(entryFingerprint(it));
                it = e;
                it.id = id;                 // 保留原 id
                fingerprints_.insert(entryFingerprint(it));
                updated = true;
                break;
            }
        }
    }
    if (updated && onChange_) onChange_();
    return updated;
}

bool TradeJournalEngine::removeEntry(const std::string& id) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == id) {
                fingerprints_.erase(entryFingerprint(*it));
                entries_.erase(it);
                removed = true;
                break;
            }
        }
    }
    if (removed && onChange_) onChange_();
    return removed;
}

void TradeJournalEngine::clear() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        fingerprints_.clear();
    }
    if (onChange_) onChange_();
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

    std::string id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string fp = entryFingerprint(e);
        if (fingerprints_.count(fp)) return {};   // 重复跳过——不触发回调
        e.id = "J" + std::to_string(nextId_++);
        entries_.push_back(std::move(e));
        fingerprints_.insert(fp);
        id = entries_.back().id;
    }
    if (onChange_ && !id.empty()) onChange_();
    return id;
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

/// 按类型拆分 sim/manual 两组 — 统计段与配对段共用同一拆分逻辑
/// AutoTrade → sim；ManualNote → manual；Signal → 不进任何一组（不参与统计/配对）
void splitForStats(const std::vector<JournalEntry>& entries,
                   std::vector<JournalEntry>& sim,
                   std::vector<JournalEntry>& manual) {
    for (const auto& e : entries) {
        if (e.type == JournalType::AutoTrade) {
            sim.push_back(e);
        } else if (e.type == JournalType::ManualNote) {
            manual.push_back(e);
        }
        // Signal 类型暂时不归入 sim/manual 分组
    }
}

}  // anonymous namespace

// =============================================================================
// collectTradeMarks / deriveHoldings — K线持仓标注数据源（纯函数，可单测）
// =============================================================================

std::vector<TradeMark> collectTradeMarks(const std::vector<JournalEntry>& entries,
                                         const StockCode& code) {
    std::vector<TradeMark> out;
    for (const auto& e : entries) {
        if (e.code != code) continue;
        TradeMark m;
        m.code = e.code;
        m.name = e.name;
        m.type = e.type;
        m.direction = e.direction;
        m.price = e.price;
        m.volume = e.volume;
        m.fees = e.fees;
        m.strategy = e.strategy;
        m.note = e.note;
        m.time = e.time;
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(),
              [](const TradeMark& a, const TradeMark& b) { return a.time < b.time; });
    return out;
}

namespace {

/// FIFO 剩余持仓推导（单一类型内）：返回剩余量+加权成本
struct DerivedLot {
    Volume quantity = 0;
    double totalCost = 0.0;  // 剩余批次含费用总成本
};
void deriveForType(std::vector<JournalEntry> group, DerivedLot& lot) {
    std::sort(group.begin(), group.end(),
              [](const JournalEntry& a, const JournalEntry& b) { return a.time < b.time; });
    std::vector<std::pair<double, Volume>> queue;  // (含费单位成本, 剩余量)
    for (const auto& e : group) {
        if (e.direction == Direction::Buy) {
            double cps = (e.volume > 0)
                ? (e.price * static_cast<double>(e.volume) + e.fees)
                      / static_cast<double>(e.volume)
                : e.price;
            queue.push_back({cps, e.volume});
        } else {
            Volume need = e.volume;
            while (need > 0 && !queue.empty()) {
                auto& lotFront = queue.front();
                Volume take = std::min(lotFront.second, need);
                lotFront.second -= take;
                need -= take;
                if (lotFront.second == 0) queue.erase(queue.begin());
            }
            // 超出部分忽略（不跌负）
        }
    }
    lot.quantity = 0;
    lot.totalCost = 0.0;
    for (const auto& [cps, vol] : queue) {
        lot.quantity += vol;
        lot.totalCost += cps * static_cast<double>(vol);
    }
}

}  // anonymous namespace

std::vector<HoldingLine> deriveHoldings(const std::vector<JournalEntry>& entries,
                                        const StockCode& code) {
    std::vector<HoldingLine> out;
    for (JournalType type : {JournalType::AutoTrade, JournalType::ManualNote}) {
        std::vector<JournalEntry> group;
        for (const auto& e : entries) {
            if (e.code == code && e.type == type) group.push_back(e);
        }
        if (group.empty()) continue;
        DerivedLot lot;
        deriveForType(std::move(group), lot);
        if (lot.quantity > 0) {
            HoldingLine h;
            h.code = code;
            h.type = type;
            h.quantity = lot.quantity;
            h.avgCost = lot.totalCost / static_cast<double>(lot.quantity);
            out.push_back(std::move(h));
        }
    }
    return out;
}

JournalStats computeStats(const std::vector<JournalEntry>& entries) {
    JournalStats out;

    // 按类型拆分（AutoTrade → sim，ManualNote → manual，Signal → 不进任何一组）
    std::vector<JournalEntry> sim, manual;
    splitForStats(entries, sim, manual);

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

    // --- 精确配对（模拟 vs 实盘）---
    // 复用与统计段同一拆分（splitForStats）：Signal 不进入配对，避免虚假配对
    out.pairs = pairManualVsSim(manual, sim);

    return out;
}

// =============================================================================
// pairManualVsSim — 模拟 vs 实盘逐笔精确配对（FIFO 数量分解）
// =============================================================================

std::vector<PairRow> pairManualVsSim(const std::vector<JournalEntry>& manual,
                                     const std::vector<JournalEntry>& sim) {
    // 1. 分组：code -> {manual, sim}
    std::map<std::string, std::pair<std::vector<JournalEntry>,
                                    std::vector<JournalEntry>>> byCode;
    for (const auto& e : manual)
        byCode[e.code.code()].first.push_back(e);
    for (const auto& e : sim)
        byCode[e.code.code()].second.push_back(e);

    std::vector<PairRow> rows;
    for (auto& [code, both] : byCode) {
        const auto& ms = both.first;
        const auto& ss = both.second;
        // 每个方向单独 FIFO
        for (Direction dir : {Direction::Buy, Direction::Sell}) {
            std::vector<const JournalEntry*> mSide, sSide;
            for (const auto& e : ms) if (e.direction == dir) mSide.push_back(&e);
            for (const auto& e : ss) if (e.direction == dir) sSide.push_back(&e);
            size_t i = 0, j = 0;
            Volume usedM = 0, usedS = 0;
            while (i < mSide.size() && j < sSide.size()) {
                const Volume rm = mSide[i]->volume - usedM;
                const Volume rs = sSide[j]->volume - usedS;
                const Volume take = std::min(rm, rs);
                PairRow r;
                r.code = mSide[i]->code;
                r.direction = dir;
                r.simPrice = sSide[j]->price;
                r.manualPrice = mSide[i]->price;
                r.priceDiff = mSide[i]->price - sSide[j]->price;
                r.diffPct = sSide[j]->price > 0
                    ? r.priceDiff / sSide[j]->price * 100.0 : 0.0;
                r.matchedVol = take;
                r.simTime = sSide[j]->time;
                r.manualTime = mSide[i]->time;
                rows.push_back(std::move(r));
                usedM += take;
                usedS += take;
                if (usedM >= mSide[i]->volume) { usedM = 0; ++i; }
                if (usedS >= sSide[j]->volume) { usedS = 0; ++j; }
            }
        }
    }
    return rows;
}

} // namespace st
