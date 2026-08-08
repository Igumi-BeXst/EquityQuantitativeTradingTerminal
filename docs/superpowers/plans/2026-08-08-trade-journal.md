# 交易日志（Trade Journal）实现计划 — P10 第九轮

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现「模拟 vs 实盘复盘对比」的交易日志：模拟成交自动落库 + 手动实盘录入 + 精确配对/统计/收益曲线对比。

**Architecture:** 引擎层（`engine/journal/trade_journal.{h,cpp}` 数据模型 + 统计 + 精确配对 + JSON store）纯函数可单测；`PaperTradeEngine` 加最小侵入 `onTrade` 回调自动落库；UI 层独立窗口（仿 FundsWindow）两个 tab（交易记录 / 对比回顾）。

**Tech Stack:** C++17, Qt 6.11 (Widgets), nlohmann/json, GoogleTest, CMake + vcpkg。

## Global Constraints

- 分层严格：UI → Intelligence → Engine → Core → Data → Foundation；引擎层不得 include UI
- 安全异步：禁裸 `this` 捕获；`++gen_` 守卫 + `ThreadPool::submitIO` + `QPointer` + `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
- 编译零警告：每次修改后 `cmake --build --preset with-qt` 零错误零警告
- 改头文件后必须 `--clean-first` 全量重建（项目记忆：陈旧对象致堆/栈损坏）
- 费率：印花税标准 0.0005（卖出）、过户费 0.00002（双向）、佣金默认 0.00025 + 最低 5 元——日志用独立默认，不动回测 `FeeConfig::defaultAShare()`
- 测试：`ctest --preset default` 全绿，当前 328 → 目标 ~355
- 中文注释/UI 文案；命名遵循项目惯例（`utils::`、`StockCode`、`Direction`）

---

### Task 1: 引擎数据模型 + TradeJournalEngine CRUD + 指纹去重

**Files:**
- Create: `src/engine/journal/trade_journal.h`
- Create: `src/engine/journal/trade_journal.cpp`
- Create: `tests/test_engine/test_trade_journal.cpp`
- Modify: `src/CMakeLists.txt`（st_engine 加 `engine/journal/trade_journal.cpp`）
- Modify: `tests/CMakeLists.txt`（test_engine 加 `test_engine/test_trade_journal.cpp`）

**Interfaces:**
- Consumes: `JournalType`（[foundation/enums.h:59](src/foundation/enums.h#L59)）、`Direction`、`StockCode`、`DateTime` + `utils::toDateTimeString`/`now`、`foundation/order.h` 的 `Trade`、`engine/backtest/fee_calculator.h` 的 `FeeConfig`/`FeeCalculator`/`Trade`
- Produces: `st::JournalEntry`、`st::TradeJournalEngine`（`addEntry/updateEntry/removeEntry/clear/entries/setFees/fees/appendAuto/entryFingerprint/restoreEntries`）

- [ ] **Step 1: 写数据模型头文件 `trade_journal.h`**

```cpp
#pragma once

#include "foundation/enums.h"
#include "foundation/order.h"
#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "engine/backtest/fee_calculator.h"
#include <mutex>
#include <string>
#include <vector>

namespace st {

/// 交易日志条目 — 模拟自动落库(AutoTrade) / 手动实盘录入(ManualNote)
struct JournalEntry {
    std::string  id;             // 唯一 ID（内部生成 "J" + 序号）
    StockCode    code;
    std::string  name;
    JournalType  type = JournalType::ManualNote;
    Direction    direction = Direction::Buy;
    Price        price = 0.0;
    Volume       volume = 0;
    Amount       fees = 0.0;     // 总费用（含佣金+印花税+过户费等）
    std::string  strategy;       // 关联策略名（可空）
    std::string  note;           // 注解
    DateTime     time;
};

/// 交易日志引擎 — 增删改查 + 持久化入口 + 模拟成交自动落库
class TradeJournalEngine {
public:
    TradeJournalEngine() = default;

    /// CRUD（手动录入路径；id 由引擎生成）
    std::string addEntry(const JournalEntry& e);      // 返回生成 id
    bool updateEntry(const std::string& id, const JournalEntry& e);
    bool removeEntry(const std::string& id);
    void clear();
    const std::vector<JournalEntry>& entries() const { return entries_; }

    /// 费率（手动录入自动算费用用）
    void setFees(const FeeConfig& cfg) { fees_ = cfg; }
    const FeeConfig& fees() const { return fees_; }

    /// 模拟成交自动落库 — 供 PaperTradeEngine.onTrade 回调（线程安全）
    /// strategy 为当前模拟策略名；内部指纹去重，重复则跳过返回空 id
    std::string appendAuto(const Trade& t, const std::string& strategy);

    /// 指纹 = FNV-1a(代码|时间|方向|数量) — 进程内去重
    std::string entryFingerprint(const JournalEntry& e) const;

    /// 载入恢复 — 保留原 id + 重建指纹集 + 更新 nextId_（供 Store::load）
    void restoreEntries(const std::vector<JournalEntry>& entries);

private:
    std::vector<JournalEntry> entries_;
    std::set<std::string> fingerprints_;   // 已入库指纹（防重）
    FeeConfig fees_ = standardFees();
    int nextId_ = 1;
    mutable std::mutex mutex_;

    /// 日志标准 A 股费率 — 佣金万2.5/最低5 + 印花税卖出0.0005(2023减半后) + 过户费双向0.00002
    /// 不用 FeeConfig::defaultAShare()（其印花税仍为 0.001 旧默认，见设计文档）
    static FeeConfig standardFees() {
        FeeConfig cfg;                       // 继承默认：佣金0.00025/最低5/过户0.00002
        cfg.stampTaxRate = 0.0005;           // 修正印花税为 2023 减半后标准
        return cfg;
    }
};

} // namespace st
```

注意：`std::set` 需 `#include <set>`；`trade_journal.h` 顶层补充该 include。

- [ ] **Step 2: 写实现 `trade_journal.cpp`**

```cpp
#include "engine/journal/trade_journal.h"
#include "foundation/utils/datetime.h"

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
```

需 `#include <cstdio>`（snprintf）、`#include <cstdint>` 与 `#include <cstdlib>`（atoi）。

- [ ] **Step 3: 写测试 `test_trade_journal.cpp`（CRUD/指纹/费率默认）**

