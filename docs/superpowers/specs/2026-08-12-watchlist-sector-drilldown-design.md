# 设计：自选股列表 + 板块成分股下钻 + 市场窗口收编视图菜单

日期：2026-08-12
状态：已确认

## Context

P10 第十四轮（市场窗口合并板块，4 tab）已合并。用户选定下一轮 = **两个功能都做**：
1. **自选股/关注列表** — 目前全仓无此功能（仅有硬编码精选池 kCuratedSH/SZ 129 只，不可用户编辑）
2. **板块成分股下钻** — 数据层已有 `EastMoneySectorConstituents`（`fetchConstituents(boardName)` 返回成分股），但无任何 UI 使用

**前置布局调整**（用户明确要求）：
- 市场窗口从左侧主 Dock **收编到「视图」菜单**（默认隐藏，`toggleViewAction` 开关，仿筹码分布模式）
- **自选股 dock 占市场窗口原位置**（左侧主 Dock，默认可见）

用户已确认的交互决策：
- 自选股**仅从图表添加**（K线/分时图周期栏按钮）
- 已添加自选的股票，**按钮要标明「已在自选」**（checkable + 状态同步）
- 板块成分股下钻用**独立弹窗**
- 其余交互（移除/持久化/双击开图）由设计补齐

## 功能定义

1. **布局重排**：市场 Dock 默认隐藏、视图菜单开关；自选股 Dock 占市场原位置；自定义指数 Dock 随之下移
2. **自选股面板**：QTableView + 模型（虚拟化），名称/现价/涨跌幅，红涨绿跌，双击开图，行右键移除，持久化 `watchlist.json`，实时刷新
3. **图表加自选**：周期栏「加入自选」按钮（checkable），已在自选显示「已在自选」，点击 toggle 增删，状态随当前股票/自选变更同步
4. **板块成分股下钻**：板块行右键「查看成分股」→ 独立弹窗（成分股列表 + 行情 + 双击开图）

## 关键设计决策

### 布局重排（MainWindow）

改前左区：
```
市场 dock（可见，4 tab）
自定义指数 dock（市场下方，splitDockWidget Vertical）
```

改后左区：
```
自选股 dock（可见，占市场原位置）
自定义指数 dock（自选股下方，splitDockWidget Vertical）
市场 dock（左区，默认隐藏；视图→市场 toggleViewAction）
```

- `marketDock_` 由局部变量升级为成员（`QDockWidget* marketDock_`）
- 构造 `restoreState` 后 `if (marketDock_) marketDock_->hide();`（同 chipDock_ 模式，强制默认隐藏，不随历史布局常驻）
- 视图菜单加 `viewMenu->addAction(marketDock_->toggleViewAction())`（文本「市场」）
- `resetLayout()` 现有实现会 `show()` 全部 dock → 重置后市场+自选+筹码都显示（与筹码分布现有行为一致，可接受；用户可经视图菜单再隐藏）

### WatchlistPanel（新 `src/ui/panels/watchlist_panel.{h,cpp}`）

```cpp
/// 自选股面板 — 用户自选列表（图表周期栏「加入自选」添加，右键移除，双击开图）
class WatchlistPanel : public QWidget {
    Q_OBJECT
public:
    explicit WatchlistPanel(IDataProvider* provider, QWidget* parent = nullptr);
    void refresh();                                // 定时/手动刷新（batchQuoteInteractive）
    bool contains(const StockCode& code) const;    // 是否已在自选（图表按钮状态同步用）
    void add(const StockCode& code, const QString& name);   // 加自选（已存在则忽略）+ 持久化
    void remove(const StockCode& code);            // 移除 + 持久化
signals:
    void openChart(const StockCode& code, const QString& name);   // 双击行
    void watchlistChanged(const StockCode& code);  // 增删后发射（MainWindow 同步图表按钮）
private:
    void load();       // 构造时从 watchlist.json 加载
    void save();       // 变更后写回 watchlist.json
    std::vector<WatchItem> items_;   // code/name/lastPrice/changePct（模型行）
    WatchlistModel* model_ = nullptr;
    QTimer* timer_ = nullptr;        // 10s 刷新（交互优先级）
};
```

- 行模型 `WatchItem { StockCode code; QString name; double price; double changePct; }`
- `WatchlistModel`（新 `src/ui/models/watchlist_model.{h,cpp}`）：QAbstractTableModel，3 列（名称/现价/涨跌幅），红涨绿跌（`#e54648`/`#2e9e5b`），虚拟化渲染（QTableView，仿 SectorListModel）
- 双击行 → `emit openChart(code, name)`（复用 MainWindow 现有 openChart handler：开图 + 右侧面板联动）
- 行右键菜单：「移除自选」
- 刷新：`batchQuoteInteractive(items_.codes)`（交互优先级），10s 定时 + add 后立即刷一次；名称来自 add 时传入（首次）或 quotes 回填

### WatchlistStore（新 `src/ui/panels/watchlist_store.{h,cpp}`）

```cpp
/// 自选股持久化 — watchlist.json（config 目录，仿 trade_journal.json 模式）
class WatchlistStore {
public:
    static std::vector<StockCode> load(const std::string& path);
    static void save(const std::string& path, const std::vector<StockCode>& codes);
};
```
- 格式：`{"codes": ["SH600000", "SZ000001", ...]}`（UTF-8 JSON，nlohmann）
- 位置：`AppPaths::configDir() + "/watchlist.json"`
- 纯静态、可单测

### 图表加自选（CentralChartWidget 改）

