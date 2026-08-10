# K线持仓标注 + 交易标记 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把交易日志数据画到 K线/分时图上——模拟+实盘买卖点箭头（红▲买/绿▼卖）、模拟+实盘持仓成本线（青/橙虚线），悬停显示浮框。

**Architecture:** 引擎层新增纯函数（collectTradeMarks / deriveHoldings）从 `TradeJournalEngine.entries()` 提取标注数据（可单测）；UI 层 KLineChart / TimelineChart 各自绘制（按视图隔离、切股清空、切周期保留重定位）；CentralChartWidget 缓存转发；MainWindow 装配日志变更回调自动刷新。

**Tech Stack:** C++17 / Qt 6 / GoogleTest（引擎层纯函数）；TDX K线 bar.time = 周期首日 00:00（对齐语义）。

## Global Constraints

- 分层：UI → Intelligence → Engine → Core → Data → Foundation；引擎层纯函数不依赖 Qt/UI
- 编译零警告：每次修改后 `cmake --build --preset with-qt`（或 build.bat）零错误零警告
- 测试：Foundation/Core/Engine 层写单元测试，`ctest --preset default` 全绿（376 → 386）
- 改头文件后必须 clean rebuild（项目记忆已知模式：陈旧对象导致堆/栈损坏）
- 安全异步：禁裸 this 捕获 → `QPointer` 守卫 + `++gen_` 世代守卫 + `invokeMethod(..., Qt::QueuedConnection)`
- 标注数据源 = `journal_->entries()`（MainWindow 持有 `std::shared_ptr<TradeJournalEngine> journal_`），**不**用 QuantWindow 里的 PaperTradeEngine
- 实盘（ManualNote）也推导持仓成本线（方案 B，用户已确认）；模拟=AutoTrade，实盘=ManualNote，Signal 类型不进标注（与 splitForStats 一致）
- 测试数 376 → **386**（+10 引擎单测）

---

### Task 1: 引擎层 — collectTradeMarks + deriveHoldings + onChange 回调

**Files:**
- Modify: `src/engine/journal/trade_journal.h`
- Modify: `src/engine/journal/trade_journal.cpp`
- Create: `tests/test_engine/test_trade_mark.cpp`
- Modify: `tests/CMakeLists.txt`（test_engine 源列表加 test_trade_mark.cpp）
- Modify: `src/engine/CMakeLists.txt`（无需——trade_mark 并入 trade_journal.cpp）

**Interfaces:**
- Consumes: `JournalEntry`（id/code/name/type/direction/price/volume/fees/strategy/note/time）、`JournalType`、`Direction`、`StockCode`、`Trade`（appendAuto 已存在）
- Produces:
  ```cpp
  struct TradeMark {
      StockCode    code;
      std::string  name;
      JournalType  type = JournalType::AutoTrade;
      Direction    direction = Direction::Buy;
      Price        price = 0.0;
      Volume       volume = 0;
      Amount       fees = 0.0;
      std::string  strategy;
      std::string  note;
      DateTime     time;
  };
  struct HoldingLine {
      StockCode    code;
      JournalType  type = JournalType::AutoTrade;
      Volume       quantity = 0;
      Price        avgCost = 0.0;
  };
  std::vector<TradeMark> collectTradeMarks(const std::vector<JournalEntry>& entries,
                                           const StockCode& code);
  std::vector<HoldingLine> deriveHoldings(const std::vector<JournalEntry>& entries,
                                          const StockCode& code);
  // TradeJournalEngine 新增：
  using ChangeCallback = std::function<void()>;
  void setOnChange(ChangeCallback cb) { onChange_ = std::move(cb); }
  ```
  （TradeMark/HoldingLine 结构 + collectTradeMarks/deriveHoldings 声明放 trade_journal.h；实现放 trade_journal.cpp 匿名命名空间外、`namespace st` 内）

- [ ] **Step 1: 写失败测试**

`tests/test_engine/test_trade_mark.cpp`（用现有 test_trade_journal.cpp 的 StockCode 构造方式——确认后复用 `StockCode("600519")`；`utils::parseDateTime`/`makeDateTime` 构造 time）：

