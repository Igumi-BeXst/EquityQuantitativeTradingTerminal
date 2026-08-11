# 市场窗口合并板块窗口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把板块窗口的功能合并进市场窗口：左侧一个「市场」Dock，4 平级 tab（涨幅榜/跌幅榜/行业板块/概念板块）+ 市场宽度常驻底部，统一 30s 错峰刷新，板块行双击打开板块指数 K 线图。

**Architecture:** SectorPanel 重构为固定类型的 SectorListPage（去掉行业/概念按钮组、刷新/导出按钮、自管定时器），MarketPanel 装配两个实例为 tab 并拥有统一错峰时钟；板块表模板同步涨跌幅榜（去硬编码黑底，跟随应用主题）。MainWindow 删除独立板块 Dock，板块开图信号走轻量路径（不设置右侧盘口面板）。

**Tech Stack:** C++17, Qt 6.11 (Widgets/QTableView/QAbstractTableModel), TDX 数据源, ThreadPool 异步 + QPointer 守卫 + QMetaObject::invokeMethod。

设计文档：[2026-08-11-market-sector-merge-design.md](../specs/2026-08-11-market-sector-merge-design.md)

## Global Constraints

- 分层架构，UI 只能依赖 engine/intelligence/data/core/foundation，禁止反向
- 安全异步：禁裸 `this` 捕获；IO 线程捕获 provider 按值 + `QPointer` 守卫 + `QMetaObject::invokeMethod(guard, lambda, Qt::QueuedConnection)`
- 编译零警告：`cmake --build --preset with-qt` 必须零错误零警告
- 回归：`ctest --preset default` 现有 392 tests 必须全绿（本改动纯 UI，无引擎/数据逻辑变更，不新增单测）
- 板块数据源：TDX `getSectorIndices` 全量 + 类型过滤（行业 8803xx-8804xx / 概念 8805xx+）+ `batchQuoteInteractive`
- 现有已验证异步/缓存/seq 去陈旧逻辑原样保留，只做搬运与减化，不重写
- 板块表模板同步涨跌幅榜：去掉 `#18181a` 黑底、`#dddddd`/`#999999` 灰字硬编码，跟随应用主题；涨跌幅列红涨绿跌 `#e54648`/`#2e9e5b` 保留

---

### Task 1: SectorListModel — 携带板块代码 + rowAt + 模板同步

**Files:**
- Modify: `src/ui/models/sector_list_model.h`
- Modify: `src/ui/models/sector_list_model.cpp`

**Interfaces:**
- Produces: `SectorRow` 新增 `StockCode code` 字段；`const SectorRow& SectorListModel::rowAt(int row) const`（越界返回静态空行）。Task 2 依赖 `rowAt` 取行用于双击开图；Task 2 的 `fetchRows` 回填 `code`。

**背景**：板块行双击要打开板块指数（880xxx）K 线图，需要携带板块 `StockCode`。当前 `SectorRow` 只有 name/changePct/amount，需补 `code` 并加 `rowAt` 访问器（仿 `MarketRankModel::itemAt`）。同时把 ForegroundRole 同步成涨跌幅榜模板：名称/成交额列不再硬编码灰字，跟随主题默认色。

- [ ] **Step 1: 改头文件 `sector_list_model.h`**

```cpp
// 文件顶部补 include（在 #include <QAbstractTableModel> 之前）
#include "foundation/stock_code.h"

// struct SectorRow 内加一行：
struct SectorRow {
    StockCode code;             // 板块指数代码（880xxx，双击开图用；不展示）
    QString name;
    double changePct = 0.0;
    double amount = 0.0;
};

// class SectorListModel 内加一个 public 方法（放在 setRows 之后）：
    /// 取第 row 行的引用（越界返回静态空行）
    const SectorRow& rowAt(int row) const;
```

- [ ] **Step 2: 改实现 `sector_list_model.cpp`**

`data()` 的 `ForegroundRole` 分支：只给涨跌幅列上色，名称/成交额列返回主题默认色（删除 `#dddddd`/`#999999` 两行）：

