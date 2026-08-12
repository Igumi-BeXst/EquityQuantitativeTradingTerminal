# 自选股 + 板块成分股下钻 + 市场收编视图菜单 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增自选股/关注列表（占市场窗口原左位）+ 板块成分股下钻弹窗；市场窗口收编到「视图」菜单（默认隐藏可开关）。

**Architecture:** 四个新组件（WatchlistStore 持久化 / WatchlistModel / WatchlistPanel / SectorConstituentsDialog）+ CentralChartWidget 加「加入自选」checkable 按钮（已在自选显示「已在自选」）+ MainWindow dock 重排与接线。自选股走 `batchQuoteInteractive`（交互优先级），成分股下钻复用 `EastMoneySectorConstituents` + `MarketRankModel`。

**Tech Stack:** C++17, Qt 6.11 (Widgets/QTableView/QAbstractTableModel/QDialog), nlohmann/json, ThreadPool 异步 + QPointer 守卫 + QMetaObject::invokeMethod。

设计文档：[2026-08-12-watchlist-sector-drilldown-design.md](../specs/2026-08-12-watchlist-sector-drilldown-design.md)

## Global Constraints

- 分层架构，UI 只能依赖 engine/intelligence/data/core/foundation，禁止反向
- 安全异步：禁裸 `this` 捕获；IO 线程捕获 provider 按值 + `QPointer` 守卫 + `QMetaObject::invokeMethod(guard, lambda, Qt::QueuedConnection)`；`QTimer::singleShot` 以 `this` 为 context
- 编译零警告：`cmake --build --preset with-qt` 必须零错误零警告
- 回归：ctest 现有 392 tests 全绿；新增 WatchlistStore round-trip 单测（~+3 → ~395）
- 自选股持久化：`AppPaths::configDir() + "/watchlist.json"`，格式 `{"codes": ["SH600000", ...]}`（`fullCode()` 格式）
- 涨跌幅列红涨绿跌 `#e54648`/`#2e9e5b`；表格跟随应用主题（无硬编码黑底）
- 现有已验证异步/缓存/seq 去陈旧模式沿用（SectorListPage/MarketPanel 同款）
- `EastMoneySectorConstituents::parseConstituents` 测试已存在（test_sector_constituents.cpp，4 例），不重复加

---

### Task 1: WatchlistStore + WatchlistModel + 单测

**Files:**
- Create: `src/foundation/utils/watchlist_store.h`   （foundation 层——st_foundation 可测，仿 scheduled_task_store 先例）
- Create: `src/foundation/utils/watchlist_store.cpp`
- Create: `src/ui/models/watchlist_model.h`
- Create: `src/ui/models/watchlist_model.cpp`
- Test: `tests/test_foundation/test_watchlist_store.cpp`
- Modify: `src/CMakeLists.txt`（st_foundation 加 watchlist_store；st_ui 加 watchlist_model）

**Interfaces:**
- Produces:
  - `class WatchlistStore { static std::vector<StockCode> load(const std::string& path); static void save(const std::string& path, const std::vector<StockCode>& codes); }`
  - `struct WatchItem { StockCode code; QString name; double price = 0.0; double changePct = 0.0; }`
  - `class WatchlistModel : public QAbstractTableModel`：`void setItems(std::vector<WatchItem>)` / `const WatchItem& itemAt(int row) const` / 3 列（名称/现价/涨跌幅）/ 涨跌幅列红涨绿跌
- Consumes: `StockCode(std::string_view fullCode)`（parse "SH600000"）、`StockCode::fullCode()`、`AppPaths::configDir()`。

- [ ] **Step 1: 写失败测试 `tests/test_data/test_watchlist_store.cpp`**

```cpp
#include "foundation/utils/watchlist_store.h"
#include "foundation/stock_code.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>

namespace st {
namespace {

TEST(WatchlistStoreTest, SaveLoadRoundTrip) {
    const std::string path = "watchlist_test_roundtrip.json";
    std::remove(path.c_str());
    std::vector<StockCode> codes;
    codes.emplace_back(std::string_view("SH600000"));
    codes.emplace_back(std::string_view("SZ000001"));
    WatchlistStore::save(path, codes);
    auto loaded = WatchlistStore::load(path);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].fullCode(), "SH600000");
    EXPECT_EQ(loaded[1].fullCode(), "SZ000001");
    std::remove(path.c_str());
}

TEST(WatchlistStoreTest, LoadEmptyWhenMissingOrBad) {
    EXPECT_TRUE(WatchlistStore::load("watchlist_does_not_exist.json").empty());
    const std::string path = "watchlist_test_bad.json";
    { std::ofstream f(path); f << "not json{" << std::endl; }
    EXPECT_TRUE(WatchlistStore::load(path).empty());
    std::remove(path.c_str());
}

TEST(WatchlistStoreTest, SaveEmptyProducesValidFile) {
    const std::string path = "watchlist_test_empty.json";
    std::remove(path.c_str());
    WatchlistStore::save(path, {});
    EXPECT_TRUE(WatchlistStore::load(path).empty());
    std::remove(path.c_str());
}

}  // namespace
}  // namespace st
```

