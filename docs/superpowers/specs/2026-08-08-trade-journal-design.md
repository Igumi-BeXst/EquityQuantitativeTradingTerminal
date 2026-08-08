# 交易日志（Trade Journal）设计 — P10 第九轮

日期：2026-08-08
状态：待审阅

## 1. 背景与目标

用户工作流：**StockTerminal 用于量化测试（模拟交易跑策略），真钱交易在外部软件手动操作，测试好之后按量化策略手动实盘**。

交易日志的核心价值 = 「**策略信号（模拟）→ 手动执行（实盘）→ 复盘对比**」的闭环：
- 记录**模拟交易**成交（自动落库）
- 记录**手动实盘**操作（手动录入）
- 对比两者：执行差距、胜率差距、收益曲线差距

## 2. 数据模型

### JournalEntry（`engine/journal/trade_journal.h`）

```cpp
struct JournalEntry {
    std::string  id;              // 唯一 ID（内部生成）
    StockCode    code;            // 标的
    std::string  name;            // 名称（冗余存，离线可用）
    JournalType  type;            // ManualNote 手动实盘 / AutoTrade 模拟自动落库
    Direction    direction;       // Buy / Sell
    Price        price;           // 成交价
    Volume       volume;          // 数量
    Amount       fees;            // 总费用（自动算或手动改）
    std::string  strategy;        // 关联策略名（模拟自动带；手动可手填）
    std::string  note;            // 注解
    DateTime     time;            // 成交时间
};
```

