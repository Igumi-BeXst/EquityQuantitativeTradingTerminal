# 全量 CSV 导出 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 6 个主要数据面板加 CSV 导出（UTF-8 with BOM，Excel 可打开中文正常），统一用通用工具函数。

**Architecture:** 新建 UI 工具层 `table_csv_export`（接收 QAbstractItemView 公共基类，同时覆盖 QTableView 和 QTableWidget）；核心「表头/单元格 → CSV 行」逻辑抽纯函数可单测；6 面板各加「导出」按钮调用。

**Tech Stack:** C++17 / Qt 6 Widgets / GoogleTest（csv 核心逻辑 +5 例）。

## Global Constraints

- 分层：UI 工具层（依赖 Qt）不碰引擎；csv 核心逻辑可单测
- 编译零警告：每次修改后 `cmake --build --preset with-qt`（或 build.bat）零错误零警告
- 测试：387 → **392**（+5 csv 导出单测），`ctest --preset default` 全绿
- 改头文件后 clean rebuild（项目记忆：陈旧对象导致堆/栈损坏）
- 安全异步：无新增异步（导出是同步 IO，少量数据直接写文件）
- UTF-8 with BOM：Excel 中文不乱码（`\xEF\xBB\xBF` 前缀）

---

### Task 1: csv 核心逻辑纯函数 + 单测

**Files:**
- Modify: `src/foundation/utils/csv.h`
- Modify: `src/foundation/utils/csv.cpp`
- Create: `tests/test_foundation/test_csv_export.cpp`
- Modify: `tests/CMakeLists.txt`（test_foundation 源列表加 test_csv_export.cpp）

**Interfaces:**
- Consumes: `st::csv::joinRow`（已存在）
- Produces:
  ```cpp
  /// 表头 + 行数据 → CSV 文本（含 UTF-8 BOM）
  /// rows 每行为一列字符串；首行视为表头
  std::string tableToCsv(const std::vector<std::vector<std::string>>& rows);
  ```

- [ ] **Step 1: 写失败测试**

`tests/test_foundation/test_csv_export.cpp`：

```cpp
#include "foundation/utils/csv.h"
#include "gtest/gtest.h"

using namespace st;

TEST(CsvExportTest, BasicTable) {
    const std::vector<std::vector<std::string>> rows = {
        {"代码", "名称", "价格"},
        {"600519", "贵州茅台", "1500.00"},
        {"000858", "五粮液", "130.50"},
    };
    const auto csv = csv::tableToCsv(rows);
    EXPECT_EQ(csv,
              std::string("\xEF\xBB\xBF") +          // UTF-8 BOM
              "代码,名称,价格\n"
              "600519,贵州茅台,1500.00\n"
              "000858,五粮液,130.50\n");
}

TEST(CsvExportTest, EscapesCommaAndQuote) {
    const std::vector<std::vector<std::string>> rows = {
        {"字段", "值"},
        {"注释", "他说\"好的\",然后离开"},   // 逗号+引号 → 转义
    };
    const auto csv = csv::tableToCsv(rows);
    EXPECT_NE(csv.find("\"他说\"\"好的\"\",然后离开\""), std::string::npos);
}

TEST(CsvExportTest, EmptyReturnsBomOnly) {
    const auto csv = csv::tableToCsv({});
    EXPECT_EQ(csv, std::string("\xEF\xBB\xBF"));
}

TEST(CsvExportTest, NumericStringsNotScientific) {
    const std::vector<std::vector<std::string>> rows = {
        {"值"},
        {"1500.000000"},
    };
    // 纯字符串导出不引入科学计数（调用方已格式化）
    EXPECT_EQ(csv::tableToCsv(rows), std::string("\xEF\xBB\xBF") + "值\n1500.000000\n");
}

TEST(CsvExportTest, MultiLineCellWrapped) {
    const std::vector<std::vector<std::string>> rows = {
        {"备注"},
        {"第一行\n第二行"},
    };
    const auto csv = csv::tableToCsv(rows);
    // 含换行的字段 → 包引号（joinRow 已处理）；BOM + 表头 + 行
    EXPECT_NE(csv.find("\"第一行\n第二行\""), std::string::npos);
}
```