- [ ] **Step 2: 运行确认失败**

Run: `ctest --preset default -R WatchlistStore`（构建后）
Expected: FAIL（`WatchlistStore` 未定义 / 测试未编译进 test_data）。

- [ ] **Step 3: 实现 `src/ui/panels/watchlist_store.{h,cpp}`**

```cpp
// watchlist_store.h（foundation/utils）
#pragma once
#include "foundation/stock_code.h"
#include <string>
#include <vector>

namespace st {

/// 自选股持久化 — watchlist.json（config 目录，仿 scheduled_task_store 的 foundation 层 store 先例）
class WatchlistStore {
public:
    /// 加载（文件不存在/非法 JSON/缺 codes 字段 → 空列表）
    static std::vector<StockCode> load(const std::string& path);
    /// 保存（UTF-8 JSON：{"codes": [fullCode...]}）
    static void save(const std::string& path, const std::vector<StockCode>& codes);
};

} // namespace st
```

```cpp
// watchlist_store.cpp（foundation/utils）
#include "foundation/utils/watchlist_store.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace st {

using json = nlohmann::json;

std::vector<StockCode> WatchlistStore::load(const std::string& path) {
    std::vector<StockCode> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    json j;
    try { in >> j; } catch (...) { return out; }
    if (!j.is_object() || !j.contains("codes") || !j["codes"].is_array()) return out;
    for (const auto& c : j["codes"]) {
        if (!c.is_string()) continue;
        try {
            StockCode sc(c.get<std::string>());
            if (sc.isValid()) out.push_back(std::move(sc));
        } catch (...) { /* 跳过坏条目 */ }
    }
    return out;
}

void WatchlistStore::save(const std::string& path, const std::vector<StockCode>& codes) {
    json j;
    j["codes"] = json::array();
    for (const auto& c : codes) j["codes"].push_back(c.fullCode());
    std::ofstream out(path, std::ios::binary);
    if (out) out << j.dump(2);
}

} // namespace st
```

- [ ] **Step 4: 实现 `src/ui/models/watchlist_model.{h,cpp}`**

```cpp
// watchlist_model.h
#pragma once
#include "foundation/stock_code.h"
#include <QAbstractTableModel>
#include <QString>
#include <vector>

namespace st {

/// 自选股行
struct WatchItem {
    StockCode code;
    QString name;
    double price = 0.0;
    double changePct = 0.0;
};

/// 自选股 Model — QTableView 虚拟化渲染（名称/现价/涨跌幅）
class WatchlistModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit WatchlistModel(QObject* parent = nullptr);
    void setItems(std::vector<WatchItem> items);
    const WatchItem& itemAt(int row) const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
private:
    std::vector<WatchItem> items_;
};

} // namespace st
```

```cpp
// watchlist_model.cpp
#include "ui/models/watchlist_model.h"
#include <QColor>

namespace st {

namespace {
constexpr const char* kUpColor = "#e54648";    // 红涨
constexpr const char* kDownColor = "#2e9e5b";  // 绿跌
}  // namespace

WatchlistModel::WatchlistModel(QObject* parent) : QAbstractTableModel(parent) {}

void WatchlistModel::setItems(std::vector<WatchItem> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

const WatchItem& WatchlistModel::itemAt(int row) const {
    static const WatchItem kEmpty;
    if (row < 0 || static_cast<size_t>(row) >= items_.size()) return kEmpty;
    return items_[static_cast<size_t>(row)];
}

int WatchlistModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(items_.size());
}

int WatchlistModel::columnCount(const QModelIndex&) const { return 3; }

QVariant WatchlistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(items_.size())) return {};
    const auto& it = items_[static_cast<size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case 0: return it.name;
                case 1: return QString::number(it.price, 'f', 2);
                case 2: return QStringLiteral("%1%").arg(it.changePct, 0, 'f', 2);
                default: return {};
            }
        case Qt::ForegroundRole:
            if (index.column() == 2) {
                return it.changePct >= 0.0
                    ? QColor(QString::fromUtf8(kUpColor))
                    : QColor(QString::fromUtf8(kDownColor));
            }
            return {};  // 名称/现价跟随应用主题默认色
        default: return {};
    }
}

QVariant WatchlistModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case 0: return QStringLiteral("名称");
        case 1: return QStringLiteral("现价");
        case 2: return QStringLiteral("涨跌幅");
        default: return {};
    }
}

} // namespace st

#include "moc_watchlist_model.cpp"
```

- [ ] **Step 5: CMake 注册**

`src/CMakeLists.txt`：st_foundation 源列表加 `foundation/utils/watchlist_store.cpp`；st_ui 源列表加 `ui/models/watchlist_model.cpp`。`tests/CMakeLists.txt` 的 `test_foundation` 源列表加 `test_foundation/test_watchlist_store.cpp`。

- [ ] **Step 6: 构建 + 跑新测试 + 全量回归**

Run: `cmake --build --preset with-qt` → `ctest --preset default`
Expected: 构建零警告；WatchlistStoreTest 3/3 过；全量 392+3=**395** 过。

- [ ] **Step 7: 提交**