```cpp
#include <gtest/gtest.h>
#include "engine/journal/trade_journal.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {
JournalEntry mkManual(const std::string& code, Direction dir,
                      double price, long vol, const std::string& time) {
    JournalEntry e;
    e.code = StockCode(code);
    e.name = "测试";
    e.type = JournalType::ManualNote;
    e.direction = dir;
    e.price = price;
    e.volume = vol;
    e.time = utils::parseDateTime(time);
    return e;
}
}  // namespace

TEST(TradeJournalTest, CrudAddUpdateRemoveClear) {
    TradeJournalEngine j;
    const auto id = j.addEntry(mkManual("SH600519", Direction::Buy, 1500.0, 100,
                                        "2026-08-01 10:00:00"));
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(j.entries().size(), 1u);

    auto e = mkManual("SH600519", Direction::Sell, 1600.0, 100, "2026-08-02 10:00:00");
    EXPECT_TRUE(j.updateEntry(id, e));
    ASSERT_EQ(j.entries().size(), 1u);
    EXPECT_EQ(j.entries()[0].id, id);
    EXPECT_EQ(j.entries()[0].direction, Direction::Sell);

    EXPECT_TRUE(j.removeEntry(id));
    EXPECT_TRUE(j.entries().empty());

    j.addEntry(mkManual("SH000001", Direction::Buy, 3.0, 1000, "2026-08-03 09:30:00"));
    j.clear();
    EXPECT_TRUE(j.entries().empty());
}

TEST(TradeJournalTest, AppendAutoFields) {
    TradeJournalEngine j;
    Trade t;
    t.id = "T1";
    t.code = StockCode("SZ000001");
    t.direction = Direction::Buy;
    t.price = 10.0;
    t.volume = 500;
    t.amount = 5000.0;
    t.totalFee = 7.5;
    t.time = utils::parseDateTime("2026-08-03 09:30:00");

    const auto id = j.appendAuto(t, "MACross");
    ASSERT_FALSE(id.empty());
    ASSERT_EQ(j.entries().size(), 1u);
    EXPECT_EQ(j.entries()[0].type, JournalType::AutoTrade);
    EXPECT_EQ(j.entries()[0].strategy, "MACross");
    EXPECT_EQ(j.entries()[0].fees, 7.5);
    EXPECT_EQ(j.entries()[0].volume, 500);
}

TEST(TradeJournalTest, AppendAutoDedup) {
    TradeJournalEngine j;
    Trade t;
    t.code = StockCode("SZ000001");
    t.direction = Direction::Buy;
    t.price = 10.0;
    t.volume = 500;
    t.time = utils::parseDateTime("2026-08-03 09:30:00");

    EXPECT_FALSE(j.appendAuto(t, "MACross").empty());
    // 同代码/时间/方向/数量 → 指纹相同 → 跳过
    EXPECT_TRUE(j.appendAuto(t, "MACross").empty());
    EXPECT_EQ(j.entries().size(), 1u);

    // 价格不同但其他相同 → 指纹不含价格 → 仍重复
    t.price = 10.5;
    EXPECT_TRUE(j.appendAuto(t, "MACross").empty());

    // 数量不同 → 指纹不同 → 落库
    t.volume = 600;
    EXPECT_FALSE(j.appendAuto(t, "MACross").empty());
    EXPECT_EQ(j.entries().size(), 2u);
}

TEST(TradeJournalTest, FingerprintIgnoresPriceAndName) {
    TradeJournalEngine j;
    auto a = mkManual("SH600519", Direction::Buy, 1500.0, 100, "2026-08-01 10:00:00");
    auto b = a;
    b.price = 9999.0;
    b.name = "另一个名字";
    EXPECT_EQ(j.entryFingerprint(a), j.entryFingerprint(b));
}

TEST(TradeJournalTest, DefaultFeesStandardAShare) {
    TradeJournalEngine j;
    EXPECT_DOUBLE_EQ(j.fees().commissionRate, 0.00025);
    EXPECT_DOUBLE_EQ(j.fees().minCommission, 5.0);
    EXPECT_DOUBLE_EQ(j.fees().stampTaxRate, 0.0005);   // 2023 减半后标准
    EXPECT_DOUBLE_EQ(j.fees().transferFeeRate, 0.00002);
}
```

- [ ] **Step 4: 跑测试确认失败**

Run: `cmake --build --preset with-qt`（先加 CMake）→ `ctest --preset default`
Expected: `test_trade_journal` 编译/运行，用例 FAIL（`TradeJournalTest` 未编译通过 → 无此类 → 先确认 CMake 加入后能编译）

> 注：本项目 ctest 是分 target 的 `test_engine` 等；`gtest_discover_tests` 会列单个用例。Task 1 先用 `ctest -R "test_engine"` 观察 `TradeJournalTest.*` 全通过（TDD 头文件先不实现会编译错——本任务为新建文件，直接写实现 + 测试一起跑通更贴合项目节奏；失败步验证「CMake 正确接入、测试可发现」即可）。

- [ ] **Step 5: 更新 CMakeLists 两处并跑通**

`src/CMakeLists.txt` st_engine 源列表（`engine/analyzer/custom_index_store.cpp` 后）加：
```cmake
    engine/journal/trade_journal.cpp
```
`tests/CMakeLists.txt` test_engine 源列表（`test_custom_index_store.cpp` 后）加：
```cmake
        test_engine/test_trade_journal.cpp
```

Run: `cmake --build --preset with-qt` → Expected: 零警告零错误
Run: `ctest --preset default -R "TradeJournalTest"` → Expected: 5 个用例全 PASS

- [ ] **Step 6: Commit**

```bash
git add src/engine/journal/trade_journal.h src/engine/journal/trade_journal.cpp
git add tests/test_engine/test_trade_journal.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 交易日志数据模型 + 引擎 CRUD + 指纹去重（模拟成交自动落库基础）"
```

---

### Task 2: JournalStats 统计（总体/分组/持仓/月度/曲线/按策略）

**Files:**
- Modify: `src/engine/journal/trade_journal.h`（加 `JournalStats` 结构 + `computeStats` 纯函数）
- Modify: `src/engine/journal/trade_journal.cpp`
- Modify: `tests/test_engine/test_trade_journal.cpp`

**Interfaces:**
- Consumes: `JournalEntry`（Task 1）
- Produces: `st::JournalStats`（`computeStats(const std::vector<JournalEntry>&)` 纯函数）

- [ ] **Step 1: 头文件加统计结构 + 计算函数声明**