```cpp
        case Qt::ForegroundRole:
            if (index.column() == 1) {  // 涨跌幅列：红涨绿跌（与涨跌幅榜同色值）
                return r.changePct >= 0.0
                    ? QColor(QString::fromUtf8(kUpColor))
                    : QColor(QString::fromUtf8(kDownColor));
            }
            return {};  // 名称/成交额跟随应用主题默认色（模板同步涨跌幅榜，去掉硬编码灰）
```

文件尾部加 `rowAt` 实现（放在 `headerData` 之后、`moc_sector_list_model.cpp` include 之前）：

```cpp
const SectorRow& SectorListModel::rowAt(int row) const {
    static const SectorRow kEmpty;  // 越界返回的空行（code.isValid()==false）
    if (row < 0 || static_cast<size_t>(row) >= rows_.size()) return kEmpty;
    return rows_[static_cast<size_t>(row)];
}
```

- [ ] **Step 3: 构建验证**

Run: `cmake --preset with-qt && cmake --build --preset with-qt`
Expected: 零错误零警告。

- [ ] **Step 4: 提交**

```bash
git add src/ui/models/sector_list_model.h src/ui/models/sector_list_model.cpp
git commit -m "refactor: 板块行携带代码 + rowAt 访问器 + 表格模板同步涨跌幅榜（去硬编码黑底/灰字）"
```

---

### Task 2: SectorPanel 重构为 SectorListPage（固定类型、去 chrome、双击开图）

**Files:**
- Modify: `src/ui/panels/sector_panel.h`（整文件重写类定义）
- Modify: `src/ui/panels/sector_panel.cpp`（整文件重写）
- Modify: `src/ui/main_window.h`（移除 SectorPanel 前置声明与成员——类改名后编译必需）
- Modify: `src/ui/main_window.cpp`（移除 include/板块 Dock/自定义指数改竖排——类改名后编译必需）

**Interfaces:**
- Produces: `class SectorListPage : public QWidget`（文件名不变），构造 `(IDataProvider*, SectorType type, QWidget* = nullptr)`；`void refresh()`；`SectorType type() const`；`QTableView* tableView() const`；signal `void openSectorChart(const StockCode& code, const QString& name)`。Task 3（MarketPanel）消费。
- Consumes: Task 1 的 `SectorRow::code` + `model_->rowAt()`。

**背景**：SectorPanel 从「带类型切换的自包含面板」降级为「固定类型纯列表页」。移除行业/概念按钮组、刷新/导出按钮、`std::map<SectorType,...>` 缓存容器（单类型 → 单值）、定时器/showEvent/hideEvent。保留虚拟化 QTableView、缓存、seq 去陈旧、在途去重、涨跌幅降序、空态、更新时间标签。新增双击开图信号。构造函数**不自动拉取**（统一由 MarketPanel 错峰调度）。

- [ ] **Step 1: 重写头文件 `sector_panel.h`**

整文件替换为（类名 `SectorPanel` → `SectorListPage`）：