```bash
git add src/ui/panels/watchlist_store.h src/ui/panels/watchlist_store.cpp \
        src/ui/models/watchlist_model.h src/ui/models/watchlist_model.cpp \
        tests/test_data/test_watchlist_store.cpp src/CMakeLists.txt
git commit -m "feat: 自选股持久化 WatchlistStore + WatchlistModel（含 round-trip 单测）"
```

---

### Task 2: WatchlistPanel 自选股面板

**Files:**
- Create: `src/ui/panels/watchlist_panel.h`
- Create: `src/ui/panels/watchlist_panel.cpp`
- Modify: `src/CMakeLists.txt`（st_ui 加 watchlist_panel）

**Interfaces:**
- Consumes: Task 1 的 `WatchlistStore` / `WatchlistModel` / `WatchItem`；`IDataProvider::batchQuoteInteractive`；`core/thread_pool.h`；`core/app_paths.h`。
- Produces: `class WatchlistPanel : public QWidget`：构造 `(IDataProvider*, QWidget* = nullptr)`；`void refresh()` / `bool contains(const StockCode&) const` / `void add(const StockCode&, const QString&)` / `void remove(const StockCode&)`；signals `openChart(const StockCode&, const QString&)`、`watchlistChanged(const StockCode&)`。Task 5（MainWindow）消费。

- [ ] **Step 1: 实现头文件 `src/ui/panels/watchlist_panel.h`**

```cpp
#pragma once

#include "foundation/stock_code.h"
#include "data/idata_provider.h"
#include "ui/models/watchlist_model.h"
#include <QString>
#include <QWidget>
#include <vector>

class QTimer;
class QTableView;
class QLabel;

namespace st {

/// 自选股面板 — 用户自选列表（图表周期栏「加入自选」添加；右键移除；双击开图）
///
/// 数据：batchQuoteInteractive（交互优先级）定时刷新（10s）；持久化 watchlist.json（WatchlistStore）。
/// 展示：QTableView + WatchlistModel 虚拟化（名称/现价/涨跌幅，红涨绿跌）。
class WatchlistPanel : public QWidget {
    Q_OBJECT

public:
    explicit WatchlistPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 拉取自选股行情（IO 池；seq 去陈旧；在途则跳过）
    void refresh();
    bool contains(const StockCode& code) const;
    void add(const StockCode& code, const QString& name);
    void remove(const StockCode& code);

signals:
    void openChart(const StockCode& code, const QString& name);
    void watchlistChanged(const StockCode& code);

private:
    void onContextMenu(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);
    void onQuotesReady(int seq, std::vector<Quote> quotes);
    void load();
    void save();

    IDataProvider* provider_ = nullptr;
    std::string path_;
    int fetchSeq_ = 0;
    bool fetching_ = false;
    std::vector<WatchItem> items_;
    QTimer* timer_ = nullptr;
    QTableView* table_ = nullptr;
    WatchlistModel* model_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
};

} // namespace st
```

- [ ] **Step 2: 实现 `src/ui/panels/watchlist_panel.cpp`**