```cpp
#include "engine/journal/trade_journal.h"
#include "foundation/stock_code.h"
#include "gtest/gtest.h"

using namespace st;

namespace {

JournalEntry mkBuy(const std::string& code, double price, int vol,
                   JournalType type, DateTime time, double fees = 0.0) {
    JournalEntry e;
    e.code = StockCode(code);
    e.name = "测试";
    e.type = type;
    e.direction = Direction::Buy;
    e.price = price;
    e.volume = vol;
    e.fees = fees;
    e.time = time;
    return e;
}

JournalEntry mkSell(const std::string& code, double price, int vol,
                    JournalType type, DateTime time, double fees = 0.0) {
    auto e = mkBuy(code, price, vol, type, time, fees);
    e.direction = Direction::Sell;
    return e;
}

DateTime t(int day, int hour = 10, int min = 0) {
    // 2026-08-day hour:min（测试用固定日期）
    std::tm tm{}; tm.tm_year = 126; tm.tm_mon = 7; tm.tm_mday = day;
    tm.tm_hour = hour; tm.tm_min = min; tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace

TEST(TradeMarkTest, CollectFiltersByCodeAndSorts) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(5)),
        mkBuy("000858", 120, 200, JournalType::AutoTrade, t(3)),
        mkSell("600519", 1700, 100, JournalType::AutoTrade, t(8)),
    };
    auto marks = collectTradeMarks(entries, StockCode("600519"));
    ASSERT_EQ(marks.size(), 2u);
    EXPECT_EQ(marks[0].time, t(5));  // 时间升序
    EXPECT_EQ(marks[1].direction, Direction::Sell);
    EXPECT_TRUE(marks[1].time > marks[0].time);
}

TEST(TradeMarkTest, CollectKeepsBothTypes) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkBuy("600519", 1720, 100, JournalType::ManualNote, t(2)),
    };
    auto marks = collectTradeMarks(entries, StockCode("600519"));
    ASSERT_EQ(marks.size(), 2u);
    EXPECT_EQ(marks[0].type, JournalType::AutoTrade);
    EXPECT_EQ(marks[1].type, JournalType::ManualNote);
}

TEST(TradeMarkTest, CollectEmptyInput) {
    EXPECT_TRUE(collectTradeMarks({}, StockCode("600519")).empty());
    EXPECT_TRUE(collectTradeMarks({mkBuy("000858", 1, 1, JournalType::AutoTrade, t(1))},
                                  StockCode("600519")).empty());
}

TEST(TradeMarkTest, HoldingSimBuyWithFees) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1), /*fees=*/30.0),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].type, JournalType::AutoTrade);
    EXPECT_EQ(hs[0].quantity, 100);
    // (1680*100 + 30) / 100 = 1680.30
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.30);
}

TEST(TradeMarkTest, HoldingPartialSell) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 40, JournalType::AutoTrade, t(5)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].quantity, 60);
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.0);  // 成本不变，只减量
}

TEST(TradeMarkTest, HoldingAllSold) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 100, JournalType::AutoTrade, t(5)),
    };
    EXPECT_TRUE(deriveHoldings(entries, StockCode("600519")).empty());
}

TEST(TradeMarkTest, HoldingOverSellIgnored) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 200, JournalType::AutoTrade, t(5)),  // 超出
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].quantity, 0);  // 扣到 0，不跌负
    // 或：卖出全部后无剩余 → 该类型不产出。二选一，实现时定死语义。
    // 本测试按「扣到 0 不跌负」实现 → 剩余量 0，avgCost 保留
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.0);
}

TEST(TradeMarkTest, HoldingBothTypesIndependent) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkBuy("600519", 1720, 100, JournalType::ManualNote, t(2)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 2u);
    // 模拟线
    EXPECT_EQ(hs[0].type, JournalType::AutoTrade);
    EXPECT_EQ(hs[0].quantity, 100);
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.0);
    // 实盘线
    EXPECT_EQ(hs[1].type, JournalType::ManualNote);
    EXPECT_EQ(hs[1].quantity, 100);
    EXPECT_DOUBLE_EQ(hs[1].avgCost, 1720.0);
}

TEST(TradeMarkTest, HoldingManualOnlyBuy) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1720, 100, JournalType::ManualNote, t(2)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].type, JournalType::ManualNote);  // 只出实盘线
}

TEST(TradeMarkTest, HoldingMultiBatchWeighted) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkBuy("600519", 1720, 300, JournalType::AutoTrade, t(3)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].quantity, 400);
    // (1680*100 + 1720*300) / 400 = 1710.0
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1710.0);
}

// —— onChange 回调 ——
TEST(TradeMarkTest, OnChangeFiresOnMutation) {
    TradeJournalEngine eng;
    int fired = 0;
    eng.setOnChange([&fired] { ++fired; });
    eng.addEntry(mkBuy("600519", 1680, 100, JournalType::ManualNote, t(1)));
    EXPECT_EQ(fired, 1);
    eng.removeEntry("J1");  // 需要知道生成的 id —— 改为 addEntry 返回后记录
}
```