```cpp
#pragma once

#include "foundation/stock_code.h"
#include "data/eastmoney_sector_provider.h"  // SectorType
#include "data/idata_provider.h"
#include "ui/models/sector_list_model.h"
#include <QString>
#include <QWidget>
#include <vector>

class QLabel;
class QTableView;
class QStackedWidget;

namespace st {

/// 板块榜单页 — 固定类型（行业/概念）的板块涨跌幅列表（通达信板块指数 880xxx 全量源）
///
/// 数据：IDataProvider::getSectorIndices（全量 8803xx-8804xx 行业 / 8805xx+ 概念，与叠加
/// 对话框同源同过滤）+ batchQuoteInteractive（涨跌幅/成交额）。
/// 展示：QTableView + SectorListModel（虚拟化渲染，大表滚动不卡；模板同步涨跌幅榜，跟随主题）。
/// 刷新：由 MarketPanel 统一错峰时钟调度（本页不自管定时器）。
/// 双击行 → openSectorChart(StockCode, name)：打开板块指数（880xxx）K 线图。
class SectorListPage : public QWidget {
    Q_OBJECT

public:
    explicit SectorListPage(IDataProvider* provider, SectorType type, QWidget* parent = nullptr);

    /// 拉取/刷新本页数据（MarketPanel 统一调度；在途则跳过）
    void refresh();
    SectorType type() const { return type_; }
    QTableView* tableView() const { return table_; }

signals:
    void openSectorChart(const StockCode& code, const QString& name);

private:
    void applyRows(std::vector<SectorRow> rows);
    /// 拉取指定类型板块数据（IO 池，返回未排序行）
    static std::vector<SectorRow> fetchRows(IDataProvider* provider, SectorType type);
    /// 发起异步拉取（在途则跳过）
    void fetch();
    /// 异步回调：seq 去陈旧、更新缓存、显示
    void onRowsReady(int seq, std::vector<SectorRow> rows);

    IDataProvider* provider_ = nullptr;
    SectorType type_ = SectorType::Industry;
    int fetchSeq_ = 0;
    int lastSeq_ = 0;
    bool fetching_ = false;
    std::vector<SectorRow> cache_;

    QTableView* table_ = nullptr;
    SectorListModel* model_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QLabel* updateLabel_ = nullptr;
};

} // namespace st
```

- [ ] **Step 2: 重写实现 `sector_panel.cpp`**

整文件替换为：

```cpp
#include "ui/panels/sector_panel.h"
#include "core/thread_pool.h"
#include "foundation/tick.h"
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QStackedWidget>
#include <QTableView>
#include <QTime>
#include <QVBoxLayout>
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace st {

SectorListPage::SectorListPage(IDataProvider* provider, SectorType type, QWidget* parent)
    : QWidget(parent), provider_(provider), type_(type) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 顶行：更新时间（右上角小字）
    auto* topRow = new QHBoxLayout();
    topRow->addStretch();
    updateLabel_ = new QLabel(tr("--"));
    updateLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    topRow->addWidget(updateLabel_);
    layout->addLayout(topRow);

    // 板块榜单（QTableView + SectorListModel 虚拟化渲染；模板同步涨跌幅榜——不设自定义 stylesheet）
    stack_ = new QStackedWidget(this);
    model_ = new SectorListModel(this);
    table_ = new QTableView(stack_);
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    // 注意：不设置自定义背景色（跟随应用主题，与涨跌幅榜一致）
    stack_->addWidget(table_);
    emptyLabel_ = new QLabel(tr("暂无板块数据"), stack_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("color:#666666;"));
    stack_->addWidget(emptyLabel_);
    layout->addWidget(stack_, 1);

    // 双击板块行 → 打开板块指数 K 线（与指数条行为一致；右侧盘口面板不设置）
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || !model_) return;
        const auto& r = model_->rowAt(idx.row());
        if (r.code.isValid()) emit openSectorChart(r.code, r.name);
    });

    // 构造时不自动拉取：刷新时机统一由 MarketPanel 错峰时钟调度（Task 3）
}

void SectorListPage::refresh() { fetch(); }

void SectorListPage::fetch() {
    if (!provider_ || fetching_) return;  // 在途则跳过（防重叠）
    fetching_ = true;
    const int seq = ++fetchSeq_;
    IDataProvider* provider = provider_;
    QPointer<SectorListPage> guard(this);
    ThreadPool::submitIO([provider, guard, seq, type = type_] {
        auto rows = fetchRows(provider, type);
        QMetaObject::invokeMethod(guard, [guard, seq, rows = std::move(rows)]() mutable {
            guard->onRowsReady(seq, std::move(rows));
        }, Qt::QueuedConnection);
    });
}

void SectorListPage::onRowsReady(int seq, std::vector<SectorRow> rows) {
    fetching_ = false;
    if (seq <= lastSeq_) return;  // 陈旧丢弃
    lastSeq_ = seq;
    cache_ = std::move(rows);
    applyRows(cache_);
}

std::vector<SectorRow> SectorListPage::fetchRows(IDataProvider* provider, SectorType type) {
    std::vector<SectorRow> rows;
    if (!provider) return rows;
    // 通达信板块指数全量（与叠加对话框同源同过滤：行业 8803xx-8804xx，概念 8805xx+）
    auto isType = [type](const std::string& c) {
        if (c.size() < 6) return false;
        if (type == SectorType::Industry) {
            return c.compare(0, 3, "880") == 0 && c >= "880300" && c < "880500";
        }
        return c.compare(0, 3, "880") == 0 && c >= "880500";
    };
    const auto sectors = provider->getSectorIndices();
    std::vector<StockCode> codes;
    std::vector<QString> names;
    codes.reserve(sectors.size());
    names.reserve(sectors.size());
    for (const auto& s : sectors) {
        if (!isType(s.code.code())) continue;
        codes.push_back(s.code);
        names.push_back(QString::fromUtf8(s.name.c_str()));
    }
    auto quotes = provider->batchQuoteInteractive(codes);
    std::unordered_map<std::string, const Quote*> qmap;
    qmap.reserve(quotes.size());
    for (const auto& q : quotes) qmap[q.code.displayCode()] = &q;

    rows.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        SectorRow r;
        r.code = codes[i];      // 板块指数代码（双击开图用）
        r.name = names[i];
        const auto it = qmap.find(codes[i].displayCode());
        if (it != qmap.end() && it->second->lastPrice > 0) {
            r.changePct = it->second->change;
            r.amount = it->second->amount;
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

void SectorListPage::applyRows(std::vector<SectorRow> rows) {
    const bool hasData = !rows.empty();  // 先判空（下面会 move 走）
    // 涨跌幅降序（全量展示，可滚动）
    std::sort(rows.begin(), rows.end(),
              [](const SectorRow& a, const SectorRow& b) {
                  return a.changePct > b.changePct;
              });
    if (model_) model_->setRows(std::move(rows));  // 模型一次 reset，虚拟化渲染

    stack_->setCurrentWidget(hasData
        ? static_cast<QWidget*>(table_)
        : static_cast<QWidget*>(emptyLabel_));
    const auto now = QTime::currentTime().toString("HH:mm:ss");
    updateLabel_->setText(hasData ? tr("更新 %1").arg(now)
                                  : tr("获取失败 %1").arg(now));
}

} // namespace st

#include "moc_sector_panel.cpp"
```