```cpp
#include "ui/panels/watchlist_panel.h"
#include "core/app_paths.h"
#include "core/thread_pool.h"
#include "foundation/utils/watchlist_store.h"
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace st {

namespace {
constexpr int kRefreshMs = 10000;  // 10s 刷新（交互优先级）
}  // namespace

WatchlistPanel::WatchlistPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      path_(AppPaths::configDir() + "/watchlist.json") {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    stack_ = new QStackedWidget(this);
    model_ = new WatchlistModel(this);
    table_ = new QTableView(stack_);
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    stack_->addWidget(table_);
    emptyLabel_ = new QLabel(tr("暂无自选，在图表周期栏点「加入自选」添加"), stack_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("color:#666666;"));
    emptyLabel_->setWordWrap(true);
    stack_->addWidget(emptyLabel_);
    layout->addWidget(stack_, 1);

    connect(table_, &QTableView::doubleClicked, this, &WatchlistPanel::onDoubleClicked);
    connect(table_, &QWidget::customContextMenuRequested,
            this, &WatchlistPanel::onContextMenu);

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &WatchlistPanel::refresh);
    timer_->start();

    load();      // 从 watchlist.json 恢复
    refresh();   // 立即拉一次行情
}

void WatchlistPanel::load() {
    auto codes = WatchlistStore::load(path_);
    items_.clear();
    items_.reserve(codes.size());
    for (auto& c : codes) {
        WatchItem it;
        it.code = std::move(c);
        it.name = QString::fromStdString(it.code.displayCode());  // 名称先占位，行情后回填
        items_.push_back(std::move(it));
    }
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(items_.empty()
        ? static_cast<QWidget*>(emptyLabel_) : static_cast<QWidget*>(table_));
}

void WatchlistPanel::save() {
    std::vector<StockCode> codes;
    codes.reserve(items_.size());
    for (const auto& it : items_) codes.push_back(it.code);
    WatchlistStore::save(path_, codes);
}

bool WatchlistPanel::contains(const StockCode& code) const {
    const std::string key = code.fullCode();
    for (const auto& it : items_) if (it.code.fullCode() == key) return true;
    return false;
}

void WatchlistPanel::add(const StockCode& code, const QString& name) {
    if (contains(code)) return;
    WatchItem it;
    it.code = code;
    it.name = name.isEmpty() ? QString::fromStdString(code.displayCode()) : name;
    items_.push_back(std::move(it));
    save();
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(static_cast<QWidget*>(table_));
    emit watchlistChanged(code);
    refresh();   // 新加入立即拉行情
}

void WatchlistPanel::remove(const StockCode& code) {
    const std::string key = code.fullCode();
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                 [&key](const WatchItem& it) { return it.code.fullCode() == key; }),
                 items_.end());
    save();
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(items_.empty()
        ? static_cast<QWidget*>(emptyLabel_) : static_cast<QWidget*>(table_));
    emit watchlistChanged(code);
}

void WatchlistPanel::refresh() {
    if (!provider_ || items_.empty() || fetching_) return;
    fetching_ = true;
    const int seq = ++fetchSeq_;
    std::vector<StockCode> codes;
    codes.reserve(items_.size());
    for (const auto& it : items_) codes.push_back(it.code);
    IDataProvider* provider = provider_;
    QPointer<WatchlistPanel> guard(this);
    ThreadPool::submitIO([provider, guard, seq, codes] {
        auto quotes = provider->batchQuoteInteractive(codes);
        QMetaObject::invokeMethod(guard, [guard, seq, quotes = std::move(quotes)]() mutable {
            guard->onQuotesReady(seq, std::move(quotes));
        }, Qt::QueuedConnection);
    });
}

void WatchlistPanel::onQuotesReady(int seq, std::vector<Quote> quotes) {
    fetching_ = false;
    if (seq != fetchSeq_) return;  // 陈旧丢弃
    std::unordered_map<std::string, const Quote*> qmap;
    qmap.reserve(quotes.size());
    for (const auto& q : quotes) qmap[q.code.fullCode()] = &q;
    for (auto& it : items_) {
        const auto qit = qmap.find(it.code.fullCode());
        if (qit != qmap.end()) {
            it.price = qit->second->lastPrice;
            it.changePct = qit->second->change;
        }
    }
    if (model_) model_->setItems(items_);
}

void WatchlistPanel::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !model_) return;
    const auto& it = model_->itemAt(index.row());
    if (it.code.isValid()) emit openChart(it.code, it.name);
}

void WatchlistPanel::onContextMenu(const QPoint& pos) {
    const QModelIndex idx = table_->indexAt(pos);
    if (!idx.isValid()) return;
    const auto& it = model_->itemAt(idx.row());
    if (!it.code.isValid()) return;
    QMenu menu(this);
    QAction* removeAct = menu.addAction(tr("移除自选 %1").arg(it.name));
    QAction* refreshAct = menu.addAction(tr("刷新"));
    QAction* chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
    if (chosen == removeAct) remove(it.code);
    else if (chosen == refreshAct) refresh();
}

} // namespace st

#include "moc_watchlist_panel.cpp"
```

注意：`onQuotesReady` 用 `std::unordered_map` — 文件顶部补 `#include <unordered_map>`。`QStackedWidget` 需要 `#include <QStackedWidget>`。

- [ ] **Step 3: CMake 注册**

`src/CMakeLists.txt` st_ui 加 `ui/panels/watchlist_panel.cpp`。

- [ ] **Step 4: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告。

- [ ] **Step 5: 提交**

```bash
git add src/ui/panels/watchlist_panel.h src/ui/panels/watchlist_panel.cpp src/CMakeLists.txt
git commit -m "feat: 自选股面板 WatchlistPanel（增删/持久化/10s 交互优先级刷新/双击开图）"
```

---

### Task 3: CentralChartWidget 加「加入自选」按钮 + 状态信号

**Files:**
- Modify: `src/ui/widgets/central_chart_widget.h`
- Modify: `src/ui/widgets/central_chart_widget.cpp`

**Interfaces:**
- Consumes: 现有 `currentCode_`/`currentName_`、`standalone_`、`setChipButtonChecked` 模式。
- Produces: `signal void toggleWatchlist(const StockCode& code, const QString& name);`、`signal void currentCodeChanged(const StockCode& code);`、`void setWatchlistButtonChecked(bool in);`。Task 5（MainWindow）连接 `toggleWatchlist`/`currentCodeChanged` 并调用 `setWatchlistButtonChecked`。

- [ ] **Step 1: 改头文件 `central_chart_widget.h`**

signals 区（`openNewWindow` 附近）加：
```cpp
    /// 图表「加入自选」点击 → 主窗口 toggle 自选（增/删）
    void toggleWatchlist(const StockCode& code, const QString& name);
    /// 当前图表标的切换（loadStock/loadCustomIndex）→ 主窗口同步自选按钮状态
    void currentCodeChanged(const StockCode& code);
```

public 区（`setChipButtonChecked` 附近）加：
```cpp
    /// 自选按钮状态同步：已在自选 → 勾选 + 「已在自选」文本
    void setWatchlistButtonChecked(bool in);
```

private 成员（`chipBtn_` 附近）加：
```cpp
    QPushButton* watchlistBtn_ = nullptr;   // 「加入自选」（非 standalone；勾选=已在自选）
```