```cpp
/// 单组交易统计（总体 / 模拟 / 实盘 / 单个策略 通用）
struct GroupStats {
    int    count = 0;
    int    wins = 0;
    int    losses = 0;
    double winRate = 0.0;      // 0..1
    double totalPnl = 0.0;     // 已实现 + 浮动（简化：按买卖配对计算）
    double profitFactor = 0.0; // 盈利总额/亏损总额
    double maxDrawdown = 0.0;  // 累计盈亏曲线最大回撤（金额）
    std::vector<double> cumPnl; // 累计盈亏序列（按时间升序）
};

/// 一笔已实现盈亏（按 code 买卖 FIFO 配对）
struct RealizedPnl {
    StockCode code;
    std::string name;
    double pnl = 0.0;           // 已实现盈亏（卖出时结算）
    double pnlPct = 0.0;        // 相对买入成本的百分比
    int    roundTrips = 0;      // 完成的买卖回合数
};

struct MonthlyPnl {
    std::string ym;             // "2026-08"
    double pnl = 0.0;
};

/// 逐笔配对（模拟 ↔ 实盘）
struct PairRow {
    StockCode code;
    Direction direction = Direction::Buy;
    double    priceDiff = 0.0;   // 实盘价 − 模拟价
    double    diffPct = 0.0;     // 价差/模拟价 × 100
    Price     simPrice = 0.0;
    Price     manualPrice = 0.0;
    Volume    matchedVol = 0;
    DateTime  simTime;
    DateTime  manualTime;
};

/// 全量统计
struct JournalStats {
    GroupStats overall;
    GroupStats sim;             // 模拟（AutoTrade）
    GroupStats manual;          // 实盘（ManualNote）
    std::vector<PairRow> pairs;      // 精确配对结果（§Task 3）
    std::vector<RealizedPnl> realized; // 持仓/已实现盈亏
    std::vector<MonthlyPnl> monthly;  // 月度收益
    std::vector<std::pair<std::string, GroupStats>> byStrategy; // 按策略
};

/// 纯函数统计 — 不依赖 UI/线程，可单测
JournalStats computeStats(const std::vector<JournalEntry>& entries);
```

- [ ] **Step 2: 实现 computeStats**

算法要点（写入 `trade_journal.cpp`）：
- **组内回合**：回合（一买一卖）**只在同组内配对**——模拟组内买卖配对、实盘组内买卖配对、总体 = 两组回合的并集。**不跨组配对**（模拟买 + 实盘卖不构成回合），这是「模拟 vs 实盘对比」的核心语义。
- **方向盈亏**：卖出笔 `pnl = (sellPrice − buyCost) × vol − sellFees`；买入笔记成本。用 per-code FIFO：`map<code, vector<(cost,vol)>>`，卖出时从队首扣减，`pnl += (sellPrice − cost)*min(vol,needVol)`，扣除费用。买入费计入成本（成本 = 成交额 + 买入费）。
- **winRate**：按「回合」算——每次卖出完成一个回合，`pnl>0` 算赢。无回合时 winRate=0。
- **profitFactor**：盈利回合总和 / 亏损回合总和（亏损=0 时返回 0）。
- **cumPnl**：按 time 升序累加每笔的「回合盈亏」；未平仓持仓不算入曲线（简化：只算已实现）。maxDrawdown = cum 序列峰值 − 当前值 的最大值。
- **monthly**：按 `toDateString(time).substr(0,7)` 分组，累加已实现盈亏（组内回合）；**输出按 ym 升序排序**（测试假设 `monthly[0]` = 最早月份）。
- **realized**：per-code FIFO 完成回合汇总 → RealizedPnl；未平仓剩余 → 当前持仓（v1 只输出已实现，`RealizedPnl` 含 roundTrips）。
- **byStrategy**：按 `strategy` 非空分组，各自 GroupStats（同样组内回合）。
- **pairs**：由 `pairManualVsSim`（Task 3）填充；Task 2 先留空（`pairs` 为空），Task 3 填实现。

GroupStats 填法：对给定子集 entries，跑同一套「回合计算」得到 count/wins/losses/totalPnl/profitFactor/maxDrawdown/cumPnl。为复用，把「子集回合计算」抽成文件内静态 helper `static GroupStats groupStatsFor(const std::vector<JournalEntry>&)`（computeStats 是自由函数，不用「私有」措辞）。`computeStats` 内：`out.sim = groupStatsFor(simEntries)`、`out.manual = groupStatsFor(manualEntries)`、`out.overall = groupStatsFor(全部 entries)`——整体重算时买卖仍只组内配对（pairing 逻辑按 code+type 组内进行），结果自然等价于 sim+manual 回合并集。

- [ ] **Step 3: 测试（统计核心）**

```cpp
namespace {
/// 买入 100 股 10 元 → 卖出 100 股 12 元（卖出一笔，费用忽略）
std::vector<JournalEntry> roundTripDone() {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00"));
    return v;
}
}  // namespace

TEST(TradeJournalStatsTest, WinRateAndProfitFactor) {
    auto stats = computeStats(roundTripDone());
    EXPECT_EQ(stats.overall.count, 1);        // 1 个回合
    EXPECT_EQ(stats.overall.wins, 1);
    EXPECT_DOUBLE_EQ(stats.overall.winRate, 1.0);
    EXPECT_NEAR(stats.overall.totalPnl, 200.0, 1e-6);   // (12-10)*100
}

TEST(TradeJournalStatsTest, LossIsLoss) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 8.0, 100, "2026-08-02 09:30:00"));
    auto stats = computeStats(v);
    EXPECT_EQ(stats.overall.wins, 0);
    EXPECT_EQ(stats.overall.losses, 1);
    EXPECT_NEAR(stats.overall.totalPnl, -200.0, 1e-6);
}

TEST(TradeJournalStatsTest, FeeIncludedInCost) {
    std::vector<JournalEntry> v;
    auto b = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    b.fees = 7.5;   // 买入费计入成本
    v.push_back(b);
    auto s = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    s.fees = 7.5;   // 卖出费从收益扣
    v.push_back(s);
    auto stats = computeStats(v);
    EXPECT_NEAR(stats.overall.totalPnl, 200.0 - 15.0, 1e-6);  // 毛利200 − 费用15
}

TEST(TradeJournalStatsTest, SimVsManualGroups) {
    std::vector<JournalEntry> v;
    // 模拟组内完整回合（买+卖 AutoTrade）
    auto sb = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    sb.type = JournalType::AutoTrade;
    auto ss = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    ss.type = JournalType::AutoTrade;
    // 实盘组内完整回合（买+卖 ManualNote）
    auto mb = mkManual("SZ000001", Direction::Buy, 5.0, 200, "2026-08-01 09:31:00");
    auto ms = mkManual("SZ000001", Direction::Sell, 6.0, 200, "2026-08-03 09:31:00");
    v.push_back(sb); v.push_back(ss); v.push_back(mb); v.push_back(ms);
    auto stats = computeStats(v);
    EXPECT_EQ(stats.sim.count, 1);
    EXPECT_EQ(stats.manual.count, 1);
    EXPECT_EQ(stats.overall.count, 2);
    EXPECT_NEAR(stats.sim.totalPnl, 200.0, 1e-6);
    EXPECT_NEAR(stats.manual.totalPnl, 200.0, 1e-6);
}

TEST(TradeJournalStatsTest, CrossGroupBuySellNotPaired) {
    // 买入在实盘组、卖出去模拟组 → 两组都不构成回合
    std::vector<JournalEntry> v;
    auto b = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    auto s = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    s.type = JournalType::AutoTrade;
    v.push_back(b);
    v.push_back(s);
    auto stats = computeStats(v);
    EXPECT_EQ(stats.sim.count, 0);
    EXPECT_EQ(stats.manual.count, 0);
    EXPECT_EQ(stats.overall.count, 0);   // 跨组不配对
}

TEST(TradeJournalStatsTest, MonthlyGrouping) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-07-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-07-02 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 11.0, 100, "2026-08-02 09:30:00"));
    auto stats = computeStats(v);
    ASSERT_EQ(stats.monthly.size(), 2u);
    EXPECT_EQ(stats.monthly[0].ym, "2026-07");
    EXPECT_NEAR(stats.monthly[0].pnl, 200.0, 1e-6);
    EXPECT_NEAR(stats.monthly[1].pnl, 100.0, 1e-6);
}

TEST(TradeJournalStatsTest, CumPnlRises) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-07-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-07-02 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 11.0, 100, "2026-08-02 09:30:00"));
    auto stats = computeStats(v);
    ASSERT_EQ(stats.overall.cumPnl.size(), 2u);
    EXPECT_NEAR(stats.overall.cumPnl.back(), 300.0, 1e-6);
}
```