注意最后用例：`addEntry` 返回生成的 id（"J1"），改为：

```cpp
TEST(TradeMarkTest, OnChangeFiresOnMutation) {
    TradeJournalEngine eng;
    int fired = 0;
    eng.setOnChange([&fired] { ++fired; });
    const auto id = eng.addEntry(mkBuy("600519", 1680, 100, JournalType::ManualNote, t(1)));
    EXPECT_EQ(fired, 1);
    eng.updateEntry(id, mkBuy("600519", 1700, 100, JournalType::ManualNote, t(2)));
    EXPECT_EQ(fired, 2);
    eng.removeEntry(id);
    EXPECT_EQ(fired, 3);
    eng.clear();
    EXPECT_EQ(fired, 4);
}
```

- [ ] **Step 2: 运行测试验证失败**

```bash
cmake --build --preset with-qt 2>&1 | tail -5
ctest --preset default -R TradeMarkTest
```
Expected: 编译失败（collectTradeMarks/deriveHoldings/setOnChange 未声明）。

- [ ] **Step 3: 实现引擎函数**

`trade_journal.h` 追加（放在 `pairManualVsSim` 声明之后、`TradeJournalEngine` 之前）：

```cpp
/// K线/分时交易标记（模拟/实盘买卖点）
struct TradeMark {
    StockCode    code;
    std::string  name;
    JournalType  type = JournalType::AutoTrade;
    Direction    direction = Direction::Buy;
    Price        price = 0.0;
    Volume       volume = 0;
    Amount       fees = 0.0;
    std::string  strategy;
    std::string  note;
    DateTime     time;
};

/// 当前持仓线（从日志条目按类型 FIFO 推导）
struct HoldingLine {
    StockCode    code;
    JournalType  type = JournalType::AutoTrade;
    Volume       quantity = 0;
    Price        avgCost = 0.0;
};

/// 提取某代码的全部交易标记（按时间升序）
std::vector<TradeMark> collectTradeMarks(const std::vector<JournalEntry>& entries,
                                         const StockCode& code);

/// 推导某代码当前持仓线（按 JournalType 各自独立 FIFO，最多 2 条）
std::vector<HoldingLine> deriveHoldings(const std::vector<JournalEntry>& entries,
                                        const StockCode& code);
```

`trade_journal.h` 的 `TradeJournalEngine` 类加：

```cpp
    /// 变更回调 — entries 增删改/clear/appendAuto 后触发（UI 刷新图表标注用）
    using ChangeCallback = std::function<void()>;
    void setOnChange(ChangeCallback cb) { onChange_ = std::move(cb); }
private:
    ChangeCallback onChange_;   // 无锁：回调仅主线程调用
```

`trade_journal.cpp` 在 `splitForStats` 之后、`computeStats` 之前（`namespace st` 内、匿名命名空间外）实现：

```cpp
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
```

注意 `HoldingOverSellIgnored` 测试：卖出全部后 queue 空 → quantity 0 → **不产出** HoldingLine（`if (lot.quantity > 0)`）。需把该测试断言改为 `EXPECT_TRUE(hs.empty())`——实现语义是「卖出超量/全卖 → 不产出」，与 AllSold 一致。**实现后修正测试**：

```cpp
TEST(TradeMarkTest, HoldingOverSellIgnored) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 200, JournalType::AutoTrade, t(5)),
    };
    EXPECT_TRUE(deriveHoldings(entries, StockCode("600519")).empty());
}
```

`onChange_` 触发：给 `addEntry/updateEntry/removeEntry/clear/appendAuto` 尾部加（锁外调用，避免死锁——appendAuto 锁内 return 需重构）：

```cpp
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
```

`updateEntry` / `removeEntry` / `clear` 同样重构为「锁内改、锁外回调」；`appendAuto`：

```cpp
std::string TradeJournalEngine::appendAuto(const Trade& t, const std::string& strategy) {
    JournalEntry e;
    /* ...填充 e... */
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
```

- [ ] **Step 4: 运行测试验证通过**