- [ ] **Step 2: 改实现 `central_chart_widget.cpp` — 建按钮**

`chipBtn_` 创建块之后、`}`（`if (!standalone_)` 收尾）之前加：

```cpp
        watchlistBtn_ = new QPushButton(tr("加入自选"), this);
        watchlistBtn_->setCheckable(true);
        watchlistBtn_->setStyleSheet(QStringLiteral(
            "QPushButton{color:#e6e6e6;border:1px solid #6a6a6a;border-radius:3px;"
            "padding:2px 10px;background:#2a2a2c;}"
            "QPushButton:hover{background:#3d3d40;border-color:#999999;}"
            "QPushButton:checked{color:#ffd700;border-color:#ffd700;"
            "background:#3a3220;font-weight:bold;}"));
        periodBar->addWidget(watchlistBtn_);
        connect(watchlistBtn_, &QPushButton::clicked, this, [this](bool) {
            if (currentCode_.isValid()) emit toggleWatchlist(currentCode_, currentName_);
        });
```

- [ ] **Step 3: 改实现 — loadStock/loadCustomIndex 发射 currentCodeChanged**

`loadStock`（末尾 `refreshOverlayButton();` 之后）加：
```cpp
    emit currentCodeChanged(currentCode_);
```
`loadCustomIndex`（末尾 `refreshOverlayButton();` 之后）加：
```cpp
    emit currentCodeChanged(currentCode_);
```

- [ ] **Step 4: 改实现 — setWatchlistButtonChecked**

`setChipButtonChecked` 定义后加：
```cpp
void CentralChartWidget::setWatchlistButtonChecked(bool in) {
    if (!watchlistBtn_) return;
    watchlistBtn_->setChecked(in);
    watchlistBtn_->setText(in ? tr("已在自选") : tr("加入自选"));
}
```

- [ ] **Step 5: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告（按钮未接线不影响编译；Task 5 接线）。

- [ ] **Step 6: 提交**

```bash
git add src/ui/widgets/central_chart_widget.h src/ui/widgets/central_chart_widget.cpp
git commit -m "feat: 图表周期栏「加入自选」按钮（checkable）+ currentCodeChanged 信号"
```

---

### Task 4: SectorConstituentsDialog + SectorListPage 右键 + MarketPanel 接线

**Files:**
- Create: `src/ui/widgets/sector_constituents_dialog.h`
- Create: `src/ui/widgets/sector_constituents_dialog.cpp`
- Modify: `src/ui/panels/sector_panel.h`
- Modify: `src/ui/panels/sector_panel.cpp`
- Modify: `src/ui/panels/market_panel.h`
- Modify: `src/ui/panels/market_panel.cpp`
- Modify: `src/CMakeLists.txt`（st_ui 加 sector_constituents_dialog）

**Interfaces:**
- Consumes: `EastMoneySectorConstituents::fetchConstituents(boardName)`（data 层，已有）；`IDataProvider::getStockList(Market::SH/SZ)`（名称 map）、`batchQuoteInteractive`；`MarketRankModel`/`MarketRankItem`（`src/ui/models/market_rank_model.h`）。
- Produces: `class SectorConstituentsDialog : public QDialog`：构造 `(IDataProvider*, const QString& boardName, QWidget* = nullptr)`；signal `openChart(const StockCode&, const QString&)`。`SectorListPage` 新增 `signal void openConstituents(const QString& name);`。`MarketPanel` 新增 slot `onOpenConstituents(const QString& name)`。

- [ ] **Step 1: 实现头文件 `src/ui/widgets/sector_constituents_dialog.h`**

```cpp
#pragma once

#include "data/idata_provider.h"
#include "foundation/stock_code.h"
#include <QDialog>
#include <QString>
#include <unordered_map>
#include <vector>

class QTableView;

namespace st {

class MarketRankModel;

/// 板块成分股弹窗 — 输入板块中文名，异步拉成分股 + 行情 + 名称，双击开图
///
/// 数据：EastMoneySectorConstituents::fetchConstituents(boardName) → codes；
/// provider->getStockList(SH/SZ) 构建名称 map（TDX 缓存命中即快）；batchQuoteInteractive 行情。
class SectorConstituentsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SectorConstituentsDialog(IDataProvider* provider,
                                      const QString& boardName, QWidget* parent = nullptr);

signals:
    void openChart(const StockCode& code, const QString& name);

private:
    void fetchConstituents();
    void onCodesReady(std::vector<StockCode> codes);
    void fetchQuotes(const std::vector<StockCode>& codes);
    void onQuotesReady(std::vector<Quote> quotes);

    IDataProvider* provider_ = nullptr;
    QString boardName_;
    QTableView* table_ = nullptr;
    MarketRankModel* model_ = nullptr;
    int fetchSeq_ = 0;
    bool fetching_ = false;
    std::vector<StockCode> codes_;            // 当前成分股
    std::unordered_map<std::string, std::string> names_;  // fullCode → 中文名
};

} // namespace st
```

- [ ] **Step 2: 实现 `src/ui/widgets/sector_constituents_dialog.cpp`**

