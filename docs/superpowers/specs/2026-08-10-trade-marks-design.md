# K线持仓标注 + 交易标记 — 设计文档

## 背景

需求文档「K线图 9 层渲染系统（K线/指标/指数叠加/持仓标注/交易标记/画线/交互）」中**持仓标注**与**交易标记**两层尚未实现。其余层（K线/指标/指数叠加/画线/交互）均已完成。

用户在 P10 第十轮（定时任务）完成后选定下一轮 = K线持仓标注 + 交易标记，并明确**排除实盘券商 API 接入**。

## 目标

把已有引擎数据画到图表上：

1. **持仓成本线** — 模拟交易当前持仓均价（青色虚线横线 + 持仓量标签）
2. **交易标记** — 模拟 + 实盘买卖点箭头（红 ▲ 买 / 绿 ▼ 卖），悬停显示浮框（日期/方向/价格/数量/类型/策略/盈亏）
3. 视图范围：**日/周/月 K 线 + 分时**都标注，各自独立、切股清标注

## 数据源

统一使用 **TradeJournalEngine.entries()**（MainWindow 直接持有 `std::shared_ptr<TradeJournalEngine> journal_`）：

| 来源 | JournalType | 说明 |
|------|-------------|------|
| 模拟交易 | AutoTrade | PaperTradeEngine 每笔成交经 `onTrade → appendAuto` 自动落库（指纹去重） |
| 实盘手动录入 | ManualNote | JournalWindow 手动录入 |

**理由**：模拟成交已全量自动落库，日志是唯一同时含模拟+实盘、可持久化、且 MainWindow 可直接访问的数据源。PaperTradeEngine 实例在 QuantWindow 内 `std::unique_ptr` 每次开关重建，MainWindow 无法稳定引用——不用它。

## 数据模型（引擎层，纯函数可单测）

在 `src/engine/journal/trade_journal.h` 追加（`trade_mark.cpp` 新增实现，纯 C++17 无 Qt）：

```cpp
/// K线/分时交易标记（模拟/实盘买卖点）
struct TradeMark {
    StockCode    code;
    std::string  name;
    JournalType  type = JournalType::AutoTrade;   // AutoTrade=模拟 / ManualNote=实盘
    Direction    direction = Direction::Buy;
    Price        price = 0.0;
    Volume       volume = 0;
    Amount       fees = 0.0;
    std::string  strategy;   // 关联策略名
    std::string  note;       // 注解
    DateTime     time;
};

/// 当前持仓线（从日志条目按类型 FIFO 推导）
struct HoldingLine {
    StockCode    code;
    JournalType  type = JournalType::AutoTrade;  // AutoTrade=模拟 / ManualNote=实盘
    Volume       quantity = 0;   // 剩余持仓
    Price        avgCost = 0.0;  // 加权平均成本（含买入费摊销）
};

/// 提取某代码的全部交易标记（按时间升序）
std::vector<TradeMark> collectTradeMarks(const std::vector<JournalEntry>& entries,
                                         const StockCode& code);

/// 推导某代码当前持仓线（FIFO：买入入队，卖出从队首扣减；返回剩余批次加权成本）
/// 按 JournalType 各自独立推导（AutoTrade→模拟线 / ManualNote→实盘线），每种类型至多一条；
/// 不匹配任何买入的卖出被忽略（同 computeRoundTrips 语义）
std::vector<HoldingLine> deriveHoldings(const std::vector<JournalEntry>& entries,
                                        const StockCode& code);
```

### collectTradeMarks 语义

- 过滤 `e.code == code`（全类型），按 `time` 升序
- 每条 → `TradeMark`（原样拷贝字段）
- 空/无匹配 → 空 vector

### deriveHoldings 语义（复用 computeRoundTrips 的 FIFO 思路）

- 取 `code` 匹配的全部条目，**按 `JournalType` 分组**（AutoTrade / ManualNote 各自独立推导），组内按时间升序
- 买入：`cps = (price*vol + fees) / vol` 入队（含费用摊销）
- 卖出：FIFO 从队首扣减（不产生标记，仅扣持仓）；卖出量超出持仓的超出部分忽略
- 每组返回剩余批次加权平均 `avgCost`，产出一条 `HoldingLine`；某组无剩余 → 不产出（最多 2 条：模拟 + 实盘）

## 对齐逻辑

| 视图 | 对齐规则 |
|------|----------|
| 日线 | 交易日期 == `bar.time` 当天日期（`utils::dateEquals` / 自定义比较年月日） |
| 周线/月线 | 交易日期落在 `[bar.time, nextBar.time)` 区间（bar.time = 周期首日 00:00，TDX 语义已确认）；最后一根 bar 含其后全部 |
| 分时 | 交易当日 == 分时日期，且按分钟数匹配 `minutesFromOpen(t)`（复用 TimelineChart 现有映射） |

K 线图在 `computeVisibleRange` 或绘制时计算 `mark→barIndex` 映射（线性扫描，bars 数量 ~120 可见 / 全量 ~千级，可接受）。

## UI 集成

### 1. KLineChart（日/周/月）

- 新增成员：`std::vector<TradeMark> tradeMarks_;`、`std::vector<HoldingLine> holdings_;`、`std::vector<int> markBarIndex_;`（绘制时重算）
- 新增 public：
  ```cpp
  /// 设置交易标记 + 当前持仓成本线（空 = 无；切股/清空时传空）
  /// holdings 至多 2 条（模拟 AutoTrade + 实盘 ManualNote），颜色区分
  void setTradeMarks(const std::vector<TradeMark>& marks,
                     const std::vector<HoldingLine>& holdings);
  ```