```bash
cmake --build --preset with-qt 2>&1 | tail -5
ctest --preset default -R TradeMarkTest
```
Expected: 编译零警告，10 个 TradeMarkTest 全过。全量 ctest 应 376 + 10 = **386**（无回归）。

- [ ] **Step 5: Commit**

```bash
git add src/engine/journal/trade_journal.h src/engine/journal/trade_journal.cpp tests/test_engine/test_trade_mark.cpp tests/CMakeLists.txt
git commit -m "feat: 交易标记/持仓线引擎函数 + 日志变更回调（图表标注数据源）"
```

---

### Task 2: KLineChart 交易标记 + 持仓成本线绘制

**Files:**
- Modify: `src/ui/widgets/kline_chart.h`
- Modify: `src/ui/widgets/kline_chart.cpp`

**Interfaces:**
- Consumes: Task 1 的 `TradeMark`/`HoldingLine`（include `engine/journal/trade_journal.h`）；现有 `barCenterX(i)`/`priceToY(p)`/`priceHi_/priceLo_`/`mainRect_`/`drawCrosshair`/`mouseMoveEvent`
- Produces:
  ```cpp
  void setTradeMarks(const std::vector<TradeMark>& marks,
                     const std::vector<HoldingLine>& holdings);
  ```
  信号：`void tradeMarkHovered(const TradeMark& mark);`（可选 v1 不做，保留在头文件注释说明）

- [ ] **Step 1: 头文件声明**

`kline_chart.h`：include 加 `"engine/journal/trade_journal.h"`；public 加：

```cpp
    /// 设置交易标记 + 持仓成本线（空 = 无；切股/清空时传空；切周期保留）
    void setTradeMarks(const std::vector<TradeMark>& marks,
                       const std::vector<HoldingLine>& holdings);
```

private 加：

```cpp
    void drawTradeMarks(QPainter& p);       // 成本线 + 买卖箭头
    void buildMarkBarIndex();               // 标记 → bar 索引（对齐）
    // 交易标记
    std::vector<TradeMark> tradeMarks_;
    std::vector<HoldingLine> holdings_;
    std::vector<int> markBarIndex_;         // 与 tradeMarks_ 平行：-1 = 不在数据范围
```

- [ ] **Step 2: 实现 setTradeMarks + 对齐**

`kline_chart.cpp`：

```cpp
void KLineChart::setTradeMarks(const std::vector<TradeMark>& marks,
                               const std::vector<HoldingLine>& holdings) {
    tradeMarks_ = marks;
    holdings_ = holdings;
    buildMarkBarIndex();
    update();
}
```

对齐（`buildMarkBarIndex`，日线同日 / 周月周期包含）：由于 `bars_` 的 `time` = 周期首日，交易日期落在 `[bar.time, nextBar.time)` 即归属该 bar。用线性扫描（bars 量级千级，标记少，可接受）：

```cpp
namespace {

/// 两 DateTime 是否同一天（本地时区年/月/日）
bool isSameDate(DateTime a, DateTime b) {
    const std::time_t ta = std::chrono::system_clock::to_time_t(a);
    const std::time_t tb = std::chrono::system_clock::to_time_t(b);
    std::tm ma{}, mb{};
#ifdef _WIN32
    localtime_s(&ma, &ta); localtime_s(&mb, &tb);
#else
    localtime_r(&ta, &ma); localtime_r(&tb, &mb);
#endif
    return ma.tm_year == mb.tm_year && ma.tm_mon == mb.tm_mon && ma.tm_mday == mb.tm_mday;
}

}  // namespace

void KLineChart::buildMarkBarIndex() {
    markBarIndex_.assign(tradeMarks_.size(), -1);
    if (bars_.empty() || tradeMarks_.empty()) return;
    size_t j = 0;
    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        const DateTime mt = tradeMarks_[i].time;
        while (j < bars_.size() && bars_[j].time < mt) ++j;   // 第一个 bar.time >= mt
        if (j >= bars_.size()) { markBarIndex_[i] = -1; continue; }
        if (isSameDate(bars_[j].time, mt)) {                   // 日线/当日
            markBarIndex_[i] = static_cast<int>(j);
        } else if (j > 0 && bars_[j - 1].time < mt) {          // 周/月：归属前一根
            markBarIndex_[i] = static_cast<int>(j - 1);
        } else {
            markBarIndex_[i] = -1;                             // 数据范围外（早于最早 bar）
        }
    }
}
```