- [ ] **Step 4: 构建 + 测试**

Run: `cmake --build --preset with-qt` → 零警告零错误
Run: `ctest --preset default -R "TradeJournalStatsTest"` → 6 用例 PASS
Run: `ctest --preset default` → 全绿（Task 1 用例不回归）

- [ ] **Step 5: Commit**

```bash
git add src/engine/journal/trade_journal.h src/engine/journal/trade_journal.cpp tests/test_engine/test_trade_journal.cpp
git commit -m "feat: 交易日志统计（胜率/盈亏/回撤/月度/分组/按策略）"
```

---

### Task 3: 精确配对算法 pairManualVsSim（FIFO 数量分解）

**Files:**
- Modify: `src/engine/journal/trade_journal.h`（`PairRow` 已有，加 `pairManualVsSim` 声明）
- Modify: `src/engine/journal/trade_journal.cpp`
- Modify: `tests/test_engine/test_trade_journal.cpp`

**Interfaces:**
- Consumes: `JournalEntry`、`PairRow`（Task 1/2）
- Produces: `std::vector<PairRow> pairManualVsSim(const std::vector<JournalEntry>& manual, const std::vector<JournalEntry>& sim)`

- [ ] **Step 1: 声明**

```cpp
/// 精确配对 — 按 code 分组 + 买卖方向各自 FIFO + 数量分解对齐
/// manual = 实盘成交（ManualNote），sim = 模拟成交（AutoTrade），均按时间升序
std::vector<PairRow> pairManualVsSim(const std::vector<JournalEntry>& manual,
                                     const std::vector<JournalEntry>& sim);
```

- [ ] **Step 2: 实现（头文件内联声明，cpp 实现）**

```cpp
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
```

需 `#include <map>`、`#include <algorithm>`。时间升序假设：调用方保证（UI 侧按 time 排序；测试构造已排序）。

- [ ] **Step 3: 测试**

```cpp
TEST(TradeJournalPairTest, OneToOne) {
    std::vector<JournalEntry> manual = {
        mkManual("SH600519", Direction::Buy, 10.2, 100, "2026-08-02 09:30:00")};
    std::vector<JournalEntry> sim = {
        mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00")};
    sim[0].type = JournalType::AutoTrade;
    auto rows = pairManualVsSim(manual, sim);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NEAR(rows[0].priceDiff, 0.2, 1e-6);
    EXPECT_NEAR(rows[0].diffPct, 2.0, 1e-6);
    EXPECT_EQ(rows[0].matchedVol, 100);
    EXPECT_EQ(rows[0].direction, Direction::Buy);
}

TEST(TradeJournalPairTest, OneToManyQuantitySplit) {
    // 实盘 3000 股，模拟 1000+2000 → 2 条 PairRow
    std::vector<JournalEntry> manual = {
        mkManual("SH600519", Direction::Buy, 10.2, 3000, "2026-08-02 09:30:00")};
    std::vector<JournalEntry> sim = {
        mkManual("SH600519", Direction::Buy, 10.0, 1000, "2026-08-01 09:30:00"),
        mkManual("SH600519", Direction::Buy, 10.1, 2000, "2026-08-01 10:00:00")};
    sim[0].type = JournalType::AutoTrade;
    sim[1].type = JournalType::AutoTrade;
    auto rows = pairManualVsSim(manual, sim);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].matchedVol, 1000);
    EXPECT_NEAR(rows[0].priceDiff, 0.2, 1e-6);
    EXPECT_EQ(rows[1].matchedVol, 2000);
    EXPECT_NEAR(rows[1].priceDiff, 0.1, 1e-6);
}

TEST(TradeJournalPairTest, DirectionIndependent) {
    std::vector<JournalEntry> manual = {
        mkManual("SH600519", Direction::Buy, 10.2, 100, "2026-08-02 09:30:00"),
        mkManual("SH600519", Direction::Sell, 12.1, 100, "2026-08-05 09:30:00")};
    std::vector<JournalEntry> sim = {
        mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"),
        mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-04 09:30:00")};
    sim[0].type = JournalType::AutoTrade;
    sim[1].type = JournalType::AutoTrade;
    auto rows = pairManualVsSim(manual, sim);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].direction, Direction::Buy);
    EXPECT_EQ(rows[1].direction, Direction::Sell);
    EXPECT_NEAR(rows[1].priceDiff, 0.1, 1e-6);
}

TEST(TradeJournalPairTest, ShortSideMatchedOnly) {
    std::vector<JournalEntry> manual = {
        mkManual("SH600519", Direction::Buy, 10.2, 3000, "2026-08-02 09:30:00")};
    std::vector<JournalEntry> sim = {
        mkManual("SH600519", Direction::Buy, 10.0, 1000, "2026-08-01 09:30:00")};
    sim[0].type = JournalType::AutoTrade;
    auto rows = pairManualVsSim(manual, sim);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].matchedVol, 1000);   // 只配到短侧 1000
}

TEST(TradeJournalPairTest, NoMatchReturnsEmpty) {
    std::vector<JournalEntry> manual = {
        mkManual("SH600519", Direction::Buy, 10.2, 100, "2026-08-02 09:30:00")};
    std::vector<JournalEntry> sim;   // 模拟无成交
    EXPECT_TRUE(pairManualVsSim(manual, sim).empty());
}

TEST(TradeJournalPairTest, MultipleStocksIsolated) {
    std::vector<JournalEntry> manual = {
        mkManual("SH600519", Direction::Buy, 10.2, 100, "2026-08-02 09:30:00"),
        mkManual("SZ000001", Direction::Buy, 5.2, 200, "2026-08-02 09:31:00")};
    std::vector<JournalEntry> sim = {
        mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00")};
    sim[0].type = JournalType::AutoTrade;
    auto rows = pairManualVsSim(manual, sim);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].code.code(), "SH600519");   // SZ000001 无对应模拟 → 不产生
}
```

- [ ] **Step 4: 构建 + 测试**

Run: `cmake --build --preset with-qt` → 零警告零错误
Run: `ctest --preset default -R "TradeJournalPairTest"` → 6 用例 PASS
Run: `ctest --preset default` → 全绿