- 绘制（在 `drawAnnotations` 之后、`drawCrosshair` 之前新增 `drawTradeMarks`）：
  - **成本线**：横线 @ 每条 `holding.avgCost`；模拟=青色虚线、实盘=橙色虚线；右端标签 `[模拟/实盘]持仓 {quantity} @ {avgCost}`；全部纳入 `priceHi_/priceLo_` 量程（computeVisibleRange 里加）
  - **买卖箭头**：红 ▲ 买（画在 bar 上方）/ 绿 ▼ 卖（画在 bar 下方）；`barCenterX(barIndex)` + 箭头 y 由 `priceToY(price)` 上下偏移；箭头小三角 + 竖线（半宽 min(0.35*bodyWidth,6)）
  - 仅画可见区内的标记（避免全量遍历绘制不可见的）
- **悬停浮框**：`mouseMoveEvent` 中 `indexAtX` 命中检查——若悬停在某 bar 且该 bar 有交易标记，在浮框追加交易行（复用现有十字浮框的绘制/布局，颜色按类型）；无标记则维持现有 OHLC 浮框
- **切股/清空**：`loadStock` / `loadBars` / `clearOverlay` 时清 `tradeMarks_`/`holdings_`
- **周期切换**：`setPeriod` 重载数据后保留标记（标记由外部持有方在 loadStock 后 set，周期切换不重新 set——见 CentralChartWidget 转发策略）

### 2. TimelineChart（分时）

- 新增成员：`std::vector<TradeMark> tradeMarks_;`
- 新增 public：`void setTradeMarks(const std::vector<TradeMark>& marks);`
- 绘制（`drawPriceLines` 之后新增 `drawTradeMarks`）：当日匹配分钟的买卖箭头，x 用现有 `xFor(minutesFromOpen(t))`；当日日期不同/无匹配 → 不画
- 悬停浮框同样追加交易行

### 3. CentralChartWidget（转发）

- 新增 public：
  ```cpp
  /// 设置当前股票的交易标记 + 持仓成本线（转发给分时+K线两图）
  void setTradeMarks(const std::vector<TradeMark>& marks,
                     const std::vector<HoldingLine>& holdings);
  ```
- 实现：`timeline_->setTradeMarks(marks); kline_->setTradeMarks(marks, holdings);`
- `loadStock` 时由 MainWindow 装配（见下），本类不直接持有日志引擎——保持与现有 `provider_` 传参风格一致（数据从 MainWindow 喂入）

### 4. MainWindow 装配 + 刷新时机

- `TradeJournalEngine` 加 `std::function<void()> onChange_` 回调（`setOnChange`），`addEntry/updateEntry/removeEntry/clear/appendAuto` 后触发
- MainWindow：
  - `loadStock`/`setPeriod` 或中央图表加载股票后：从 `journal_->entries()` 提取当前代码标记 → `centralChart_->setTradeMarks(...)`
  - 注册 `journal_->setOnChange([this]{ refreshTradeMarks(); })`：重新提取当前代码标记并推送（模拟成交自动落库 / 手动增删后图表自动更新）
  - `refreshTradeMarks()` 幂等（当前代码为空时跳过）

## 测试

新增 `tests/test_engine/test_trade_mark.cpp`：

| 用例 | 断言 |
|------|------|
| collectTradeMarks 过滤代码 | 仅返回匹配代码的标记，时间升序 |
| collectTradeMarks 全类型 | 模拟+实盘都保留，type 正确 |
| collectTradeMarks 空输入 | 空 vector |
| deriveHoldings 模拟买入 | 单笔模拟买入 → 1 条 HoldingLine(type=AutoTrade)，剩余量+成本（含费用摊销） |
| deriveHoldings 买入卖出部分 | 卖出部分扣减 → 剩余量正确、成本不变 |
| deriveHoldings 全部卖出 | 剩余 0 → 该类型不产出 |
| deriveHoldings 卖出超量 | 超出部分忽略 → 剩余不跌破 0 |
| deriveHoldings 模拟实盘各自推导 | 模拟+实盘各自买入 → 2 条 HoldingLine，type 正确 |
| deriveHoldings 只看模拟条目 | ManualNote 组无买入 → 只出模拟线 |
| deriveHoldings 多批次 | 多笔买入加权成本正确 |

预计 +10 例 → 当前 376 → **386**。

## 已知限制 / 决策

- **实盘持仓成本线依赖录入完整性**：实盘 ManualNote 为手动录入，若只记买入不记卖出，FIFO 会误判为「仍在持有」，成本线可能与真实券商持仓不符——这是方案 B 的固有风险（用户已知并选择）。仅当手动买卖记录完整时，实盘成本线才准确；模拟线因引擎自动落库而始终可靠
- **点击箭头跳转详情**：v1 不做，仅悬停浮框（YAGNI；后续可加信号 `tradeMarkHovered` → 主窗口联动日志窗口）
- **浮框数据来源**：标记浮框读的是已落库日志（appendAuto 即时落库，无延迟）
- **无日志但引擎在跑**：模拟引擎的 in-flight 成交在落库前不显示（几毫秒级，可接受）
- **周/月线对齐**：周期首日判定（TDX 语义）；若数据源不同导致周期首日规则差异，标记可能偏移一根 bar（已知限制，A 股 TDX 实测为准）

## 验收

- 构建零警告 + ctest 385 全绿
- 手动：模拟交易成交 → K线/分时出现标记 + 成本线；悬停显示浮框；切股清空；手动录入实盘日志 → 红标显示；删除/清空日志 → 标记消失