注意：`moc_sector_panel.cpp` 的 include 行保留（Q_OBJECT 类的 moc 文件名由源文件名决定，`sector_panel.cpp` 仍对应 `moc_sector_panel.cpp`，类名改了但文件名没变）。

- [ ] **Step 3: 同步清理 MainWindow 的 SectorPanel 引用（类改名后编译必需）**

类改名后 `main_window` 仍引用旧类名 `SectorPanel`，必须同任务清理，否则整体编译失败（不能留到 Task 4，违反每任务可编译原则）。改 `src/ui/main_window.h`：

```cpp
// 前置声明区删除：
class SectorPanel;
// 成员区删除：
    SectorPanel* sectorPanel_ = nullptr;
```

改 `src/ui/main_window.cpp`：
- 删除 `#include "ui/panels/sector_panel.h"`（第 9 行）
- 删除 `createDocks()` 中整个板块 Dock 块（现第 261-269 行，含注释）：

```cpp
    // 左: 板块前十榜单（市场面板下方；行业/概念涨跌幅 Top10 简单列表）
    // [BISECT] 定位关闭堆损坏时曾临时禁用；根因实为陈旧对象/ABI 错位（全量重建已修复），面板本身无问题。
    auto* sectorDock = new QDockWidget(tr("板块"), this);
    sectorDock->setObjectName(QStringLiteral("sectorDock"));
    sectorPanel_ = new SectorPanel(provider_.get(), sectorDock);
    sectorDock->setWidget(sectorPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, sectorDock);
    splitDockWidget(marketDock, sectorDock, Qt::Vertical);
    sectorDock->setMinimumWidth(260);
```