注意：周/月 bar 的 time 是周期首日，`mt` 在周期内时 `bars_[j].time >= mt` 不成立（周期首日 < mt），`j-1` 是周期首日那根 → 正确归属。若 TDX 月线首日语义不同（实测校准），调整条件。

- [ ] **Step 3: 量程纳入成本线**

`computeVisibleRange` 主图 hi/lo 计算后、`overlay` 块后（或 overlay 块后）追加：

```cpp
    // 交易标记成本线纳入量程（模拟青 / 实盘橙）
    for (const auto& h : holdings_) {
        if (h.quantity > 0 && h.avgCost > 0) {
            hi = std::max(hi, h.avgCost);
            lo = std::min(lo, h.avgCost);
        }
    }
```

- [ ] **Step 4: 绘制函数**

`paintEvent` 中 `drawAnnotations(p)` 之前插入 `drawTradeMarks(p)`（十字线之下、画线标注之上）：

```cpp
    drawAxes(p);
    drawTradeMarks(p);   // 交易标记（成本线 + 买卖箭头）
    drawAnnotations(p);
    drawCrosshair(p);
```

实现：

```cpp
void KLineChart::drawTradeMarks(QPainter& p) {
    if (bars_.empty()) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    // 持仓成本线（模拟青 #00e5ff / 实盘橙 #ff9800）
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    const QColor kSimColor("#00e5ff");
    const QColor kManualColor("#ff9800");
    for (const auto& h : holdings_) {
        if (h.quantity <= 0 || h.avgCost <= 0) continue;
        const double y = priceToY(h.avgCost);
        if (y < mainRect_.top() - 12 || y > mainRect_.bottom() + 12) continue;
        const QColor color = (h.type == JournalType::AutoTrade) ? kSimColor : kManualColor;
        p.setPen(QPen(color, 1, Qt::DashLine));
        p.drawLine(QPointF(mainRect_.left(), y), QPointF(mainRect_.right(), y));
        const QString label = QStringLiteral("[%1] %2 @ %3")
            .arg(h.type == JournalType::AutoTrade ? tr("模拟") : tr("实盘"))
            .arg(h.quantity).arg(h.avgCost, 0, 'f', 2);
        const int tw = QFontMetrics(f).horizontalAdvance(label);
        p.drawText(QPointF(mainRect_.right() - tw - 4, y - 4), label);
    }

    // 买卖箭头（红 ▲ 买 / 绿 ▼ 卖），按 markBarIndex_
    const double bw = bodyWidth();
    const double bodyHalf = std::min(0.35 * bw, 6.0);
    const QColor kBuyColor("#ff5252");   // 红买
    const QColor kSellColor("#00e676");  // 绿卖
    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        const int bi = markBarIndex_[i];
        if (bi < start || bi >= end) continue;
        const auto& m = tradeMarks_[i];
        const double cx = barCenterX(bi);
        const double y = priceToY(m.price);
        const QColor color = (m.direction == Direction::Buy) ? kBuyColor : kSellColor;
        p.setPen(QPen(color, 1));
        p.setBrush(color);
        // 竖线 + 三角：买朝上（画 bar 上方），卖朝下（画 bar 下方）
        const double up = (m.direction == Direction::Buy) ? -1.0 : 1.0;
        const double len = bodyHalf * 2.2;
        p.drawLine(QPointF(cx, y), QPointF(cx, y + up * len));
        const double ax = cx, ay = y + up * len;
        QPolygonF tri;
        tri << QPointF(ax, ay + up * 4.5)
            << QPointF(ax - 4.5, ay - up * 4.5)
            << QPointF(ax + 4.5, ay - up * 4.5);
        p.drawPolygon(tri);
    }
}
```

- [ ] **Step 5: 悬停浮框追加交易行**

`drawCrosshair` 的 txt 构建后追加（在 OHLC 行之后、box 尺寸计算前）：

```cpp
    // 悬停 K 线若有交易标记 → 浮框追加交易行
    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        if (markBarIndex_[i] != mouseIndex_) continue;
        const auto& m = tradeMarks_[i];
        const QString dir = (m.direction == Direction::Buy) ? tr("买") : tr("卖");
        const QString typ = (m.type == JournalType::AutoTrade) ? tr("模拟") : tr("实盘");
        txt += QStringLiteral("\n%1%2 %3 %4 @ %5")
            .arg(typ).arg(dir)
            .arg(m.volume).arg(QString::fromStdString(m.name))
            .arg(m.price, 0, 'f', 2);
        if (!m.strategy.empty())
            txt += QStringLiteral(" [%1]").arg(QString::fromStdString(m.strategy));
    }
```