- [ ] **Step 5: 让 computeStats 填 pairs**

`computeStats` 末尾加（内部按 type 拆两组）：
```cpp
std::vector<JournalEntry> manualEntries, simEntries;
for (const auto& e : entries)
    (e.type == JournalType::AutoTrade ? simEntries : manualEntries).push_back(e);
out.pairs = pairManualVsSim(manualEntries, simEntries);
```

加一个测试确认接线：

```cpp
TEST(TradeJournalStatsTest, ComputeStatsFillsPairs) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.2, 100, "2026-08-02 09:30:00"));
    auto s = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    s.type = JournalType::AutoTrade;
    v.push_back(s);
    auto stats = computeStats(v);
    ASSERT_EQ(stats.pairs.size(), 1u);
    EXPECT_NEAR(stats.pairs[0].priceDiff, 0.2, 1e-6);
}
```

- [ ] **Step 6: Commit**

```bash
git add src/engine/journal/trade_journal.h src/engine/journal/trade_journal.cpp tests/test_engine/test_trade_journal.cpp
git commit -m "feat: 模拟vs实盘逐笔精确配对（FIFO数量分解）"
```

---

### Task 4: TradeJournalStore JSON 持久化 + FeeConfig 序列化

**Files:**
- Create: `src/engine/journal/trade_journal_store.h`
- Create: `src/engine/journal/trade_journal_store.cpp`
- Create: `tests/test_engine/test_trade_journal_store.cpp`
- Modify: `src/CMakeLists.txt`、`tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `JournalEntry`、`TradeJournalEngine`（Task 1）、`FeeConfig`、`utils::Json`（nlohmann）
- Produces: `st::TradeJournalStore`（`load(path) -> TradeJournalEngine`、`save(path, engine)`、`feeConfig`/`setFeeConfig`）

- [ ] **Step 1: 头文件**

```cpp
#pragma once

#include "engine/journal/trade_journal.h"
#include <string>

namespace st {

/// 交易日志持久化 — JSON（configDir/trade_journal.json）
/// 条目数组 + 费率配置。文件缺失/损坏 → 空日志 + 默认费率。
class TradeJournalStore {
public:
    /// 载入 → 填充 engine（含指纹集重建）；失败返回 false 但 engine 为空
    bool load(const std::string& path, TradeJournalEngine& engine) const;

    /// 保存 engine 全量 + 费率；失败返回 false
    bool save(const std::string& path, const TradeJournalEngine& engine) const;

    /// 费率独立读写（可单独存 journal_config 或用同一文件）
    static FeeConfig loadFeeConfig(const std::string& path);
    static bool saveFeeConfig(const std::string& path, const FeeConfig& cfg);
};

} // namespace st
```

- [ ] **Step 2: 实现**

`trade_journal_store.cpp` 要点：
- **JournalEntry ↔ JSON 映射**（nlohmann `to_json`/`from_json`）：id/code.code()/name/type(数字)/direction(数字)/price/volume/fees/strategy/note/time(toDateString)
- `load`：读文件 → parse → `engine.restoreEntries(parsed)`（**保留原 id** + 重建指纹集 + 更新 nextId_，见 Task 1 接口）→ 空文件/异常 → 返回 false（engine 空）
- `save`：`engine.entries()` → 序列化数组 → 写文件（`std::filesystem::create_directories` 父目录，仿 [custom_index_store.cpp:79-85](src/engine/analyzer/custom_index_store.cpp#L79)）
- `loadFeeConfig`：读 `journal_config.json` 的 4 个 double；缺省回退默认 A 股（佣金万2.5/最低5/印花万五/过户十万分之二）

- [ ] **Step 3: 测试 `test_trade_journal_store.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/journal/trade_journal_store.h"
#include "foundation/utils/datetime.h"
#include <cstdio>
#include <filesystem>

using namespace st;

namespace {
std::string tmpPath() {
    const auto* p = std::getenv("TEMP");
    return std::string(p ? p : ".") + "/trade_journal_test_" +
           std::to_string(reinterpret_cast<uintptr_t>(::operator new(0))) + ".json";
}
JournalEntry mk(const std::string& code, Direction d, double price, long vol,
                const std::string& t, const std::string& note = "") {
    JournalEntry e;
    e.code = StockCode(code);
    e.type = JournalType::ManualNote;
    e.direction = d;
    e.price = price;
    e.volume = vol;
    e.fees = 5.0;
    e.note = note;
    e.time = utils::parseDateTime(t);
    return e;
}
}  // namespace

TEST(TradeJournalStoreTest, RoundTrip) {
    const std::string path = tmpPath();
    {
        TradeJournalEngine j;
        j.addEntry(mk("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00", "第一笔"));
        TradeJournalStore store;
        EXPECT_TRUE(store.save(path, j));
    }
    {
        TradeJournalEngine j2;
        TradeJournalStore store;
        EXPECT_TRUE(store.load(path, j2));
        ASSERT_EQ(j2.entries().size(), 1u);
        EXPECT_EQ(j2.entries()[0].code.code(), "SH600519");
        EXPECT_EQ(j2.entries()[0].note, "第一笔");
        EXPECT_DOUBLE_EQ(j2.entries()[0].fees, 5.0);
    }
    std::remove(path.c_str());
}

TEST(TradeJournalStoreTest, MissingFileReturnsEmpty) {
    TradeJournalEngine j;
    TradeJournalStore store;
    EXPECT_FALSE(store.load("/nonexistent/trade_journal.json", j));
    EXPECT_TRUE(j.entries().empty());
}

TEST(TradeJournalStoreTest, CorruptFileFallsBackEmpty) {
    const std::string path = tmpPath();
    { std::FILE* f = std::fopen(path.c_str(), "w"); std::fputs("not json{", f); std::fclose(f); }
    TradeJournalEngine j;
    TradeJournalStore store;
    EXPECT_FALSE(store.load(path, j));
    EXPECT_TRUE(j.entries().empty());
    std::remove(path.c_str());
}

TEST(TradeJournalStoreTest, FeeConfigRoundTrip) {
    const std::string path = tmpPath() + ".cfg.json";
    FeeConfig cfg = FeeConfig::defaultAShare();
    cfg.commissionRate = 0.0001;   // 万1
    cfg.minCommission = 3.0;
    EXPECT_TRUE(TradeJournalStore::saveFeeConfig(path, cfg));
    auto loaded = TradeJournalStore::loadFeeConfig(path);
    EXPECT_DOUBLE_EQ(loaded.commissionRate, 0.0001);
    EXPECT_DOUBLE_EQ(loaded.minCommission, 3.0);
    std::remove(path.c_str());
}