- 自定义指数 Dock 的放置从「与板块 tabify」改为「与市场竖排 split」（现第 271-278 行；同块删除 `tabifyDockWidget(sectorDock, ...)` 引用，否则编译错）：

```cpp
    // 左: 自定义指数（独立 Dock，与市场竖排；建/编/删 + 实时点位 + 打开图表）
    customIndexDock_ = new QDockWidget(tr("自定义指数"), this);
    customIndexDock_->setObjectName(QStringLiteral("customIndexDock"));
    customIndexPanel_ = new CustomIndexPanel(provider_.get(), customIndexDock_);
    customIndexDock_->setWidget(customIndexPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, customIndexDock_);
    splitDockWidget(marketDock, customIndexDock_, Qt::Vertical);  // 独立竖排（不再与板块 tabify）
    customIndexDock_->setMinimumWidth(260);
```

- [ ] **Step 4: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告（MainWindow 引用已在本任务清理，不应再有 `SectorPanel` 未定义错误）。

- [ ] **Step 5: 提交**

```bash
git add src/ui/panels/sector_panel.h src/ui/panels/sector_panel.cpp
git commit -m "refactor: SectorPanel 重构为 SectorListPage（固定类型/去chrome/双击开图信号）"
git add src/ui/main_window.h src/ui/main_window.cpp
git commit -m "refactor: 移除 MainWindow 板块 Dock（SectorPanel 类改名后的编译必需清理）"
```

---

### Task 3: MarketPanel — 4 平级 tab + 统一错峰刷新 + 板块开图转发

**Files:**
- Modify: `src/ui/panels/market_panel.h`
- Modify: `src/ui/panels/market_panel.cpp`

**Interfaces:**
- Consumes: Task 2 的 `SectorListPage`（构造/`refresh()`/`tableView()`/`openSectorChart`）。
- Produces: `MarketPanel` 新增 signal `void openSectorChart(const StockCode&, const QString&)`（Task 4 MainWindow 连接）；`refresh()` 语义扩展为「市场池 + 板块错峰」。

**背景**：市场窗口变 4 tab。统一 30s 时钟沿用现有 `timer_`，`refresh()` 在市场池提交后追加 `QTimer::singleShot` 错峰触发行业/概念页刷新（板块走 `batchQuoteInteractive` 交互优先级，可与批量并存）。导出按当前 tab 分流。切到板块 tab 立即后台刷新。

- [ ] **Step 1: 改头文件 `market_panel.h`**

```cpp
// 前置声明区加一行（SectorListPage 是 UI 类，前向声明即可）：
class SectorListPage;

// signals 区加一个：
signals:
    void openChart(const StockCode& code, const QString& name);
    /// 板块指数开图（双击板块行；右侧盘口/关键数据/筹码面板保持不动）
    void openSectorChart(const StockCode& code, const QString& name);

// private 区加一个槽 + 两个成员：
private:
    void onOpenSectorChart(const StockCode& code, const QString& name);
    // ...
    SectorListPage* industryPage_ = nullptr;   // 行业板块 tab
    SectorListPage* conceptPage_ = nullptr;    // 概念板块 tab
```

- [ ] **Step 2: 改实现 `market_panel.cpp` — 导出分流 + 新增板块 tab**

文件顶部 include 区补：

```cpp
#include "data/eastmoney_sector_provider.h"  // SectorType
#include "ui/panels/sector_panel.h"          // SectorListPage
```

现有导出 handler（第 54-61 行）替换为按当前 tab 分流：

```cpp
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        QAbstractItemView* view = nullptr;
        switch (tabs_->currentIndex()) {
            case 0: view = gainersView_; break;
            case 1: view = losersView_; break;
            case 2: view = industryPage_ ? industryPage_->tableView() : nullptr; break;
            case 3: view = conceptPage_ ? conceptPage_->tableView() : nullptr; break;
            default: break;
        }
        if (view) st::ui::exportViewToCsv(view, this, "market_ranking.csv");
    });
```