- [ ] **Step 6: 切股/清空/切周期语义**

- `loadStock` / `loadBars`：`tradeMarks_.clear(); holdings_.clear(); markBarIndex_.clear();`（切股清空）
- `setPeriod` 后：**不清空**——KLineChart 内部不重取数据（由外部 `loadStock` 重载），标记保留；`setData`（loadStock 异步回调）不碰 tradeMarks_
- `clearOverlay`：不碰交易标记（独立功能）

关键：`buildMarkBarIndex` 需在 bars 更新后重算。`setData`（异步回调主线程更新 bars_）尾部调 `buildMarkBarIndex()`——保证周期/股票加载后标记重对齐。但 `loadStock` 已清空标记，所以重算空列表无害。

- [ ] **Step 7: 编译验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -5
```
Expected: 零警告。手动暂无法验证（Task 5 装配后）。

- [ ] **Step 8: Commit**

```bash
git add src/ui/widgets/kline_chart.h src/ui/widgets/kline_chart.cpp
git commit -m "feat: K线图交易标记（买卖箭头+持仓成本线+悬停浮框）"
```

---

### Task 3: TimelineChart 分时交易标记

**Files:**
- Modify: `src/ui/widgets/time_line_chart.h`
- Modify: `src/ui/widgets/time_line_chart.cpp`

**Interfaces:**
- Consumes: `TradeMark`（include `engine/journal/trade_journal.h`）；`minutesFromOpen(t)`/`xFor(minutes)`/`priceToY`/`mainRect_`/`drawCrosshair`
- Produces:
  ```cpp
  void setTradeMarks(const std::vector<TradeMark>& marks);
  ```

- [ ] **Step 1: 头文件声明**

`time_line_chart.h`：include 加 `"engine/journal/trade_journal.h"`；public 加：

```cpp
    /// 设置交易标记（当日匹配分钟的买卖箭头；空 = 无；切股清空）
    void setTradeMarks(const std::vector<TradeMark>& marks);
```

private 加：

```cpp
    void drawTradeMarks(QPainter& p);
    std::vector<TradeMark> tradeMarks_;
```

- [ ] **Step 2: 实现 setTradeMarks**

`time_line_chart.cpp`：

```cpp
void TimelineChart::setTradeMarks(const std::vector<TradeMark>& marks) {
    tradeMarks_ = marks;
    update();
}
```

- [ ] **Step 3: 绘制**

`paintEvent` 中 `drawOverlayLine(p)` 之后、`drawVolume` 之前插 `drawTradeMarks(p)`（价格层之上）。实现：

```cpp
void TimelineChart::drawTradeMarks(QPainter& p) {
    if (data_.points.empty() || tradeMarks_.empty()) return;
    // 只画当日交易；匹配分钟 → x 坐标
    const std::time_t td = std::chrono::system_clock::to_time_t(data_.date);
    std::tm tmd{}; localtime_s(&tmd, &td);
    const QColor kBuyColor("#ff5252");
    const QColor kSellColor("#00e676");
    for (const auto& m : tradeMarks_) {
        const std::time_t mt = std::chrono::system_clock::to_time_t(m.time);
        std::tm mtm{}; localtime_s(&mtm, &mt);
        if (mtm.tm_year != tmd.tm_year || mtm.tm_mon != tmd.tm_mon ||
            mtm.tm_mday != tmd.tm_mday) continue;   // 非当日不画
        const int mins = minutesFromOpen(m.time);
        if (mins < 0 || mins > 240) continue;
        const double x = xFor(mins);
        const double y = priceToY(m.price);
        const QColor color = (m.direction == Direction::Buy) ? kBuyColor : kSellColor;
        p.setPen(QPen(color, 1));
        p.setBrush(color);
        const double up = (m.direction == Direction::Buy) ? -1.0 : 1.0;
        const double len = 6.0;
        p.drawLine(QPointF(x, y), QPointF(x, y + up * len));
        QPolygonF tri;
        tri << QPointF(x, y + up * len + up * 4.5)
            << QPointF(x - 4.5, y + up * len - up * 4.5)
            << QPointF(x + 4.5, y + up * len - up * 4.5);
        p.drawPolygon(tri);
    }
}
```

注意：`localtime_s` 在非 WIN32 用 `localtime_r`（同 minutesFromOpen 的模式）。分时图的 `mouseIndex_` 是数据点索引，交易标记按分钟独立定位，不依赖 data_.points 对齐（分钟级粒度够）。

- [ ] **Step 4: 悬停浮框追加（可选，v1 分时不追加浮框——数据点索引与交易分钟映射不直接，保持简单）**

v1 决策：分时悬停浮框不追加交易行（K线图浮框已有完整交易信息；分时标记只是视觉提示）。**在头文件注释注明 v2 可加。**

- [ ] **Step 5: 切股清空**

`loadStock` / `loadIntraday` 里 `tradeMarks_.clear()`（切股清空）。

- [ ] **Step 6: 编译验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -5
```

