# 设计：市场窗口合并板块窗口（一个 Dock + 4 平级 tab + 统一错峰刷新）

日期：2026-08-11
状态：已确认（方案 A）

## Context

左侧当前有两个独立 Dock 上下竖排：**市场**（MarketPanel：涨幅榜/跌幅榜 tab + 市场宽度常驻底部）和**板块**（SectorPanel：行业板块/概念板块 tab，TDX 全量 132/438，30s 自刷新）。另有**自定义指数** dock 与板块 tab 并列。

用户要求把板块窗口的功能合并进市场窗口，动机（全部确认）：
1. **省空间 / 减 Dock** — 两个 Dock 变一个，少一个标题栏和操作栏
2. **统一入口概念** — 涨幅/跌幅/板块同属「市场全景」，一个窗口 tab 切换更顺手
3. **板块双击开图** — 板块行现在点不了，合并后双击打开该板块的 K 线图
4. **统一刷新错峰** — 两面板各自 30s 刷 TDX，合并后一个时钟统一错峰，避免连接拥堵（此前卡顿的源头之一）

用户已确认的结构决策：
- **四个平级 tab**：涨幅榜 | 跌幅榜 | 行业板块 | 概念板块（不嵌套）
- **统一时钟 + 错峰轮询**：市场池 t=0、行业 +1s、概念 +2s
- **自定义指数 Dock 保持独立**，不与任何 dock tabify
- **板块表模板同步成涨跌幅榜**（去掉硬编码黑色样式，跟随应用主题）

## 功能定义

1. **市场 Dock 合并**：左侧只保留一个「市场」dock，内容为 4 平级 tab + 市场宽度常驻底部；板块 dock 及其标题栏/操作栏移除
2. **板块列表双击开图**：行业/概念板块行双击 → 中央图表打开该板块指数（880xxx）K 线图，行为参照指数条（`indexClicked`）——只开图，不设置右侧盘口/关键数据/筹码面板
3. **统一错峰刷新**：单一 30s 时钟，市场池 → 行业 → 概念错峰拉取；板块 tab 激活时缓存即现 + 立即后台刷新
4. **板块表视觉模板 = 涨跌幅榜模板**：无硬编码背景色，跟随应用主题；涨跌幅列红涨绿跌（与 MarketRankModel 同色值）；行选择 + 双击开图

## 关键设计决策

### 布局（合并后）

```
市场 Dock（左侧唯一）
├── 顶栏: [刷新] [导出]              更新时间
├── QTabWidget
│   ├── 涨幅榜    (MarketRankModel + QTableView，现有不动)
│   ├── 跌幅榜    (MarketRankModel + QTableView，现有不动)
│   ├── 行业板块  (SectorListPage(Industry) — 全量 132)
│   └── 概念板块  (SectorListPage(Concept)  — 全量 438)
└── 市场宽度     常驻底部（现有不动）
```

自定义指数 Dock 独立，不 tabify。

### 组件 1：SectorPanel → SectorListPage 重构（改 `sector_panel.{h,cpp}`）

类改名 `SectorPanel` → `SectorListPage`，从「带类型切换的自包含面板」降级为「固定类型的纯列表页」。

**移除**：
- `typeGroup_` 行业/概念按钮组、`refreshBtn_`、导出按钮（顶栏统一移到 MarketPanel）
- `timer_`、`showEvent/hideEvent`（定时刷新统一由 MarketPanel 管理）

**保留**：`updateLabel_`（每页右上角小字显示该页最近刷新时间「更新 HH:mm:ss」——板块页的时效性提示，不属于涨跌幅榜模板的观感项）
- `setType(SectorType)`、`type_` 成员
- `std::map<SectorType, ...>` 缓存容器 → 简化为单值（类型固定）

**构造**：`SectorListPage(IDataProvider* provider, SectorType type, QWidget* parent)`

**保留**（已验证逻辑原样）：
- 虚拟化 QTableView + SectorListModel（大表滚动不卡）
- 空态切换（stack_ + emptyLabel_）
- 涨跌幅降序 + 红涨绿跌
- `fetchRows`：TDX `getSectorIndices` 全量 + 类型过滤（8803xx-8804xx 行业 / 8805xx+ 概念）+ `batchQuoteInteractive`
- seq 去陈旧 + 同类型在途去重（fetching_/lastSeq_/fetchSeq_）
- 缓存（cache_）——切回即现

**新增**：
- `void refresh()`（公开，供 MarketPanel 统一时钟调用）
- 双击行 → `emit openSectorChart(const StockCode& code, const QString& name)`（880xxx StockCode 直接透传；参照指数条行为）
- `SectorType type() const`（供 MarketPanel 错峰调度识别）

### 组件 2：SectorListModel 模板同步（改 `sector_list_model.cpp`）

- **删除**：硬编码表底 `#18181a`、表头 `#222225`/`#bbbbbb`、名称列 `#dddddd`、数据列 `#999999`（这些色值专为黑底调）
- **同步涨跌幅榜**：
  - 涨跌幅列：红涨绿跌 `#e54648` / `#2e9e5b`（与 MarketRankModel 同色值，可直接复用同一对常量）
  - 名称/成交额列：跟随应用主题默认文字色（不再硬编码灰色）
  - 表底色/表头：无自定义 stylesheet，跟随应用主题（ThemeManager 全局 QSS）