跌幅榜 tab 添加之后（现有第 88-89 行 `onLosersDoubleClicked` connect 之后）插入板块 tab：

```cpp
    connect(losersView_, &QTableView::doubleClicked,
            this, &MarketPanel::onLosersDoubleClicked);

    // 板块榜单页（固定类型；TDX 全量；刷新由统一错峰时钟调度，见 refresh()）
    industryPage_ = new SectorListPage(provider_, SectorType::Industry, this);
    tabs_->addTab(industryPage_, tr("行业板块"));
    conceptPage_ = new SectorListPage(provider_, SectorType::Concept, this);
    tabs_->addTab(conceptPage_, tr("概念板块"));
    connect(industryPage_, &SectorListPage::openSectorChart,
            this, &MarketPanel::onOpenSectorChart);
    connect(conceptPage_, &SectorListPage::openSectorChart,
            this, &MarketPanel::onOpenSectorChart);
    // 切到板块 tab：立即后台刷新（缓存已由错峰轮询保持新鲜，此刷新覆盖首次/过期数据）
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 2 && industryPage_) industryPage_->refresh();
        else if (index == 3 && conceptPage_) conceptPage_->refresh();
    });
```

- [ ] **Step 3: 改实现 `market_panel.cpp` — 统一错峰刷新**

`refresh()`（现有第 149-165 行）在市场池 `submitIO` 之后追加错峰调度：

```cpp
void MarketPanel::refresh() {
    if (refreshing_ || !provider_) return;
    refreshing_ = true;
    const int gen = ++gen_;
    std::vector<StockCode> pool = pool_;
    // 安全异步：按值捕获 provider + QPointer 守卫投递回主线程
    QPointer<MarketPanel> guard(this);
    IDataProvider* provider = provider_;
    ThreadPool::submitIO([provider, guard, gen, pool] {
        auto quotes = provider->batchQuote(pool);
        QMetaObject::invokeMethod(guard, [guard, gen, quotes = std::move(quotes)]() mutable {
            guard->refreshing_ = false;
            if (gen != guard->gen_) return;  // 陈旧回写丢弃
            guard->onQuotesReady(quotes);
        }, Qt::QueuedConnection);
    });

    // 统一错峰：市场池 t=0（上面），行业 +1s，概念 +2s（板块走 batchQuoteInteractive
    // 交互优先级，不与批量队列竞争；singleShot 以 this 为 context，本面板销毁即取消）
    QTimer::singleShot(1000, this, [this] { if (industryPage_) industryPage_->refresh(); });
    QTimer::singleShot(2000, this, [this] { if (conceptPage_) conceptPage_->refresh(); });
}
```

- [ ] **Step 4: 加板块开图转发**

`onQuotesReady` 之后加：

```cpp
void MarketPanel::onOpenSectorChart(const StockCode& code, const QString& name) {
    // 板块指数开图：转发给 MainWindow 的轻量处理（只 loadStock，不设置右侧面板）
    emit openSectorChart(code, name);
}
```

- [ ] **Step 5: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告。

- [ ] **Step 6: 提交**

```bash
git add src/ui/panels/market_panel.h src/ui/panels/market_panel.cpp
git commit -m "feat: 市场窗口 4 平级 tab（涨幅/跌幅/行业/概念）+ 统一错峰刷新 + 板块开图转发"
```

---

### Task 4: MainWindow — 连接板块开图 + 收敛定时刷新

**Files:**
- Modify: `src/ui/main_window.cpp`

**Interfaces:**
- Consumes: Task 3 的 `MarketPanel::openSectorChart` 信号。
- Produces: 无新接口。

**背景**：Task 2 已删除板块 Dock、`sectorPanel_` 成员、SectorPanel 引用（类改名编译必需）。本任务补上剩余接线：① 板块双击开图连接——只 `loadStock` + `refreshTradeMarks`，不设置右侧盘口/关键数据/筹码（与指数条 `indexClicked` 行为一致）；② `runScheduledTask` 的 `RefreshQuotes` 刷新引用收敛为 `marketPanel_->refresh()`（内部已错峰覆盖市场池 + 行业 + 概念）。