- 周期栏加「加入自选」按钮 `watchlistBtn_`（checkable；非 standalone 窗口，同 chipBtn_ 条件）
- `void setWatchlistButtonChecked(bool in)`：`setChecked(in)` + 文本 `in ? tr("已在自选") : tr("加入自选")`
- 点击 → 有 `currentCode_` 才 `emit toggleWatchlist(currentCode_, currentName_)`
- 新增 `signal void toggleWatchlist(const StockCode& code, const QString& name);`
- 新增 `signal void currentCodeChanged(const StockCode& code);`：`loadStock`/`loadCustomIndex` 成功后发射（股票切换后 MainWindow 同步按钮状态）

### MainWindow 接线

- 创建 `watchlistDock_` + `WatchlistPanel`（占市场原位置）
- `connect(watchlistPanel_, &WatchlistPanel::openChart, ...)` → 复用市场面板的 openChart handler（开图+右侧面板）
- `connect(centralChart_, &CentralChartWidget::toggleWatchlist, this, ...)`：
  ```cpp
  [this](const StockCode& code, const QString& name) {
      if (!watchlistPanel_) return;
      if (watchlistPanel_->contains(code)) { watchlistPanel_->remove(code);
          statusBar()->showMessage(tr("已从自选移除 %1").arg(name), 3000); }
      else { watchlistPanel_->add(code, name);
          statusBar()->showMessage(tr("已加入自选 %1").arg(name), 3000); }
      syncWatchlistButton();
  }
  ```
- `connect(centralChart_, &CentralChartWidget::currentCodeChanged, this, ...)` → `syncWatchlistButton()`
- `connect(watchlistPanel_, &WatchlistPanel::watchlistChanged, this, ...)` → 若影响当前代码则 `syncWatchlistButton()`
- `syncWatchlistButton()`：`centralChart_->setWatchlistButtonChecked(watchlistPanel_ && watchlistPanel_->contains(centralChart_->currentCode()))`

### 板块成分股下钻（新 `src/ui/widgets/sector_constituents_dialog.{h,cpp}`）

```cpp
/// 板块成分股弹窗 — 输入板块中文名，异步拉取成分股+行情，双击开图
class SectorConstituentsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SectorConstituentsDialog(IDataProvider* provider,
                                      const QString& boardName, QWidget* parent = nullptr);
signals:
    void openChart(const StockCode& code, const QString& name);  // 双击成分股
private:
    void fetch();   // IO 池：EastMoneySectorConstituents::fetchConstituents(boardName)
                    // → batchQuoteInteractive → 主线程填充
};
```

- 接线：`SectorListPage` 加行右键菜单「查看成分股」→ `emit openConstituents(name)`；`MarketPanel` 连接 → 弹 `SectorConstituentsDialog(provider_, name, this)`；弹窗 `openChart` → `MarketPanel::openChart` 转发（复用现有 → MainWindow 开图）
- 弹窗 UI：QTableView（代码/名称/现价/涨跌幅，红涨绿跌）+ 双击开图 + 关闭；加载中显示「加载中…」，空结果显示「未找到成分股」
- 数据：`fetchConstituents(boardName)` 用板块中文名（SectorRow.name）；风险：TDX 板块名 vs 东财板块名不一致 → 空态兜底（记已知限制）

### 文件改动

| 文件 | 改动 |
|------|------|
| `src/ui/panels/watchlist_panel.{h,cpp}` | NEW — 自选股面板 |
| `src/ui/models/watchlist_model.{h,cpp}` | NEW — 自选股模型 |
| `src/ui/panels/watchlist_store.{h,cpp}` | NEW — watchlist.json 持久化 |
| `src/ui/widgets/sector_constituents_dialog.{h,cpp}` | NEW — 成分股弹窗 |
| `src/ui/widgets/central_chart_widget.{h,cpp}` | 加自选按钮 + 两信号 + setWatchlistButtonChecked |
| `src/ui/panels/sector_panel.{h,cpp}` | 行右键「查看成分股」→ openConstituents |
| `src/ui/panels/market_panel.{h,cpp}` | openConstituents → 弹窗；弹窗 openChart 转发 |
| `src/ui/main_window.{h,cpp}` | dock 重排 + 视图菜单 + 接线 |
| `src/CMakeLists.txt` | 新增 4 组文件 |
| `docs/DEVLOG.md` / `docs/changelog.md` / `CLAUDE.md` | 收尾 |

### 测试与验证

- **新增单测**：
  - `WatchlistStore` 序列化 round-trip（load/save 往返，+3：空/单/多代码、非法 JSON 空列表）→ test_data
  - `EastMoneySectorConstituents::parseConstituents`（若已存在测试则跳过；+2~3：有效列表解析、空/畸形返回空）→ test_data
- 基线 392 → 预计 ~397（以实跑为准）
- 构建零警告 + ctest 全绿
- 冒烟：视图→市场 开关 dock；自选添加/移除/双击开图/重启持久化；图表按钮「加入自选」↔「已在自选」切换与股票切换同步；板块行右键成分股弹窗双击开图；关窗不崩

## 风险与对策

- **TDX 板块名 vs 东财板块名不一致**（成分股下钻）：弹窗空态兜底 + 记已知限制；v2 可加 880xxx → 东财板块代码映射
- **自选按钮状态同步遗漏**：currentCodeChanged + watchlistChanged 双信号兜底；loadCustomIndex 也发射 currentCodeChanged（伪代码 contains 恒 false → 按钮复位「加入自选」）
- **watchlist.json 并发写**：单线程 UI 操作，无并发；save 前深拷贝 codes
- **resetLayout 后市场 dock 显示**：与筹码分布现有行为一致，可接受（用户可再隐藏）
- **standalone 图表窗口无自选按钮**：与 chipBtn_ 同条件（非 standalone），避免跨窗口接线复杂度