```cpp
#include "ui/widgets/sector_constituents_dialog.h"
#include "data/eastmoney_sector_constituents.h"
#include "core/thread_pool.h"
#include "ui/models/market_rank_model.h"
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QTableView>
#include <QVBoxLayout>

namespace st {

namespace {
constexpr int kTopN = 200;  // 成分股最多显示前 200（东财分页，防超长列表）
}  // namespace

SectorConstituentsDialog::SectorConstituentsDialog(IDataProvider* provider,
                                                   const QString& boardName, QWidget* parent)
    : QDialog(parent), provider_(provider), boardName_(boardName) {
    setWindowTitle(tr("成分股 — %1").arg(boardName));
    setMinimumSize(520, 480);

    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("加载 %1 成分股…").arg(boardName), this);
    title->setStyleSheet(QStringLiteral("color:#888888;"));
    layout->addWidget(title);

    model_ = new MarketRankModel(this);
    table_ = new QTableView(this);
    table_->setModel(model_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setShowGrid(false);
    layout->addWidget(table_, 1);

    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || !model_) return;
        const auto& item = model_->itemAt(idx.row());
        if (item.code.isValid()) emit openChart(item.code, QString::fromStdString(item.name));
    });

    fetchConstituents();
}

void SectorConstituentsDialog::fetchConstituents() {
    if (!provider_ || fetching_) return;
    fetching_ = true;
    const int seq = ++fetchSeq_;
    const QString board = boardName_;
    IDataProvider* provider = provider_;
    QPointer<SectorConstituentsDialog> guard(this);
    ThreadPool::submitIO([provider, guard, seq, board] {
        EastMoneySectorConstituents em;
        auto codes = em.fetchConstituents(board.toStdString());
        // 名称 map：TDX 股票列表缓存（SH+SZ），供成分股中文名
        std::unordered_map<std::string, std::string> names;
        for (const auto m : {Market::SH, Market::SZ}) {
            for (const auto& s : provider->getStockList(m)) {
                if (!s.code.isValid()) continue;
                names[s.code.fullCode()] = s.name;
            }
        }
        QMetaObject::invokeMethod(guard, [guard, seq, codes = std::move(codes),
                                          names = std::move(names)]() mutable {
            guard->fetchSeq_ = seq;
            guard->fetching_ = false;
            guard->names_ = std::move(names);
            guard->onCodesReady(std::move(codes));
        }, Qt::QueuedConnection);
    });
}

void SectorConstituentsDialog::onCodesReady(std::vector<StockCode> codes) {
    codes_ = std::move(codes);
    if (codes_.empty()) {
        setWindowTitle(tr("成分股 — %1（未找到）").arg(boardName_));
        return;
    }
    fetchQuotes(codes_);
}

void SectorConstituentsDialog::fetchQuotes(const std::vector<StockCode>& codes) {
    if (!provider_ || codes.empty()) return;
    std::vector<StockCode> batch(codes.begin(), codes.begin() +
        std::min(kTopN, static_cast<int>(codes.size())));
    const int seq = ++fetchSeq_;
    IDataProvider* provider = provider_;
    QPointer<SectorConstituentsDialog> guard(this);
    ThreadPool::submitIO([provider, guard, seq, batch] {
        auto quotes = provider->batchQuoteInteractive(batch);
        QMetaObject::invokeMethod(guard, [guard, seq, quotes = std::move(quotes)]() mutable {
            if (seq != guard->fetchSeq_) return;  // 陈旧丢弃
            guard->onQuotesReady(std::move(quotes));
        }, Qt::QueuedConnection);
    });
}

void SectorConstituentsDialog::onQuotesReady(std::vector<Quote> quotes) {
    std::vector<MarketRankItem> items;
    items.reserve(quotes.size());
    for (const auto& q : quotes) {
        if (q.lastPrice <= 0 || q.preClose <= 0) continue;  // 停牌跳过
        MarketRankItem it;
        it.code = q.code;
        auto nit = names_.find(q.code.fullCode());
        it.name = nit != names_.end() ? nit->second : q.code.displayCode();
        it.price = q.lastPrice;
        it.changePct = q.change;
        items.push_back(std::move(it));
    }
    if (model_) model_->setItems(items);
    setWindowTitle(tr("成分股 — %1（%2）").arg(boardName_).arg(items.size()));
}

} // namespace st

#include "moc_sector_constituents_dialog.cpp"
```

注意：`std::min` 需 `#include <algorithm>`；`Market` 枚举来自 `foundation/stock_code.h`（已经 idata_provider.h 传递）。

- [ ] **Step 3: 改 `sector_panel.{h,cpp}` — 行右键「查看成分股」**

`sector_panel.h` signals 区（`openSectorChart` 后）加：
```cpp
    /// 板块行右键「查看成分股」→ MarketPanel 开成分股弹窗
    void openConstituents(const QString& name);
```

`sector_panel.cpp` 构造（双击 connect 后）加：
```cpp
    // 右键菜单：查看成分股
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        const QModelIndex idx = table_->indexAt(pos);
        if (!idx.isValid() || !model_) return;
        const auto& r = model_->rowAt(idx.row());
        QMenu menu(this);
        QAction* act = menu.addAction(tr("查看成分股 %1").arg(r.name));
        if (menu.exec(table_->viewport()->mapToGlobal(pos)) == act) {
            emit openConstituents(r.name);
        }
    });
```
`#include <QMenu>` 加进 `sector_panel.cpp`。