TEST(TradeJournalStoreTest, MissingFeeConfigUsesStandard) {
    auto cfg = TradeJournalStore::loadFeeConfig("/nonexistent/journal_config.json");
    EXPECT_DOUBLE_EQ(cfg.commissionRate, 0.00025);
    EXPECT_DOUBLE_EQ(cfg.stampTaxRate, 0.0005);
}
```

注意 `tmpPath()` 用 `::operator new(0)` 拿随机地址做唯一文件名——GTest 环境下可行；若编译器报错改用固定名 + 每次 remove。更稳妥做法：用 `std::filesystem::temp_directory_path()` + 固定后缀 + 每次测试开始 remove。实现时可调整。

- [ ] **Step 4: 更新 CMakeLists 并跑通**

`src/CMakeLists.txt` st_engine 加：
```cmake
    engine/journal/trade_journal_store.cpp
```
`tests/CMakeLists.txt` test_engine 加：
```cmake
        test_engine/test_trade_journal_store.cpp
```

Run: `cmake --build --preset with-qt` → 零警告零错误
Run: `ctest --preset default -R "TradeJournalStoreTest"` → 5 用例 PASS
Run: `ctest --preset default` → 全绿

- [ ] **Step 5: Commit**

```bash
git add src/engine/journal/ tests/test_engine/test_trade_journal_store.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 交易日志 JSON 持久化 + 费率配置读写"
```

---

### Task 5: PaperTradeEngine onTrade 回调

**Files:**
- Modify: `src/engine/paper_trade/paper_trade_engine.h`
- Modify: `src/engine/paper_trade/paper_trade_engine.cpp`
- Modify: `tests/test_engine/test_paper_trade.cpp`

**Interfaces:**
- Consumes: `Trade`（[order.h:37](src/foundation/order.h#L37)）
- Produces: `PaperTradeEngine::setOnTrade(TradeCallback)`、`PaperTradeEngine::onTrade_`

- [ ] **Step 1: 头文件加回调声明**

在 [paper_trade_engine.h:58](src/engine/paper_trade/paper_trade_engine.h#L58)（`trades()` 后）加：
```cpp
    /// 成交回调 — 每笔成交落库（供交易日志自动记录），可空
    using TradeCallback = std::function<void(const Trade&)>;
    void setOnTrade(TradeCallback cb) { onTrade_ = std::move(cb); }

private:
    ...
    TradeCallback onTrade_;
```

- [ ] **Step 2: 实现 — executeTrade 里 push_back 后触发**

定位 [paper_trade_engine.cpp:189](src/engine/paper_trade/paper_trade_engine.cpp#L189) `trades_.push_back(trade);` 之后加：
```cpp
    if (onTrade_) onTrade_(trade);