- [ ] **Step 7: Commit**

```bash
git add src/ui/widgets/time_line_chart.h src/ui/widgets/time_line_chart.cpp
git commit -m "feat: 分时图交易标记（当日买卖箭头）"
```

---

### Task 4: CentralChartWidget 缓存转发 + MainWindow 装配

**Files:**
- Modify: `src/ui/widgets/central_chart_widget.h`
- Modify: `src/ui/widgets/central_chart_widget.cpp`
- Modify: `src/ui/main_window.h`
- Modify: `src/ui/main_window.cpp`

**Interfaces:**
- Consumes: Task 2/3 的 KLineChart::setTradeMarks / TimelineChart::setTradeMarks；Task 1 的 `TradeMark`/`HoldingLine`/`setOnChange`；MainWindow `journal_`
- Produces:
  ```cpp
  // CentralChartWidget:
  void setTradeMarks(const std::vector<TradeMark>& marks,
                     const std::vector<HoldingLine>& holdings);
  ```

- [ ] **Step 1: CentralChartWidget 缓存转发**

`central_chart_widget.h`：include 加 `"engine/journal/trade_journal.h"`；public 加：

```cpp
    /// 设置当前股票的交易标记 + 持仓成本线（转发给分时+K线两图）
    /// 内部缓存：loadStock/setPeriod 重载图表后自动重新注入
    void setTradeMarks(const std::vector<TradeMark>& marks,
                       const std::vector<HoldingLine>& holdings);
```

private 加：

```cpp
    void reapplyTradeMarks();
    std::vector<TradeMark> marks_;
    std::vector<HoldingLine> holdings_;
```

`central_chart_widget.cpp`：

```cpp
void CentralChartWidget::setTradeMarks(const std::vector<TradeMark>& marks,
                                       const std::vector<HoldingLine>& holdings) {
    marks_ = marks;
    holdings_ = holdings;
    reapplyTradeMarks();
}

void CentralChartWidget::reapplyTradeMarks() {
    timeline_->setTradeMarks(marks_);
    kline_->setTradeMarks(marks_, holdings_);
}
```

**关键**：`loadStock` / `setPeriod` 内部调用 `timeline_->loadStock(...)` 或 `kline_->loadStock(...)` **之后**追加 `reapplyTradeMarks();`（因为 loadStock 会清空标记，必须重注入；缓存 marks_/holdings_ 保证周期切换后标记重新对齐而非丢失）。`loadCustomIndex` 同样在喂数据后重注入（若外部设置了）。

- [ ] **Step 2: MainWindow 装配**

`main_window.h`：private 加 `void refreshTradeMarks();`

`main_window.cpp`：

- 在 journal_ 初始化后（~line 140 `journal_->setFees(feeCfg);` 后）注册回调：

```cpp
    // 交易日志变更 → 刷新中央图表交易标记（模拟成交落库/手动增删自动更新）
    journal_->setOnChange([this] { refreshTradeMarks(); });
```

- 中央图表加载股票的每处（line 78 / 98 / 202 / 416 / 437 / 459 的 `centralChart_->loadStock(...)` 之后）调用 `refreshTradeMarks()`——为减少重复，可直接在 `refreshTradeMarks` 里调 `centralChart_->setTradeMarks`，而加载点只需触发一次刷新。最简：**在 `CentralChartWidget::loadStock/setPeriod/loadCustomIndex` 内**已有 reapplyTradeMarks 缓存机制，但缓存初始为空。所以 MainWindow 需要在中央图表首次/每次加载股票后喂一次数据。

实现 `refreshTradeMarks`：