- [ ] **Step 4: 改 `market_panel.{h,cpp}` — 接线弹窗**

`market_panel.h` private 加：
```cpp
    void onOpenConstituents(const QString& name);
```

`market_panel.cpp`：
- include `#include "ui/widgets/sector_constituents_dialog.h"`（MarketPanel 打开弹窗需要；弹窗是独立窗口，但作为 market 面板动作发起点）
- 连接板块页 `openConstituents`（`connect(industryPage_, &SectorListPage::openConstituents, ...)` 和 conceptPage_ 同）：
```cpp
    connect(industryPage_, &SectorListPage::openConstituents,
            this, &MarketPanel::onOpenConstituents);
    connect(conceptPage_, &SectorListPage::openConstituents,
            this, &MarketPanel::onOpenConstituents);
```
- 实现：
```cpp
void MarketPanel::onOpenConstituents(const QString& name) {
    if (name.isEmpty() || !provider_) return;
    auto* dlg = new SectorConstituentsDialog(provider_, name, this);
    connect(dlg, &SectorConstituentsDialog::openChart,
            this, &MarketPanel::onOpenSectorChart);  // 复用板块开图转发（轻量开图）
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}
```

注意：弹窗双击 → `openChart` → 复用 `MarketPanel::onOpenSectorChart`（轻量开图，不碰右侧面板）——与板块指数开图行为一致；成分股是普通股票，但 v1 复用轻量路径即可（记已知限制，v2 可走 openChart 全路径）。

- [ ] **Step 5: CMake 注册**

`src/CMakeLists.txt` st_ui 加 `ui/widgets/sector_constituents_dialog.cpp`。

- [ ] **Step 6: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告。

- [ ] **Step 7: 提交**

```bash
git add src/ui/widgets/sector_constituents_dialog.h src/ui/widgets/sector_constituents_dialog.cpp \
        src/ui/panels/sector_panel.h src/ui/panels/sector_panel.cpp \
        src/ui/panels/market_panel.h src/ui/panels/market_panel.cpp src/CMakeLists.txt
git commit -m "feat: 板块成分股下钻弹窗（右键→成分股+行情→双击开图）"
```

---

### Task 5: MainWindow — dock 重排 + 视图菜单 + 自选接线

**Files:**
- Modify: `src/ui/main_window.h`
- Modify: `src/ui/main_window.cpp`

**Interfaces:**
- Consumes: Task 2 的 `WatchlistPanel`；Task 3 的 `CentralChartWidget::toggleWatchlist`/`currentCodeChanged`/`setWatchlistButtonChecked`。
- Produces: 无新接口。布局：自选股 dock 占左主位、自定义指数下移、市场 dock 默认隐藏 + 视图菜单 toggle。

- [ ] **Step 1: 改头文件 `main_window.h`**

成员区加：
```cpp
    QDockWidget* marketDock_ = nullptr;      // 市场 dock（视图菜单开关，默认隐藏）
    QDockWidget* watchlistDock_ = nullptr;   // 自选股 dock（左主位）
    class WatchlistPanel* watchlistPanel_ = nullptr;
```
private 加方法：
```cpp
    void openStockChart(const StockCode& code, const QString& name);  // 统一开图（股票：含右侧面板）
    void syncWatchlistButton();  // 图表「加入自选」按钮状态 = 当前股票是否在自选
```

- [ ] **Step 2: 改实现 `main_window.cpp` — dock 重排**

`createDocks()` 改：
1. 市场 dock：`auto* marketDock` → `marketDock_ = new QDockWidget(tr("市场"), this);` 并用 `marketDock_` 替代局部 `marketDock` 引用。`marketPanel_ = new MarketPanel(provider_.get(), marketDock_); marketDock_->setWidget(marketPanel_);`
2. `connect(marketPanel_, &MarketPanel::openChart, this, &MainWindow::openStockChart);`（把现有内联 lambda 换成 `openStockChart` 方法）
3. 市场 dock 与自定义指数 dock 的 `splitDockWidget(marketDock, customIndexDock_, Qt::Vertical)` → 改为 **watchlistDock_ 与 customIndexDock_ 竖排**。顺序：
   - 先创建 `watchlistDock_`（左主位）：
     ```cpp
     watchlistDock_ = new QDockWidget(tr("自选股"), this);
     watchlistDock_->setObjectName(QStringLiteral("watchlistDock"));
     watchlistPanel_ = new WatchlistPanel(provider_.get(), watchlistDock_);
     watchlistDock_->setWidget(watchlistPanel_);
     addDockWidget(Qt::LeftDockWidgetArea, watchlistDock_);
     connect(watchlistPanel_, &WatchlistPanel::openChart, this, &MainWindow::openStockChart);
     ```
   - 市场 dock 创建（`addDockWidget(Left, marketDock_)`，不 split）
   - 自定义指数：`addDockWidget(Left, customIndexDock_)` + `splitDockWidget(watchlistDock_, customIndexDock_, Qt::Vertical)`