`JournalType` 复用现有枚举 [enums.h:59](src/foundation/enums.h#L59)。`Direction`/`Price`/`Volume`/`Amount`/`DateTime` 复用 foundation types。

### 费率配置（复用 `FeeConfig`）

日志窗口内「费率设置」对话框 → 存 `FeeConfig`（[fee_calculator.h:20](src/engine/backtest/fee_calculator.h#L20)），**四样均可编辑**，默认标准 A 股：

| 项 | 默认值 | 说明 |
|----|--------|------|
| commissionRate | 0.00025 | 佣金万 2.5 |
| minCommission | 5.0 | 最低 5 元 |
| stampTaxRate | 0.0005 | 印花税卖出 0.05%（2023 减半后标准；修正引擎旧默认 0.001） |
| transferFeeRate | 0.00002 | 过户费双向十万分之二 |

持久化到 `configDir/journal_config.json`（`FeeConfig` 序列化）。

> 注：现有 `FeeConfig::defaultAShare()` 印花税为 0.001（千分之一），属历史遗留。日志导入/手动录入用标准 0.0005；不动回测引擎的默认值（范围外）。

## 3. 引擎层

### TradeJournalEngine（`engine/journal/trade_journal.{h,cpp}`）

- `addEntry / updateEntry / removeEntry / clear`（含 ID 生成 + 去重）
- `entries()` 只读访问；`setFees(const FeeConfig&)` / `fees()`
- `importTrade(const Trade&)` — 模拟引擎成交 → JournalEntry（AutoTrade，strategy 字段留空待面板填）
- `importTrades(const std::vector<Trade>&)` — 批量导入（初始化兜底）
- `entryFingerprint(const JournalEntry&)` — 指纹去重
- `appendAuto(const Trade&, const std::string& strategy)` — 供面板回调调用（内部查重）

### 对比统计 JournalStats（纯函数）

输入 `const std::vector<JournalEntry>&` → 输出统计结构。全部纯函数，可单测。

- **总体**：胜率 / 总盈亏 / 盈亏比 / 最大回撤 / 交易数 / 累计盈亏曲线（按时间排序累加金额）
- **按类型分组**：模拟（AutoTrade） vs 实盘（ManualNote）各自的 胜率/盈亏/交易数/曲线
- **逐笔精确配对**（见 §4）：`std::vector<PairRow>`
- **持仓成本**：按 code 合并买卖 → 当前持仓 + 已实现盈亏
- **月度收益**：按年-月分组盈亏
- **按策略分组**：每个策略名的 胜率/盈亏/交易数

`PairRow`：
```cpp
struct PairRow {
    StockCode code;         // 标的
    Direction direction;    // 方向（同方向配对）
    double    priceDiff;    // 实盘价 − 模拟价
    double    diffPct;      // 价差/模拟价 × 100
    Price     simPrice;     // 模拟成交价
    Price     manualPrice;  // 实盘成交价
    Volume    matchedVol;   // 本次配对数
    DateTime  simTime;      // 模拟成交时间
    DateTime  manualTime;   // 实盘成交时间
};
```

## 4. 逐笔精确配对算法（FIFO 数量分解）

**目标**：按股票对齐「模拟买卖流」与「实盘买卖流」，逐笔算出执行差距。不用「最近时间」简化法。

**算法**（纯函数 `pairManualVsSim(manual, sim) -> std::vector<PairRow>`）：

1. **分组**：按 `code` 分组（模拟 / 实盘各自按时间升序）。
2. **方向流**：每组内把买卖按时间拆成「方向段」。买入段、卖出段各自独立 FIFO。
3. **数量分解匹配**（每个方向段内）：
   - 双指针 i（实盘）、j（模拟），都指向该方向段数组开头。
   - 每次取 `take = min(manual[i].volume - usedM, sim[j].volume - usedS)`；
   - 生成一条 `PairRow`（matchedVol=take，priceDiff = manual[i].price − sim[j].price）；
   - 消耗两侧数量，`usedM`/`usedS` 满则推进对应指针；
   - 直到某一侧耗尽。
4. **未匹配余量**：单侧剩余不产生 PairRow（不影响统计正确性，配对表只显示成功配对的）。

**性质**：数量完全对齐（一笔实盘 3000 股可配给两笔模拟 1000+2000）；买卖方向各自独立；同代码内时间序保证 FIFO 语义。

**测试夹具**（`tests/test_engine/test_trade_journal.cpp`）：
- 1 对 1：价差/差距%数值
- 1 对 多（实盘 3000 vs 模拟 1000+2000 → 2 条 PairRow，数量恰好拆分）
- 方向独立：买入配对不影响卖出配对
- 数量不对齐：一侧多出，只配到短侧
- 无匹配（实盘有、模拟无）→ 空
- 多股票分组隔离

## 5. PaperTradeEngine 接入（自动落库）

**改 `engine/paper_trade/paper_trade_engine.{h,cpp}`**（最小侵入）：
- 头文件加：
  ```cpp
  using TradeCallback = std::function<void(const Trade&)>;
  void setOnTrade(TradeCallback cb);   // 设置成交回调
  ```
  + 私有成员 `TradeCallback onTrade_;`
- `executeTrade` 里 `trades_.push_back(trade)` 之后：
  ```cpp
  if (onTrade_) onTrade_(trade);
  ```
- 默认空回调，不破坏现有行为（回测引擎不涉及）。

**改 `ui/panels/paper_trade_panel.cpp`**：
- 面板持有 `std::shared_ptr<TradeJournalEngine>`（或通过回调注入，见 §7 UI 装配）
- 创建引擎后：
  ```cpp
  engine_->setOnTrade([guard = QPointer<PaperTradePanel>(this),
                       journal = journal_, sel](const Trade& t) {
      // 回调在 IO 线程执行 → 转主线程（QMetaObject::invokeMethod）
      // 落库用 appendAuto(t, 当前选中策略名)
  });
  ```
- 用 QPointer 守卫：面板销毁后回调 no-op。回调里拿当前策略名（面板 state 成员）。

**指纹去重**：`appendAuto` 内部用 `entryFingerprint`（见下）查重，重复则跳过。

### 指纹设计（§2 确认）

```cpp
/// 指纹 = FNV-1a(代码|时间|方向|数量) — 轻量稳定，进程内去重足够
std::string TradeJournalEngine::entryFingerprint(const JournalEntry& e) const {
    return std::to_string(fnv1a(e.code.code() + "|" + toDateTimeString(e.time)
                 + "|" + dirStr(e.direction) + "|" + std::to_string(e.volume)));
}
```
（项目无 sha256 依赖；指纹只在进程内用，重启后从已载入日志重建指纹集，故无需加密级哈希。）
- **不含价格**：滑点导致成交价微变不会误判成新成交。
- **时间在指纹内**：同一次运行中同一信号成交时间戳固定 → 指纹稳定；引擎重启 ID 重置不影响。
- 落库前查 `std::set<std::string> fingerprints_`（内存 + 载入时重建），在则跳过。

## 6. UI — JournalWindow（独立窗口）

顶部「日志(&L)」菜单 →「交易日志(&J)…」→ `openJournalWindow()`（仿 FundsWindow：`WA_DeleteOnClose`，QPointer 安全异步）。

**JournalWindow : QMainWindow**，两个 tab：

### 交易记录 tab
- `QTableWidget`：时间 / 代码 / 名称 / 类型（模拟=蓝徽标 / 实盘=红徽标）/ 方向 / 价格 / 数量 / 费用 / 策略 / 注解
- 工具栏：新建 / 编辑 / 删除 / 清空 / 筛选（按 类型 / 股票 / 策略 / 方向）
- 新建/编辑对话框：StockSearchBar 搜代码 + 方向/价格/数量 + 费用（自动算 + 可改）+ 策略下拉（可手填）+ 注解
  - 费用自动算：`FeeCalculator(fees_)` 对临时 `Trade` 计算 → 显示总费用 → 可覆盖

### 对比回顾 tab
- 统计卡（总体：胜率/总盈亏/盈亏比/最大回撤/交易数）
- 模拟 vs 实盘两组统计卡（各自胜率/盈亏/交易数）
- **收益曲线双序列叠加**：复用 `EquityCurveWidget`（多序列，模拟累计盈亏 vs 实盘累计盈亏，不同颜色）
- **逐笔配对表**：`PairRow` 表格（代码/方向/模拟价/实盘价/价差/差距%/数量/时间）
- 月度收益表、持仓成本表、按策略表现表
- 数据变化自动重算

## 7. UI 装配（MainWindow）

- `MainWindow` 持有 `std::shared_ptr<TradeJournalEngine> journal_`（生命周期同 MainWindow，保证模拟落库目标稳定存在）
- `openJournalWindow()` 创建 JournalWindow，传入 journal_
- 模拟自动落库的 journal 注入路径：
  - QuantWindow 创建 PaperTradePanel 时，MainWindow 把 journal_ 传入；或
  - PaperTradePanel 暴露 `setJournal(std::shared_ptr<TradeJournalEngine>)`，QuantWindow 构造时设置
  - 选后者（更内聚，QuantWindow 已有面板装配逻辑）

## 8. 测试计划

`tests/test_engine/test_trade_journal.cpp`（当前 328 → 目标 ~355）：

| 组 | 用例 |
|----|------|
| CRUD | 增删改查、clear |
| 导入 | Trade→Entry 字段映射 |
| 费率 | FeeCalculator 对四种费率项、最低佣金、印花税卖出/过户双向 |
| 统计 | 胜率/盈亏比/最大回撤/交易数 |
| 持仓 | 买卖合并 → 当前持仓/已实现盈亏 |
| 月度 | 年-月分组 |
| 类型分组 | 模拟 vs 实盘统计隔离 |
| 精确配对 | §4 全部夹具 |
| 指纹 | 同指纹跳过 / 不同指纹通过 |
| 曲线 | 累计盈亏序列累加 |
| 引擎回调 | PaperTradeEngine onTrade 触发、空回调安全 |
| store | JSON load/save（含损坏文件回退空） |

## 9. 风险与对策

| 风险 | 对策 |
|------|------|
| 回调在 IO 线程落库 → 线程安全 | appendAuto 内部锁（`std::mutex`），面板用 QMetaObject 转主线程 |
| 改 PaperTradeEngine 头文件 → 增量构建陈旧对象（已知记忆模式） | clean rebuild |
| 指纹误判（同秒同向同量两笔真交易） | 极罕见，接受；手动删除即可 |
| 精确配对算法复杂度 | 纯函数 + 完整单测夹具；UI 只消费结果 |
| 印花税旧默认 0.001 被误用 | 日志用独立 FeeConfig 默认 0.0005，不动回测默认 |

## 10. 范围外（v1 不做）

- 回测引擎结果导入日志（回测有独立绩效面板）
- 配对表导出 / CSV
- 实盘信号实时推送（用户实盘在外部软件）
- 净值曲线（无初始资金概念）