- [ ] **Step 2: 运行测试验证失败**

```bash
cmake --build --preset with-qt 2>&1 | tail -3
ctest --preset default -R CsvExportTest
```
Expected: 编译失败（tableToCsv 未声明）。

- [ ] **Step 3: 实现 tableToCsv**

`csv.h` 加声明；`csv.cpp` 加实现（含 BOM）：

```cpp
std::string tableToCsv(const std::vector<std::vector<std::string>>& rows) {
    std::ostringstream os;
    os << "\xEF\xBB\xBF";   // UTF-8 BOM：Excel 中文不乱码
    for (const auto& row : rows) {
        os << joinRow(row) << '\n';
    }
    return os.str();
}
```

- [ ] **Step 4: 运行测试验证通过**

```bash
cmake --build --preset with-qt 2>&1 | tail -3
ctest --preset default -R CsvExportTest
```
Expected: 5 例全过；全量 ctest 387 → 392。

- [ ] **Step 5: Commit**

```bash
git add src/foundation/utils/csv.h src/foundation/utils/csv.cpp tests/test_foundation/test_csv_export.cpp tests/CMakeLists.txt
git commit -m "feat: CSV 表格导出核心函数（UTF-8 BOM）+ 单测"
```

---

### Task 2: 通用视图导出工具

**Files:**
- Create: `src/ui/utils/table_csv_export.h`
- Create: `src/ui/utils/table_csv_export.cpp`
- Modify: `src/CMakeLists.txt`（st_ui 加 `ui/utils/table_csv_export.cpp`）

**Interfaces:**
- Consumes: `csv::tableToCsv`（Task 1）、`QAbstractItemView`/`QFileDialog`/`QFile`
- Produces:
  ```cpp
  /// 把 QAbstractItemView（QTableView/QTableWidget 公共基类）当前内容导出为 CSV
  /// 表头 = model->headerData(横向)；单元格 = model->data(index, DisplayRole)
  std::string tableViewToCsv(const QAbstractItemView* view);

  /// 弹保存对话框并写文件；返回是否成功
  bool exportViewToCsv(QAbstractItemView* view, QWidget* parent,
                       const QString& defaultName);
  ```

- [ ] **Step 1: 头文件 + 实现**

`table_csv_export.h`：

```cpp
#pragma once

#include <QString>
#include <string>

class QAbstractItemView;
class QWidget;

namespace st::ui {

/// 把 QAbstractItemView（QTableView/QTableWidget 公共基类）当前内容导出为 CSV
/// 表头 = model->headerData(横向)；单元格 = model->data(index, DisplayRole)
/// 返回 CSV 文本（含 UTF-8 BOM）；空表格返回仅 BOM
std::string tableViewToCsv(const QAbstractItemView* view);

/// 弹保存对话框并写文件（UTF-8 BOM）；返回是否成功
/// parent 为空则无父窗口；defaultName 为建议文件名
bool exportViewToCsv(QAbstractItemView* view, QWidget* parent,
                     const QString& defaultName);

} // namespace st::ui
```

`table_csv_export.cpp`：