4. 保持 `connect(marketPanel_, &MarketPanel::openSectorChart, ...)` 不变。

构造 `restoreState` 后（现第 72-74 行 `if (chipDock_) chipDock_->hide();` 附近）加：
```cpp
    // 市场 dock 默认隐藏（视图→市场 打开；自选股占左主位）
    if (marketDock_) marketDock_->hide();
```

- [ ] **Step 3: 改实现 — 视图菜单**

`createMenus()` 视图菜单（`customIndexDock_` toggle 之后）加：
```cpp
    if (marketDock_) viewMenu->addAction(marketDock_->toggleViewAction());
```

- [ ] **Step 4: 改实现 — 自选接线 + syncWatchlistButton**

`createDocks()` 市场 dock 创建后加：
```cpp
    connect(centralChart_, &CentralChartWidget::toggleWatchlist, this,
            [this](const StockCode& code, const QString& name) {
        if (!watchlistPanel_) return;
        if (watchlistPanel_->contains(code)) {
            watchlistPanel_->remove(code);
            statusBar()->showMessage(tr("已从自选移除 %1").arg(name), 3000);
        } else {
            watchlistPanel_->add(code, name);
            statusBar()->showMessage(tr("已加入自选 %1").arg(name), 3000);
        }
        syncWatchlistButton();
    });
    connect(centralChart_, &CentralChartWidget::currentCodeChanged, this,
            [this](const StockCode&) { syncWatchlistButton(); });
    connect(watchlistPanel_, &WatchlistPanel::watchlistChanged, this,
            [this](const StockCode& code) {
        if (centralChart_ && centralChart_->currentCode().fullCode() == code.fullCode()) {
            syncWatchlistButton();
        }
    });
```

`createCentral()` 或构造尾部定义 `openStockChart` / `syncWatchlistButton`：
```cpp
void MainWindow::openStockChart(const StockCode& code, const QString& name) {
    centralStack_->setCurrentWidget(centralChart_);
    centralChart_->loadStock(code, name.isEmpty()
        ? QString::fromStdString(code.displayCode()) : name);
    refreshTradeMarks();
    if (marketDepth_) marketDepth_->setStock(code, name);
    if (keyData_) keyData_->setStock(code, name);
    if (chipPanel_) chipPanel_->setStock(code, name);
}

void MainWindow::syncWatchlistButton() {
    if (!centralChart_) return;
    const bool in = watchlistPanel_
        ? watchlistPanel_->contains(centralChart_->currentCode()) : false;
    centralChart_->setWatchlistButtonChecked(in);
}
```

把 `connect(marketPanel_, &MarketPanel::openChart, this, [this](...){...})` 原内联 lambda 替换为 `connect(marketPanel_, &MarketPanel::openChart, this, &MainWindow::openStockChart);`（lambda 体与 `openStockChart` 完全一致）。

- [ ] **Step 5: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告。

- [ ] **Step 6: 提交**

```bash
git add src/ui/main_window.h src/ui/main_window.cpp
git commit -m "feat: 市场 dock 收编视图菜单 + 自选股 dock 占左主位 + 图表自选按钮接线"
```

---

### Task 6: 全量验证 + 文档收尾

**Files:**
- Modify: `docs/DEVLOG.md`
- Modify: `docs/changelog.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: 全量构建 + 回归测试**

Run: `cmake --build --preset with-qt && ctest --preset default`
Expected: 零错误零警告 + **395** tests 全绿（392 + 3 WatchlistStore）。

- [ ] **Step 2: 冒烟清单（手动）**

1. 启动：左侧主位是「自选股」dock；市场 dock 默认隐藏
2. 视图→市场 → 市场 dock 弹出（4 tab 正常）；再点收起
3. 图表开股票 → 周期栏「加入自选」→ 按钮变「已在自选」+ 自选股 dock 出现该股（有行情）+ 重启应用仍在（watchlist.json）
4. 换另一只股票 → 按钮变「加入自选」；切回已自选的股票 → 变「已在自选」
5. 自选股行双击 → 开图 + 右侧盘口/关键数据联动
6. 自选股行右键 → 移除自选 → 按钮变「加入自选」
7. 市场窗口行业/概念 tab → 板块行右键「查看成分股」→ 弹窗列出成分股+行情 → 双击开图
8. 关窗不崩（ThreadPool waitForDone 无残留引用）

- [ ] **Step 3: 更新文档**

- `docs/DEVLOG.md` 顶部加条目：P10 第十五轮（自选股列表 + 板块成分股下钻 + 市场收编视图菜单）
- `docs/changelog.md` 加版本说明
- `CLAUDE.md` 当前阶段追加 `→ P10 第十五轮 ✅（自选股列表 + 板块成分股下钻 + 市场收编视图菜单）`；测试数 392 → **395**

- [ ] **Step 4: 提交**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: 自选股 + 板块成分股下钻 + 市场收编视图菜单（P10 第十五轮）文档收尾"
```

- [ ] **Step 5: 收尾说明**

向用户汇报：改动文件清单、验证结果（395 全绿）、冒烟清单，确认后 push GitHub。
