# 全量 CSV 导出 — 设计文档

## 背景

需求文档「工具 (P8)」中**数据导出CSV/Excel**为剩余项。现有导出能力仅覆盖：回测面板（`exportBtn_`，backtest_panel.cpp）+ K线单图表（`KLineChart::exportData`）。选股结果、市场全景、板块行情、资金数据、交易日志等主要数据面板**均无导出**。

用户选定下一轮 = 全量 CSV 导出，6 个面板全覆盖。

## 目标

为 6 个主要数据面板加「导出」按钮，导出当前表格内容为 CSV（UTF-8，Excel 可打开）。统一用通用工具函数，避免每个面板重复实现。

## 通用 CSV 导出工具

**新建 `src/ui/utils/table_csv_export.h/.cpp`**（UI 工具层，含 Qt 依赖）：

```cpp
/// 把 QAbstractItemView（QTableView / QTableWidget 公共基类）当前内容导出为 CSV
/// 表头 = model->headerData(横向)；单元格 = model->data(index, DisplayRole)
/// 返回 CSV 文本（UTF-8）；空表格返回空串
std::string tableViewToCsv(const QAbstractItemView* view);

/// 弹保存对话框并写文件（父窗口、默认文件名、成功提示）
/// 返回是否成功
bool exportViewToCsv(QAbstractItemView* view, QWidget* parent,
                     const QString& defaultName);
```

**实现**：
- `tableViewToCsv`：`const auto* model = view->model();` 列数 `model->columnCount()`，行数 `model->rowCount()`；表头用 `model->headerData(col, Qt::Horizontal).toString()`；单元格 `model->data(model->index(r, c)).toString()`；复用 `st::csv::joinRow` 转义；`\n` 换行
- `exportViewToCsv`：`QFileDialog::getSaveFileName`（过滤 `CSV 文件 (*.csv)`）→ `QFile::write` → `LogManager` 记录 → 状态提示（可空）

**为什么 QAbstractItemView**：QTableView（回测/选股/市场/板块用 model）和 QTableWidget（资金/日志用 item）都是其子类，`model()` 统一访问——一个函数两种表格都覆盖。

## 6 面板接入

每个面板加「导出」按钮（放工具栏/按钮行），点击 → `exportViewToCsv(view, this, 默认名)`：

| 面板 | 表格视图 | 默认文件名 | 备注 |
|------|----------|-----------|------|
| 选股结果 ScreenerPanel | `resultView_` (QTableView) | `screener_result.csv` | 结果表格 |
| 市场全景 MarketPanel | 涨幅榜表格 | `market_ranking.csv` | 需定位榜单 view |
| 板块行情 SectorPanel | 板块表格 | `sector_quotes.csv` | 需定位表格 view |
| 资金数据 FundsWindow | `table_` (QTableWidget) | `funds_lhb.csv` / `funds_rzrq.csv` | 多 tab 各一表格 |
| 交易日志 JournalWindow | `recordsTable_` (QTableWidget) | `journal_records.csv` | 交易记录 tab |
| 回测结果 BacktestPanel | `tradesView_` (QTableView) | 复用现有导出（统一到新工具） | 已有 exportBtn_ |

**接入方式**：优先复用现有按钮（回测已有），其余面板新增小按钮「导出」→ `exportViewToCsv`。

## 设计决策

- **只做 CSV 不做 Excel**：CSV 是 Excel 可直接打开的通用格式，需求「CSV/Excel」用 CSV 覆盖（用户确认）；引入 xlsx 库（libxlsxwriter 等）工作量大，v2 可选
- **导出当前表格内容**：不是底层数据模型的全量——所见即所得（含当前排序/筛选），符合用户预期
- **UTF-8 编码**：Excel 打开中文需注意 BOM（`\xEF\xBB\xBF` 前缀）——**加 UTF-8 BOM** 保证 Excel 正确识别中文
- **空表格**：返回空/提示「无数据可导出」

## 测试

- `csv` 工具层可单测（纯字符串逻辑）：`tableViewToCsv` 依赖 Qt model，难单测；**把「表头/单元格 → CSV 行」的核心逻辑抽为纯函数**可测
- 新增 `tests/test_foundation/test_csv_export.cpp`：表头+数据 → CSV 文本断言（含中文/逗号转义/BOM）
- 预计 +5 例 → 当前 387 → **392**

## 已知限制 / 决策

- **Excel(.xlsx) 不做**（v1 只 CSV；CSV 可被 Excel 打开）
- **BOM**：UTF-8 with BOM，Excel 中文不乱码
- **回测面板**：现有导出逻辑统一迁移到新工具（行为一致）
- **资金多 tab**：龙虎榜 + 融资融券各一个「导出」按钮（或共享按钮按当前 tab 导出，v1 各按钮更明确）

## 验收

- 构建零警告 + ctest 392 全绿
- 手动：6 面板各导出 → CSV 文件用 Excel 打开，中文正常、列对齐、逗号/引号字段转义正确