- 三列（板块 / 涨跌幅 / 成交额）不变

### 组件 3：MarketPanel 扩展（改 `market_panel.{h,cpp}`）

- QTabWidget 4 tab：现有 涨幅/跌幅 + 新增 行业/概念（两个 SectorListPage 实例）
- 顶栏「刷新」「导出」：
  - 刷新 → 始终跑**全套错峰流程**（t=0 市场池 + t+1s 行业 + t+2s 概念，不区分当前 tab；符合已确认的「刷新按钮=同一套错峰流程」）
  - 导出 → 按当前 tab 分流（涨幅/跌幅现有逻辑 + 板块 tab 复用 `exportViewToCsv(table_, ...)`）
- **统一 30s 错峰时钟**（替换 SectorListPage 自管时钟）：
  ```
  t=0s      市场池 batchQuote(pool)          （涨幅/跌幅 + 市场宽度）
  t+1s      行业   SectorListPage(Industry).refresh()   → batchQuoteInteractive(132)
  t+2s      概念   SectorListPage(Concept).refresh()    → batchQuoteInteractive(438)
  每 30s 重复；用 QTimer::singleShot 实现偏移
  ```
  - 手动「刷新」按钮 = 同一套错峰流程（t=0 立即市场池，随后 +1s/+2s）
  - 板块 tab 激活（`tabs_->currentChanged`）→ 该页缓存即现 + 立即 `refresh()`
- 板块页 `openSectorChart` → 转发为 MarketPanel 现有 `openChart(StockCode, QString)` 信号

### 组件 4：MainWindow（改 `main_window.cpp`）

- 删除 `sectorDock` 及 `sectorPanel_` 实例创建
- `runScheduledTask` RefreshQuotes：`marketPanel_->refresh() + sectorPanel_->refresh()` → 收敛为 `marketPanel_->refresh()`（内部已覆盖板块错峰）
- 自定义指数 dock 不再 `tabifyDockWidget`（保持独立）
- 若有其它 `sectorPanel_` 引用（搜索/定时等）一并清理

### 数据 / 交互细节

- 错峰间隔 1s 可配置常量；三个数据源均异步（IO 池），TDX 连接串行不撞车
- 板块页隐藏时：缓存照常后台错峰刷新，切回即见新鲜数据（无需等激活刷新）
- 双击开图：板块行 880xxx 直接 `loadStock(code, name)`；右侧盘口/关键数据/筹码**不设置**（与指数条一致，避免对板块指数无意义行情/数据查询）
- 市场宽度常驻底部，4 tab 共享

## 文件改动

| 文件 | 改动 |
|------|------|
| `src/ui/panels/sector_panel.{h,cpp}` | 重构为 `SectorListPage`（固定类型、去 chrome、双击开图） |
| `src/ui/models/sector_list_model.cpp` | 模板同步涨跌幅榜（去硬编码黑底/灰字，红涨绿跌同色） |
| `src/ui/panels/market_panel.{h,cpp}` | 4 tab + 统一错峰时钟 + 转发 openSectorChart |
| `src/ui/main_window.cpp` | 删 sectorDock、定时任务引用收敛、自定义指数独立 |
| `src/CMakeLists.txt` | 无新文件（sector_panel.cpp 类名原地改） |
| `docs/DEVLOG.md` / `docs/changelog.md` | 收尾记录 |

引擎/数据层零改动。

## 测试与验证

- 现有 392 tests 不受影响（纯 UI 重构，无引擎/数据逻辑变更）
- `cmake --build --preset with-qt` 零错误零警告
- 冒烟清单：
  1. 左侧只剩一个「市场」dock；4 tab 切换正常
  2. 行业/概念全量列表滚动不卡（虚拟化仍生效）
  3. 板块表观感与涨幅榜一致（无黑底硬编码；涨跌幅红涨绿跌）
  4. 板块行双击 → 中央图表打开对应板块 K 线（880xxx）
  5. 刷新错峰：观察日志/更新时间戳，市场/行业/概念 ~1s 间隔，无拥堵卡顿
  6. 手动刷新 + 30s 定时刷新均正常；板块 tab 激活缓存即现
  7. 关窗不崩（回归；ThreadPool waitForDone 前无残留 sectorPanel_ 引用）

## 风险与对策

- **SectorPanel 类改名波及**：grep 全仓 `sectorPanel_`/`SectorPanel` 引用逐一清理；编译错即提示遗漏
- **错峰仍撞车**：市场池 5000 只最重，行业/概念已走 `batchQuoteInteractive`（交互优先级高于批量）；1s 偏移 + 交互优先级双保险
- **双击开图右侧面板残留**：开板块图不设置盘口/关键数据/筹码，仅 loadStock + refreshTradeMarks（同指数条）；旧股票面板状态保留不额外清
- **导出分流遗漏**：`exportViewToCsv` 目标按 currentIndex 分派，板块 tab 必须覆盖，否则导出空表