```cpp
#include "ui/utils/table_csv_export.h"
#include "foundation/utils/csv.h"
#include "core/log_manager.h"
#include "core/app_paths.h"
#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QModelIndex>
#include <QWidget>

namespace st::ui {

std::string tableViewToCsv(const QAbstractItemView* view) {
    if (!view || !view->model()) return csv::tableToCsv({});
    const auto* model = view->model();
    std::vector<std::vector<std::string>> rows;
    const int cols = model->columnCount();
    if (cols <= 0) return csv::tableToCsv({});
    // 表头
    std::vector<std::string> header;
    header.reserve(static_cast<size_t>(cols));
    for (int c = 0; c < cols; ++c) {
        header.push_back(model->headerData(c, Qt::Horizontal).toString().toStdString());
    }
    rows.push_back(std::move(header));
    // 数据行
    const int rCount = model->rowCount();
    for (int r = 0; r < rCount; ++r) {
        std::vector<std::string> row;
        row.reserve(static_cast<size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            row.push_back(model->data(model->index(r, c)).toString().toStdString());
        }
        rows.push_back(std::move(row));
    }
    return csv::tableToCsv(rows);
}

bool exportViewToCsv(QAbstractItemView* view, QWidget* parent,
                     const QString& defaultName) {
    if (!view || !view->model() || view->model()->rowCount() <= 0) {
        // 空表格：提示并返回
        return false;
    }
    const QString defaultPath = QString::fromStdString(AppPaths::dataDir() + "/") + defaultName;
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("导出 CSV"), defaultPath, QObject::tr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LogManager::instance()->log(LogLevel::Warn, "CSV 导出失败: {}", path.toStdString());
        return false;
    }
    const std::string csv = tableViewToCsv(view);
    file.write(csv.data(), static_cast<qint64>(csv.size()));
    LogManager::instance()->log(LogLevel::Info, "已导出 CSV {} 行: {}", view->model()->rowCount(),
                                path.toStdString());
    return true;
}

} // namespace st::ui
```

- [ ] **Step 2: CMake 注册**

`src/CMakeLists.txt` 的 st_ui 源列表加 `ui/utils/table_csv_export.cpp`。

- [ ] **Step 3: 编译验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -3
```
Expected: 零警告。（新文件 + CMakeLists，需 configure 识别）

- [ ] **Step 4: Commit**

```bash
git add src/ui/utils/table_csv_export.h src/ui/utils/table_csv_export.cpp src/CMakeLists.txt
git commit -m "feat: 通用表格视图 CSV 导出工具（QTableView/QTableWidget 统一）"
```

---

### Task 3: 6 面板接入导出按钮

**Files:**
- Modify: `src/ui/panels/screener_panel.{h,cpp}` — 加「导出」按钮 → exportViewToCsv(resultView_)
- Modify: `src/ui/panels/market_panel.{h,cpp}` — 加「导出」按钮 → 当前 tab（涨幅榜 gainersView_ / 跌幅榜 losersView_）
- Modify: `src/ui/panels/sector_panel.{h,cpp}` — 加「导出」按钮 → exportViewToCsv(table_)
- Modify: `src/ui/panels/funds_window.{h,cpp}` — 龙虎榜/融资融券两子面板各加「导出」
- Modify: `src/ui/panels/journal_window.{h,cpp}` — 交易记录 tab 加「导出」
- Modify: `src/ui/panels/backtest_panel.{h,cpp}` — 现有导出统一到新工具（可选，Task 3 或独立）

**Interfaces:**
- Consumes: `exportViewToCsv`（Task 2）、各面板 view 成员
- Produces: 各面板新增「导出」按钮 + 点击处理

- [ ] **Step 1: ScreenerPanel**

`screener_panel.cpp` 顶部按钮行（runBtn_ 附近）加：

```cpp
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        st::ui::exportViewToCsv(resultView_, this, "screener_result.csv");
    });
    // 加入 runBtn_ 所在按钮行
```

include 加 `ui/utils/table_csv_export.h`。

- [ ] **Step 2: MarketPanel**

市场面板有 tabs_（涨幅榜/跌幅榜）。在 topRow 加「导出」按钮，点击导出**当前 tab**：

```cpp
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        QAbstractItemView* view = (tabs_->currentIndex() == 0)
            ? static_cast<QAbstractItemView*>(gainersView_)
            : static_cast<QAbstractItemView*>(losersView_);
        st::ui::exportViewToCsv(view, this, "market_ranking.csv");
    });
    topRow->addWidget(exportBtn);
```

- [ ] **Step 3: SectorPanel**

`sector_panel.cpp` topRow（refreshBtn_ 附近）加「导出」：

```cpp
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        st::ui::exportViewToCsv(table_, this, "sector_quotes.csv");
    });
    topRow->addWidget(exportBtn);