```

- [ ] **Step 3: 测试**

`tests/test_engine/test_paper_trade.cpp` 加：
```cpp
TEST(PaperTradeEngineTest, OnTradeCallbackFires) {
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    e.setConfig(cfg);
    int fired = 0;
    Trade captured;
    e.setOnTrade([&](const Trade& t) { ++fired; captured = t; });

    // 直接调 executeTrade 私有 → 通过 onQuote 驱动
    // 用一个简单策略：收到报价即买入
    // —— 更简单：spy on executeTrade 不可行，改用 onQuote + 极简策略
    class BuyOnce : public IStrategy {
    public:
        std::string name() const override { return "BuyOnce"; }
        void initialize() override {}
        void onStart() override {}
        void onStop() override {}
        void onBar(const StrategyContext&) override {
            if (!bought) { buy(100); bought = true; }
        }
        bool bought = false;
    };
    auto s = std::make_shared<BuyOnce>();
    e.addStrategy(s);
    e.start();
    e.onQuote(StockCode("SH600519"), 10.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_GE(fired, 1);
    EXPECT_EQ(captured.code.code(), "SH600519");
    EXPECT_EQ(captured.direction, Direction::Buy);
    e.stop();
}

TEST(PaperTradeEngineTest, NoCallbackIsSafe) {
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    e.setConfig(cfg);
    class BuyOnce : public IStrategy {
    public:
        std::string name() const override { return "BuyOnce"; }
        void initialize() override {}
        void onStart() override {}
        void onStop() override {}
        void onBar(const StrategyContext&) override { if (!b) { buy(100); b = true; } }
        bool b = false;
    };
    auto s = std::make_shared<BuyOnce>();
    e.addStrategy(s);
    e.start();
    e.onQuote(StockCode("SH600519"), 10.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_EQ(e.trades().size(), 1u);
    e.stop();
}
```

（需 `#include "foundation/utils/datetime.h"`。若 BuyOnce 内嵌类在匿名命名空间编译有困难，改在文件顶部定义具名测试策略类。）

- [ ] **Step 4: 构建 + 测试**

Run: `cmake --build --preset with-qt` → 零警告零错误（改了头文件，**注意 clean rebuild**）
Run: `ctest --preset default -R "OnTradeCallback"` → 2 用例 PASS
Run: `ctest --preset default` → 全绿

- [ ] **Step 5: Commit**

```bash
git add src/engine/paper_trade/paper_trade_engine.h src/engine/paper_trade/paper_trade_engine.cpp tests/test_engine/test_paper_trade.cpp
git commit -m "feat: PaperTradeEngine 成交回调 onTrade（交易日志自动落库接入点）"
```

---

### Task 6: JournalWindow UI — 交易记录 tab

**Files:**
- Create: `src/ui/panels/journal_window.h`
- Create: `src/ui/panels/journal_window.cpp`
- Modify: `src/CMakeLists.txt`（st_ui 加 `ui/panels/journal_window.cpp`）

**Interfaces:**
- Consumes: `TradeJournalEngine`（Task 1）、`StockSearchBar`（[stock_search_bar.h](src/ui/panels/stock_search_bar.h)）、`FeeCalculator`、`IDataProvider`（名称补填可选）
- Produces: `st::JournalWindow`（`openChart` 信号、`setJournal`）、`st::JournalEntryDialog`

- [ ] **Step 1: 头文件**

```cpp
#pragma once

#include "foundation/stock_code.h"
#include <QMainWindow>
#include <QString>
#include <memory>

class QTabWidget;
class QTableWidget;
class QLineEdit;

namespace st {

class TradeJournalEngine;
class JournalStats;

/// 交易日志窗口 — 顶部「日志」菜单打开（仿资金窗口独立窗口）
/// tab1 交易记录（CRUD + 筛选），tab2 对比回顾（统计+曲线+配对）
class JournalWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit JournalWindow(std::shared_ptr<TradeJournalEngine> journal,
                           QWidget* parent = nullptr);
    ~JournalWindow() override;

signals:
    void openChart(const StockCode& code, const QString& name);

private:
    void rebuildAll();          // 重读引擎 → 刷两个 tab

    std::shared_ptr<TradeJournalEngine> journal_;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* recordsTable_ = nullptr;
    QLineEdit* filter_ = nullptr;
    // tab2 控件（Task 7）
    class QWidget* statsPage_ = nullptr;
};

} // namespace st
```

- [ ] **Step 2: 实现记录 tab（构造 + 表格 + CRUD 动作 + 筛选）**

构造要点（`journal_window.cpp`）：
- 顶部菜单栏可留空；主区 `QTabWidget`
- **记录 tab**：`QTableWidget(0, 10)` 列：时间/代码/名称/类型/方向/价格/数量/费用/策略/注解；横向 header `ResizeToContents`，末列 Stretch；`NoEditTriggers`
- 工具栏 `QHBoxLayout`：新建/编辑/删除/清空 + 筛选 `QLineEdit`（placeholder「筛选 代码/名称/策略/注解」）
- `rebuildAll()`：`journal_->entries()` 排序（time 降序）→ 填表；类型徽标——item 前景色 `AutoTrade=#42a5f5`（蓝）、`ManualNote=#ef5350`（红）
- **新建/编辑对话框** `JournalEntryDialog`（QDialog）：
  - StockSearchBar（代码/名称）+ 方向 QComboBox（买/卖）+ 价格 QDoubleSpinBox + 数量 QSpinBox + 费用 QDoubleSpinBox（初始自动算）+ 策略 QLineEdit + 注解 QLineEdit
  - 费用自动算：构造临时 `Trade{code, direction, price, volume}` → `FeeCalculator(journal_->fees()).calculateTotal(trade)` → 预填费用框 → 用户可改
  - 价格/数量/方向变化时重算费用（connect valueChanged）
  - `accept()` 时把数据填回 `JournalEntry` 传出
- 新建：`journal_->addEntry(entry)`；编辑：`updateEntry(id, entry)`（选中的行 id 存 `Qt::UserRole`）；删除：确认后 `removeEntry`；清空：确认后 `clear()`
- 筛选：`textChanged` → 按过滤词重填表格（`rebuildAll` 加过滤参数）

- [ ] **Step 3: 加 tab2 占位（Task 7 填）**

`JournalWindow` 构造里先 `statsPage_ = new QWidget; tabs_->addTab(statsPage_, tr("对比回顾"));`，Task 7 替换为真实内容。

- [ ] **Step 4: 更新 CMakeLists 并构建**

`src/CMakeLists.txt` st_ui 源列表加：
```cmake
    ui/panels/journal_window.cpp
```
Run: `cmake --build --preset with-qt` → 零警告零错误（新增头文件，clean rebuild）

- [ ] **Step 5: Commit**

```bash
git add src/ui/panels/journal_window.h src/ui/panels/journal_window.cpp src/CMakeLists.txt
git commit -m "feat: 交易日志窗口 — 交易记录 tab（CRUD + 筛选 + 费用自动算）"
```

---

### Task 7: JournalWindow UI — 对比回顾 tab（统计 + 曲线 + 配对表）

**Files:**
- Modify: `src/ui/panels/journal_window.h`
- Modify: `src/ui/panels/journal_window.cpp`

**Interfaces:**
- Consumes: `JournalStats`/`computeStats`（Task 2）、`PairRow`（Task 3）、`EquityCurveWidget`（[equity_curve_widget.h](src/ui/widgets/equity_curve_widget.h)）
- Produces: 对比回顾 tab 完整内容

- [ ] **Step 1: 头文件加 tab2 成员**

```cpp
    class EquityCurveWidget* curve_ = nullptr;
    class QLabel* overallLabel_ = nullptr;
    class QLabel* simLabel_ = nullptr;
    class QLabel* manualLabel_ = nullptr;
    QTableWidget* pairTable_ = nullptr;
    QTableWidget* monthlyTable_ = nullptr;
    QTableWidget* realizedTable_ = nullptr;
    QTableWidget* strategyTable_ = nullptr;
```

- [ ] **Step 2: 实现对比回顾 tab 布局**

`journal_window.cpp` 构造里替换 `statsPage_` 占位为完整布局（QVBoxLayout）：
- 顶部三行统计卡（QLabel）：`总体 胜率X% 盈亏±Y 回撤Z / 模拟 胜率X% 盈亏±Y / 实盘 胜率X% 盈亏±Y`
- `EquityCurveWidget* curve_`：高度 ~200
  - `curve_->setSeries({ { "模拟", QColor("#42a5f5"), simCum },
                          { "实盘", QColor("#ef5350"), manualCum } })`
  - `simCum`/`manualCum` 从 `stats.sim.cumPnl` / `stats.manual.cumPnl` 取
  - **注意**：累计盈亏金额可为负/大数，`EquityCurveWidget` 的 pad 逻辑（`pad<0.005→0.01`）在金额量级下不合适。Task 7 在刷新前对数据做**轻量适配**：`cumPnl` 为空时喂 `{0}` 单点占位；不为空直接传（Y 轴 hi/lo 由 widget 自动含负值；pad 阈值对金额无碍——负值会扩展 lo，曲线仍可见）。若实测负值区间压扁，则将序列整体抬升到 ≥0（加 min 偏移）再喂入，仅在曲线控件上显示（不改变统计数值）。
- 配对表 `QTableWidget(0, 8)`：代码/方向/模拟价/实盘价/价差/差距%/数量/时间
- 月度收益表 `QTableWidget(0, 2)`：月份/盈亏
- 持仓已实现表 `QTableWidget(0, 4)`：代码/名称/已实现盈亏/回合数
- 按策略表 `QTableWidget(0, 4)`：策略/胜率/盈亏/交易数

`rebuildAll()` 尾部调 `refreshStats()`：`auto stats = computeStats(journal_->entries());` → 填各控件。

- [ ] **Step 3: 构建**

Run: `cmake --build --preset with-qt` → 零警告零错误
Run: `ctest --preset default` → 全绿（引擎测试不回归）

- [ ] **Step 4: Commit**

```bash
git add src/ui/panels/journal_window.h src/ui/panels/journal_window.cpp
git commit -m "feat: 交易日志窗口 — 对比回顾 tab（统计卡+双序列收益曲线+配对表）"
```

---

### Task 8: MainWindow 装配 + PaperTradePanel 自动落库 + 菜单

**Files:**
- Modify: `src/ui/main_window.h`
- Modify: `src/ui/main_window.cpp`
- Modify: `src/ui/panels/quant_window.h`
- Modify: `src/ui/panels/quant_window.cpp`
- Modify: `src/ui/panels/paper_trade_panel.h`
- Modify: `src/ui/panels/paper_trade_panel.cpp`

**Interfaces:**
- Consumes: `JournalWindow`、`TradeJournalEngine`、`TradeJournalStore`、`AppPaths::configDir`（[app_paths.h:24](src/core/app_paths.h#L24)）
- Produces: 顶部「日志」菜单 → JournalWindow；模拟成交自动落库

- [ ] **Step 1: MainWindow 持有 journal + 打开窗口**

`main_window.h`：
- `class JournalWindow;` 前置声明
- 私有成员 `JournalWindow* journalWindow_ = nullptr;` + `std::shared_ptr<TradeJournalEngine> journal_;`
- 私有方法 `void openJournalWindow();`

`main_window.cpp`：
- `initServices()`（或构造末尾）加载日志：
  ```cpp
  journal_ = std::make_shared<TradeJournalEngine>();
  TradeJournalStore store;
  store.load(AppPaths::configDir() + "/trade_journal.json", *journal_);
  ```
- 菜单（`createMenus()`，「资金」菜单后）：
  ```cpp
  auto* journalMenu = menuBar()->addMenu(tr("日志(&L)"));
  journalMenu->addAction(tr("交易日志(&J)…"), this, &MainWindow::openJournalWindow);
  ```
- `openJournalWindow()`（仿 `openFundsWindow()` [main_window.cpp:384](src/ui/main_window.cpp#L384)）：
  ```cpp
  if (!journalWindow_) {
      journalWindow_ = new JournalWindow(journal_);
      journalWindow_->setAttribute(Qt::WA_DeleteOnClose);
      connect(journalWindow_, &QObject::destroyed, this,
              [this] { journalWindow_ = nullptr; });
      connect(journalWindow_, &JournalWindow::openChart, this,
              [this](const StockCode& c, const QString& n) { centralChart_->loadStock(c, n); });
  }
  journalWindow_->show();
  journalWindow_->raise();
  journalWindow_->activateWindow();
  ```

- [ ] **Step 2: PaperTradePanel 自动落库**

`paper_trade_panel.h`：
- 前置声明 `class TradeJournalEngine;`
- public 加 `void setJournal(std::shared_ptr<TradeJournalEngine> journal);`
- 私有成员 `std::shared_ptr<TradeJournalEngine> journal_;`

`paper_trade_panel.cpp`：
- `setJournal` 实现：`journal_ = std::move(journal);`
- 创建引擎处（[paper_trade_panel.cpp:207](src/ui/panels/paper_trade_panel.cpp#L207) `engine_ = std::make_unique<PaperTradeEngine>();` 后）加：
  ```cpp
  const QString strategyName = guard->strategyCombo_->currentText();
  if (guard->journal_) {
      auto j = guard->journal_;
      QPointer<PaperTradePanel> pGuard(guard);
      guard->engine_->setOnTrade([j, pGuard, strategyName](const Trade& t) {
          // 回调在 IO 线程 → 转主线程安全落库（appendAuto 内部有锁，直接调也可，
          // 但为保持 UI 一致性走主线程；guard 失效则 no-op）
          QMetaObject::invokeMethod(pGuard, [j, t, strategyName] {
              j->appendAuto(t, strategyName.toStdString());
          }, Qt::QueuedConnection);
      });
  }
  ```
  （`QMetaObject`、`QPointer`、`Trade` include 需补：`#include <QPointer>`、`#include <QMetaObject>`、`#include "foundation/order.h"`。此代码块在 `invokeMethod(guard, [guard, code, seed...])` 主线程 lambda 内，`guard` 为 QPointer 有效）

- [ ] **Step 3: QuantWindow 传 journal 给面板**

`quant_window.h`：前置声明 `class TradeJournalEngine;`；`setJournal` 或构造传参。
选构造传参最内聚——`quant_window.cpp` 构造加参数 `std::shared_ptr<TradeJournalEngine> journal`；`paperTradePanel_ = new PaperTradePanel(provider, tabs);` 后 `paperTradePanel_->setJournal(journal);`
`main_window.cpp` `openQuantWindow()` 创建 QuantWindow 处传入 `journal_`。

- [ ] **Step 4: 构建 + 全量测试**

Run: `cmake --build --preset with-qt`（改多个头文件 → **`--clean-first` 全量重建**）→ 零警告零错误
Run: `ctest --preset default` → 全绿（目标 ~355）

- [ ] **Step 5: 手动冒烟清单**
- 顶部菜单出现「日志 → 交易日志」，打开独立窗口
- 记录 tab 新建一条手动交易（搜代码、填价格数量、费用自动算、保存显示）
- 模拟交易启动一笔成交 → 打开日志窗口 → 记录 tab 出现 AutoTrade 蓝行
- 再启动一次模拟（引擎重建）→ 同一笔不重复出现（指纹去重）
- 对比回顾 tab：统计卡/双序列曲线/配对表有数据

- [ ] **Step 6: Commit**

```bash
git add src/ui/main_window.h src/ui/main_window.cpp
git add src/ui/panels/quant_window.h src/ui/panels/quant_window.cpp
git add src/ui/panels/paper_trade_panel.h src/ui/panels/paper_trade_panel.cpp
git commit -m "feat: 顶部「日志」菜单 + 模拟成交自动落库接入"
```

---

### Task 9: 文档收尾 + 费率设置对话框

**Files:**
- Modify: `src/ui/panels/journal_window.h`
- Modify: `src/ui/panels/journal_window.cpp`
- Modify: `docs/DEVLOG.md`
- Modify: `docs/changelog.md`
- Modify: `CLAUDE.md`

**Interfaces:**
- Consumes: `FeeConfig`、`TradeJournalStore::loadFeeConfig/saveFeeConfig`、`AppPaths::configDir`
- Produces: 费率设置对话框（四样可编辑 + 保存持久化）

- [ ] **Step 1: 费率设置对话框**

JournalWindow 记录 tab 工具栏加「费率设置」按钮 → `JournalFeeDialog`（QDialog）：
- 四行 QDoubleSpinBox：佣金费率（step 0.00001，decimals 5，range 0~0.01）/ 最低佣金 / 印花税率 / 过户费率
- 初始值 `journal_->fees()`
- 确定 → `journal_->setFees(cfg)` + `TradeJournalStore::saveFeeConfig(AppPaths::configDir() + "/journal_config.json", cfg)` + 重刷表格（费用列按新费率？——已存条目不重算，仅影响后续新建）

- [ ] **Step 2: 构建 + 测试**

Run: `cmake --build --preset with-qt` → 零警告零错误
Run: `ctest --preset default` → 全绿

- [ ] **Step 3: 更新文档**

`docs/DEVLOG.md` 顶部加 P10 第九轮条目（功能/数据/UI/验证/已知限制）
`docs/changelog.md` 顶部加版本说明
`CLAUDE.md`：
- 当前阶段改为 `P10 第八轮 ✅ → P10 第九轮 ✅（交易日志：模拟vs实盘对比）`
- 测试数 328 → 实际数（以 `ctest --preset default` 实跑为准，约 ~355）
- Engine 测试数更新（`Engine: ✅ 112` → 112+27 左右）

- [ ] **Step 4: 终验**

Run: `cmake --build --preset with-qt --clean-first` → 零警告零错误
Run: `ctest --preset default` → 全绿
手动：关窗不崩（回归）

- [ ] **Step 5: Commit**

```bash
git add src/ui/panels/journal_window.h src/ui/panels/journal_window.cpp
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "feat: 交易日志费率设置 + P10 第九轮文档收尾"
```