- [ ] **Step 1: 改实现 `main_window.cpp` — 板块开图连接**

`marketDock` 创建后、`connect(marketPanel_, &MarketPanel::openChart, ...)` 之后追加：

```cpp
    // 板块行双击 → 中央图表打开板块指数 K 线（与指数条行为一致：只开图，不动右侧面板）
    connect(marketPanel_, &MarketPanel::openSectorChart, this,
            [this](const StockCode& code, const QString& name) {
                centralStack_->setCurrentWidget(centralChart_);
                centralChart_->loadStock(code, name.isEmpty()
                    ? QString::fromStdString(code.displayCode()) : name);
                refreshTradeMarks();
            });
```

- [ ] **Step 2: 改实现 `main_window.cpp` — 定时刷新收敛**

`runScheduledTask` 的 `RefreshQuotes` 分支（原第 538-544 行；Task 2 删除板块 Dock 后行号可能偏移，按内容 `if (sectorPanel_) sectorPanel_->refresh();` 匹配）删除 `sectorPanel_` 行：

```cpp
    case ScheduledTaskType::RefreshQuotes:
        if (marketPanel_) marketPanel_->refresh();   // 内部已错峰覆盖市场池 + 行业 + 概念
        t.lastResult = "刷新行情完成";
        break;
```

- [ ] **Step 3: 全仓清理旧类名引用**

Run: `grep -rn "sectorPanel_\|SectorPanel" src/`
Expected: 无残留（Task 2 已清理）。若有遗漏，逐一改名为 `SectorListPage` 并修正用法。

- [ ] **Step 4: 构建验证**

Run: `cmake --build --preset with-qt`
Expected: 零错误零警告。

- [ ] **Step 5: 提交**

```bash
git add src/ui/main_window.cpp
git commit -m "refactor: 板块开图轻量连接 + 定时刷新收敛为 marketPanel（板块并入市场收尾）"
```

---

### Task 5: 全量验证 + 文档收尾

**Files:**
- Modify: `docs/DEVLOG.md`
- Modify: `docs/changelog.md`
- Modify: `CLAUDE.md`（如需更新当前阶段描述）

- [ ] **Step 1: 全量构建 + 回归测试**

Run: `cmake --build --preset with-qt && ctest --preset default`
Expected: 零错误零警告 + 392 tests 全绿。

- [ ] **Step 2: 冒烟清单（手动）**

在 with-qt 构建的 StockTerminal 上逐项验证：

1. 左侧只剩「市场」和「自定义指数」两个 Dock，板块独立 Dock 消失
2. 市场 Dock 4 tab：涨幅榜/跌幅榜/行业板块/概念板块 切换正常
3. 行业（132 行）/概念（438 行）全量列表滚动不卡（虚拟化仍生效）
4. 板块表观感与涨幅榜一致：无黑底、表头跟随主题、涨跌幅红涨绿跌
5. 板块行双击 → 中央图表打开对应板块 K 线（880xxx）；右侧盘口/关键数据/筹码不被设置
6. 刷新错峰：观察两板块页更新时间戳先后间隔 ~1s；市场池刷新期间无卡顿
7. 手动刷新按钮、30s 定时刷新均正常；切到板块 tab 缓存即现
8. 关窗不崩（ThreadPool waitForDone 前无残留 sectorPanel_ 引用）

- [ ] **Step 3: 更新文档**

- `docs/DEVLOG.md` 顶部加条目：市场窗口合并板块（4 tab + 统一错峰刷新 + 板块双击开图 + 模板同步）
- `docs/changelog.md` 加版本说明（同上要点）
- `CLAUDE.md` 当前阶段描述追加本轮（如测试数不变则不动 392）

- [ ] **Step 4: 提交**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: 市场窗口合并板块窗口（P10 第十四轮）文档收尾"
```

- [ ] **Step 5: 收尾说明**

向用户汇报：改动文件清单、验证结果、冒烟情况，确认后 push GitHub。