```

- [ ] **Step 4: FundsWindow（两个子面板各一按钮）**

`FundsDragonTigerPanel`（龙虎榜）：构造函数按钮行加「导出」→ `exportViewToCsv(table_, this, "funds_lhb.csv")`
`FundsMarginPanel`（融资融券）：同样加「导出」→ `exportViewToCsv(table_, this, "funds_rzrq.csv")`
（两个子面板各含 `table_` 成员，各加一个按钮，比主窗口共享按钮清晰）

- [ ] **Step 5: JournalWindow**

交易记录 tab（recordsPage）顶部加「导出」→ `exportViewToCsv(recordsTable_, this, "journal_records.csv")`。

- [ ] **Step 6: BacktestPanel 统一到新工具**

现有 `exportBtn_`（backtest_panel.cpp:123）点击逻辑（手工写 csv）替换为 `st::ui::exportViewToCsv(tradesView_, this, "backtest_trades.csv")`（或保留现有逻辑——若现有导出含更多列/汇总，则不动；以不破坏现有功能为准，实现时判断）。

- [ ] **Step 7: 编译验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -3
```
Expected: 零警告。改多个头文件 → 若遇陈旧对象崩溃 `--clean-first`。

- [ ] **Step 8: Commit**

```bash
git add src/ui/panels/screener_panel.h src/ui/panels/screener_panel.cpp src/ui/panels/market_panel.h src/ui/panels/market_panel.cpp src/ui/panels/sector_panel.h src/ui/panels/sector_panel.cpp src/ui/panels/funds_window.h src/ui/panels/funds_window.cpp src/ui/panels/journal_window.h src/ui/panels/journal_window.cpp src/ui/panels/backtest_panel.h src/ui/panels/backtest_panel.cpp
git commit -m "feat: 6 面板 CSV 导出按钮（选股/市场/板块/资金/日志/回测）"
```

---

### Task 4: 文档收尾 + 全量验证

**Files:**
- Modify: `docs/DEVLOG.md`
- Modify: `docs/changelog.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: DEVLOG / changelog / CLAUDE.md 更新**

- `docs/DEVLOG.md` 顶部加 P10 第十三轮条目（需求/实施/验证/已知限制）
- `docs/changelog.md` 加版本说明
- `CLAUDE.md`：阶段标记加「P10 第十三轮 ✅（全量 CSV 导出：6 面板 + UTF-8 BOM）」；测试数 387 → 392

- [ ] **Step 2: 全量验证**

```bash
cmake --build --preset with-qt 2>&1 | tail -3   # 零警告
ctest --preset default                          # 392 全绿
```

- [ ] **Step 3: 收尾提交**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: 全量 CSV 导出（P10 第十三轮）文档收尾"
```

- [ ] **Step 4: 终验 + 分支收尾**

构建零警告 + ctest 392 全绿。完成后用 finishing-a-development-branch 流程。

---

## 风险与对策

- **QTableWidget 也走 model()**：QTableWidget 内部是 model，`headerData`/`data` 正常返回（已验证 funds/journal 用 QTableWidget）；若某列 headerData 为空（未设表头）→ 用列号兜底（`QString::number(c)`）
- **空表格**：`exportViewToCsv` 返回 false（不弹保存框），避免导出空文件
- **BOM 与单测**：`tableToCsv` 输出 BOM，测试用 `std::string("\xEF\xBB\xBF")` 字面断言
- **回测现有导出**：若现有逻辑含额外列/汇总（不只表格），保留原逻辑不动；仅当它等价于表格导出时才统一（实现时判断，避免破坏）
- **市场面板当前 tab**：涨幅榜/跌幅榜用 `tabs_->currentIndex()` 区分（v1 简化）
- **陈旧对象**：改多个面板头文件后 clean rebuild

## 收尾
- 用户冒烟后 push GitHub；更新 CLAUDE.md/DEVLOG/changelog