```cpp
void MainWindow::refreshTradeMarks() {
    if (!centralChart_) return;
    const StockCode code = centralChart_->currentCode();
    if (!code.isValid()) return;
    const auto entries = journal_->entries();
    const auto marks = collectTradeMarks(entries, code);
    const auto holdings = deriveHoldings(entries, code);
    centralChart_->setTradeMarks(marks, holdings);
}
```

- 加载点：在 centralChart 连接的几个 lambda 里，`centralChart_->loadStock(...)` 后追加 `refreshTradeMarks();`。同时 `MainWindow::MainWindow` 初始化中央图表后不喂（初始无股票，currentCode 无效自动跳过）。
- `loadCustomIndex` 场景：MainWindow 有对应入口（搜 grep "loadCustomIndex"），自定义指数代码是伪代码 "CIxxx"，`collectTradeMarks` 用 `StockCode("CIxxx")` 匹配不到任何日志 → 空标记（无害，指数图无交易标记，符合预期）。

- [ ] **Step 3: 编译验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -5
```
Expected: 零警告。

- [ ] **Step 4: 手动验证**

运行 build.bat 生成的 StockTerminal：
1. 打开模拟交易面板，跑一只股票 → 成交 → 回主图看该股 K线：买卖箭头 + 青色成本线出现
2. 手动录入实盘日志（日志窗口）→ 红标 + 橙色成本线出现
3. 悬停标记 K 线 → 浮框追加交易行
4. 切日/周/月 → 标记保留重定位；切股票 → 清空
5. 切分时 → 当日买卖箭头
6. 删除/清空日志 → 标记消失

- [ ] **Step 5: Commit**

```bash
git add src/ui/widgets/central_chart_widget.h src/ui/widgets/central_chart_widget.cpp src/ui/main_window.h src/ui/main_window.cpp
git commit -m "feat: 中央图表+主窗口装配交易标记（日志变更自动刷新）"
```

---

### Task 5: 文档收尾 + 全量验证

**Files:**
- Modify: `docs/DEVLOG.md`
- Modify: `docs/changelog.md`
- Modify: `CLAUDE.md`（阶段 + 测试数）

- [ ] **Step 1: DEVLOG / changelog / CLAUDE.md 更新**

- `docs/DEVLOG.md` 顶部加 P10 第十一轮条目（需求/实施/验证/已知限制）
- `docs/changelog.md` 加版本说明
- `CLAUDE.md`：阶段标记加「P10 第十一轮 ✅（交易标记：K线+分时买卖箭头 + 模拟/实盘持仓成本线 + 悬停浮框）」；测试数 376 → 386

- [ ] **Step 2: 全量验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -3   # 零警告
ctest --preset default                          # 386 全绿
```
（改头文件多轮后如遇陈旧对象崩溃，`cmake --build --preset with-qt --clean-first` 重建）

- [ ] **Step 3: 收尾提交**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: K线持仓标注+交易标记（P10 第十一轮）文档收尾"
```

- [ ] **Step 4: 终验 + 分支收尾**

构建零警告 + ctest 386 全绿 + 关窗不崩（回归）。完成后用 finishing-a-development-branch 流程决定 merge/push。

---

## 风险与对策

- **周/月线对齐偏差**：bar.time = 周期首日（TDX 语义已确认），`buildMarkBarIndex` 用「第一个 >= mt 的 bar，若日期不同则归前一根」；若 TDX 月线首日/尾日语义实测不符 → 调整条件（实现时可用 sector_calib 类似工具离屏验证）
- **float 精度**：`EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.30)` 对 `(1680*100+30)/100` 应精确；若不同编译单元四舍五入差异 → 改 `EXPECT_NEAR(..., 1e-6)`
- **onChange 回调重入**：回调在锁外调用，无死锁；MainWindow 回调里调 `journal_->entries()`（加锁）→ 安全
- **分时浮框不做交易行**：v1 简化（分钟索引映射复杂），K线浮框已覆盖信息
- **自定义指数**：伪代码匹配不到日志 → 空标记，无害
- **signal 类型条目**：collectTradeMarks 会包含 Signal（日志有该类型），若不想显示可在 collectTradeMarks 过滤 `e.type != JournalType::Signal`——spec 未明确，**默认包含**（与日志全量一致），实现时可加过滤（一行）

## 收尾
- 用户冒烟后 push GitHub；更新 CLAUDE.md/DEVLOG/changelog
