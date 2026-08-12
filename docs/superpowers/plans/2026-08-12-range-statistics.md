# K线区间统计 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** K线图上拖拽选择区间，弹出全套区间统计表格（涨跌幅/最高最低/振幅/天数/量额/换手/均价），选区高亮保留、多窗口自动生效。

**Architecture:** 统计逻辑放 engine 层纯函数 `computeRangeStats`（可单测）；KLineChart 复用现有 `DrawMode` 框架加 `Range` 模式（控制条按钮 + 拖拽选区 + 高亮绘制 + `rangeSelected` 信号）；新 `RangeStatsDialog` 弹窗表格；CentralChartWidget 接到信号弹窗。

**Tech Stack:** C++17, Qt 6.11 (Widgets/QTableView/QDialog), 无异步（纯同步计算，无 IO）。

设计文档：[2026-08-12-range-statistics-design.md](../specs/2026-08-12-range-statistics-design.md)

## Global Constraints

- 分层架构，UI 只能依赖 engine/intelligence/data/core/foundation，禁止反向；`range_statistics` 放 `engine/analyzer/`（纯 C++17，无 Qt 依赖）
- 编译零警告：`cmake --build --preset with-qt` 必须零错误零警告（Ninja 自动 re-configure，新增文件无需手动 cmake）
- 回归：ctest 现有 **395 tests** 全绿；新增 8 例 → 预计 **403**
- 快速 TDD 命令：`ctest --preset default -R RangeStatisticsTest --output-on-failure`（gtest_discover_tests 按测试名注册）
- 涨跌色 `#e54648`（红）/`#2e9e5b`（绿）/中性 `#d4d4d4`；选区高亮 `QColor(255,255,255,18)` + 边框 `#ffd700` 虚线
- 日期格式化用 `utils::toDateString(DateTime)`（"YYYY-MM-DD"）；`formatVolume`（股→手/万/亿）复用 kline_chart.cpp 匿名空间同款逻辑
- Bar 字段：`open/high/low/close/volume(股)/amount(元)/turnoverRate(0~1 比值)`；无效 bar = `!isValid()`（open/high/low/close 不全 >0）
- 安全异步约束对本科目不适用（无新异步）；修改时不得破坏现有画线/叠加/平移交互

---

### Task 1: Engine — RangeStats + computeRangeStats + 单测

**Files:**
- Create: `src/engine/analyzer/range_statistics.h`
- Create: `src/engine/analyzer/range_statistics.cpp`
- Test: `tests/test_engine/test_range_statistics.cpp`
- Modify: `src/CMakeLists.txt`（st_engine 加 `engine/analyzer/range_statistics.cpp`）
- Modify: `tests/CMakeLists.txt`（test_engine 加 `test_engine/test_range_statistics.cpp`）

**Interfaces:**
- Produces:
  - `struct RangeStats { DateTime fromDate; DateTime toDate; int barCount; double openClosePct; double high; DateTime highDate; double low; DateTime lowDate; double amplitude; double totalVolume; double totalAmount; double turnoverSum; double avgPrice; }`
  - `std::optional<RangeStats> computeRangeStats(const std::vector<Bar>& bars, int from, int to);`
- Consumes: `Bar`（`foundation/bar.h`）、`DateTime`（`foundation/types.h`）、`utils::toDateString/parseDate`（`foundation/utils/datetime.h`）。

- [ ] **Step 1: 写失败测试 `tests/test_engine/test_range_statistics.cpp`**

```cpp
#include "engine/analyzer/range_statistics.h"
#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"
#include <gtest/gtest.h>
#include <string_view>

namespace st {
namespace {

Bar makeBar(const char* date, double open, double high, double low, double close,
            double volume, double amount, double turnover) {
    Bar b;
    b.code = StockCode(std::string_view("SH600000"));
    b.time = utils::parseDate(date);
    b.period = BarPeriod::Daily;
    b.open = open; b.high = high; b.low = low; b.close = close;
    b.volume = static_cast<Volume>(volume);
    b.amount = amount;
    b.turnoverRate = turnover;
    return b;
}

TEST(RangeStatisticsTest, NormalRange) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10.0, 10.5, 9.8, 10.2, 100000, 1000000, 0.010),
        makeBar("2026-08-04", 10.2, 11.0, 10.1, 10.9, 120000, 1200000, 0.012),
        makeBar("2026-08-05", 10.9, 11.5, 10.8, 11.2, 140000, 1500000, 0.014),
        makeBar("2026-08-06", 11.2, 11.3, 10.6, 10.8,  90000,  950000, 0.009),
        makeBar("2026-08-07", 10.8, 10.9, 10.4, 10.5, 110000, 1100000, 0.011),
    };
    auto rs = computeRangeStats(bars, 0, 4);
    ASSERT_TRUE(rs.has_value());
    EXPECT_EQ(utils::toDateString(rs->fromDate), "2026-08-03");
    EXPECT_EQ(utils::toDateString(rs->toDate), "2026-08-07");
    EXPECT_EQ(rs->barCount, 5);
    EXPECT_NEAR(rs->openClosePct, (10.5 - 10.0) / 10.0, 1e-9);   // 末close/首open-1
    EXPECT_NEAR(rs->high, 11.5, 1e-9);
    EXPECT_EQ(utils::toDateString(rs->highDate), "2026-08-05");
    EXPECT_NEAR(rs->low, 9.8, 1e-9);
    EXPECT_EQ(utils::toDateString(rs->lowDate), "2026-08-03");
    EXPECT_NEAR(rs->amplitude, (11.5 - 9.8) / 10.0, 1e-9);       // (高-低)/首open
    EXPECT_NEAR(rs->totalVolume, 560000.0, 1e-9);
    EXPECT_NEAR(rs->totalAmount, 5750000.0, 1e-9);
    EXPECT_NEAR(rs->turnoverSum, 0.056, 1e-9);
    EXPECT_NEAR(rs->avgPrice, 5750000.0 / 560000.0, 1e-9);
}

TEST(RangeStatisticsTest, SingleBar) {
    std::vector<Bar> bars{ makeBar("2026-08-03", 10.0, 11.0, 9.0, 10.5, 50000, 500000, 0.02) };
    auto rs = computeRangeStats(bars, 0, 0);
    ASSERT_TRUE(rs.has_value());
    EXPECT_EQ(rs->barCount, 1);
    EXPECT_NEAR(rs->openClosePct, (10.5 - 10.0) / 10.0, 1e-9);
    EXPECT_NEAR(rs->high, 11.0, 1e-9);
    EXPECT_NEAR(rs->low, 9.0, 1e-9);
    EXPECT_NEAR(rs->amplitude, (11.0 - 9.0) / 10.0, 1e-9);
    EXPECT_EQ(utils::toDateString(rs->highDate), "2026-08-03");
    EXPECT_EQ(utils::toDateString(rs->lowDate), "2026-08-03");
}

TEST(RangeStatisticsTest, ReversedRange) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10, 10.5, 9.8, 10.2, 1, 1, 0),
        makeBar("2026-08-04", 10, 11, 10, 10.9, 1, 1, 0),
    };
    EXPECT_FALSE(computeRangeStats(bars, 1, 0).has_value());
}

TEST(RangeStatisticsTest, OutOfBounds) {
    std::vector<Bar> bars{ makeBar("2026-08-03", 10, 10.5, 9.8, 10.2, 1, 1, 0) };
    EXPECT_FALSE(computeRangeStats(bars, 0, 5).has_value());
    EXPECT_FALSE(computeRangeStats(bars, -1, 0).has_value());
}

TEST(RangeStatisticsTest, EmptyBars) {
    EXPECT_FALSE(computeRangeStats({}, 0, 0).has_value());
}

TEST(RangeStatisticsTest, SkipsInvalidBars) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10.0, 10.5, 9.8, 10.2, 100, 1000, 0.01),
        Bar{},  // 全 0 → !isValid()，不参与极值/基准/量额/换手，但计入 barCount
        makeBar("2026-08-05", 10.9, 11.5, 10.8, 11.2, 140, 1500, 0.02),
    };
    auto rs = computeRangeStats(bars, 0, 2);
    ASSERT_TRUE(rs.has_value());
    EXPECT_EQ(rs->barCount, 3);
    EXPECT_NEAR(rs->high, 11.5, 1e-9);
    EXPECT_NEAR(rs->low, 9.8, 1e-9);
    EXPECT_NEAR(rs->openClosePct, (11.2 - 10.0) / 10.0, 1e-9);   // 基准=首个有效 open
    EXPECT_NEAR(rs->totalVolume, 240.0, 1e-9);
    EXPECT_NEAR(rs->totalAmount, 2500.0, 1e-9);
    EXPECT_NEAR(rs->turnoverSum, 0.03, 1e-9);
}

TEST(RangeStatisticsTest, AllInvalidReturnsNullopt) {
    std::vector<Bar> bars{ Bar{}, Bar{} };
    EXPECT_FALSE(computeRangeStats(bars, 0, 1).has_value());
}

TEST(RangeStatisticsTest, AveragePriceAndZeroVolume) {
    std::vector<Bar> bars{
        makeBar("2026-08-03", 10, 10.5, 9.8, 10.2, 100000, 1000000, 0.01),
        makeBar("2026-08-04", 10.2, 11, 10.1, 10.9, 0, 0, 0.02),  // 有效但量额为 0
    };
    auto rs = computeRangeStats(bars, 0, 1);
    ASSERT_TRUE(rs.has_value());
    EXPECT_NEAR(rs->avgPrice, 1000000.0 / 100000.0, 1e-9);
    auto rs2 = computeRangeStats(
        std::vector<Bar>{ makeBar("2026-08-03", 10, 10, 10, 10, 0, 0, 0) }, 0, 0);
    ASSERT_TRUE(rs2.has_value());
    EXPECT_NEAR(rs2->avgPrice, 0.0, 1e-9);
}

}  // namespace
}  // namespace st
```

- [ ] **Step 2: 改 CMakeLists 并跑测试确认失败**

Modify `src/CMakeLists.txt` st_engine 块 `engine/analyzer/overlay_analysis.cpp` 后加一行：
```cmake
    engine/analyzer/range_statistics.cpp
```
Modify `tests/CMakeLists.txt` test_engine 块 `test_engine/test_overlay_analysis.cpp` 后加一行：
```cmake
        test_engine/test_range_statistics.cpp
```
Run: `cmake --build --preset with-qt 2>&1 | tail -5` 再 `ctest --preset default -R RangeStatisticsTest --output-on-failure`
Expected: 编译报错（`range_statistics.h` 不存在 / `computeRangeStats` 未声明）。

- [ ] **Step 3: 写实现 `range_statistics.h`**

```cpp
#pragma once

#include "foundation/bar.h"
#include "foundation/types.h"
#include <optional>
#include <vector>

namespace st {

/// K线区间统计 — 纯 C++17，无 Qt 依赖，可单测
struct RangeStats {
    DateTime fromDate;      // 区间首 bar 日期（原始）
    DateTime toDate;        // 区间末 bar 日期（原始）
    int barCount = 0;       // 区间 bar 总数（含无效 bar）
    double openClosePct = 0.0;  // 涨跌幅：lastValidClose / baseOpen - 1
    double high = 0.0;      // 区间最高价（有效 bar 极值）
    DateTime highDate;
    double low = 0.0;       // 区间最低价
    DateTime lowDate;
    double amplitude = 0.0; // 振幅：(high - low) / baseOpen
    double totalVolume = 0.0;  // 累计成交量（股）
    double totalAmount = 0.0;  // 累计成交额（元）
    double turnoverSum = 0.0;  // 区间换手率：各有效 bar turnoverRate 累加
    double avgPrice = 0.0;     // 均价：totalAmount / totalVolume（totalVolume<=0 → 0）
};

/// 计算闭区间 [from, to]（含端点）统计。
/// 空 bars / from<0 / to<0 / from>=size / to>=size / from>to → nullopt。
/// 区间内全为无效 bar → nullopt；无效 bar 跳过但计入 barCount；
/// baseOpen = 区间内首个有效 bar 的 open；涨跌幅取最后一个有效 bar 的 close。
std::optional<RangeStats> computeRangeStats(const std::vector<Bar>& bars, int from, int to);

} // namespace st
```

- [ ] **Step 4: 写实现 `range_statistics.cpp`**

```cpp
#include "engine/analyzer/range_statistics.h"

#include <algorithm>

namespace st {

std::optional<RangeStats> computeRangeStats(const std::vector<Bar>& bars, int from, int to) {
    if (bars.empty() || from < 0 || to < 0 ||
        from >= static_cast<int>(bars.size()) ||
        to >= static_cast<int>(bars.size()) || from > to) {
        return std::nullopt;
    }
    RangeStats rs;
    rs.fromDate = bars[static_cast<size_t>(from)].time;
    rs.toDate = bars[static_cast<size_t>(to)].time;
    rs.barCount = to - from + 1;

    double baseOpen = 0.0;
    double lastClose = 0.0;
    bool hasValid = false;
    double high = -1.0;    // 价格恒正，-1 哨兵
    double low = 1e300;    // 大数哨兵
    for (int i = from; i <= to; ++i) {
        const auto& b = bars[static_cast<size_t>(i)];
        if (!b.isValid()) continue;
        if (!hasValid) { baseOpen = b.open; hasValid = true; }
        if (b.high > high) { high = b.high; rs.highDate = b.time; }
        if (b.low < low)   { low  = b.low;  rs.lowDate  = b.time; }
        rs.totalVolume += static_cast<double>(b.volume);
        rs.totalAmount += b.amount;
        rs.turnoverSum += b.turnoverRate;
        lastClose = b.close;
    }
    if (!hasValid) return std::nullopt;   // 区间内全无效 bar → 无可统计
    rs.high = high;
    rs.low = low;
    rs.openClosePct = lastClose / baseOpen - 1.0;
    rs.amplitude = (high - low) / baseOpen;
    if (rs.totalVolume > 0) rs.avgPrice = rs.totalAmount / rs.totalVolume;
    return rs;
}

} // namespace st
```

- [ ] **Step 5: 构建 + 跑测试确认通过**

Run: `cmake --build --preset with-qt`（零警告）+ `ctest --preset default -R RangeStatisticsTest --output-on-failure`
Expected: 8 例全 PASS。

- [ ] **Step 6: Commit**

```bash
git add src/engine/analyzer/range_statistics.h src/engine/analyzer/range_statistics.cpp \
        tests/test_engine/test_range_statistics.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 区间统计引擎层 RangeStats + computeRangeStats（纯函数 + 8 例单测）"
```

---

### Task 2: KLineChart — DrawMode::Range + 拖拽选区 + 高亮 + rangeSelected 信号

**Files:**
- Modify: `src/ui/widgets/kline_chart.h`（DrawMode 枚举 / signals / 私有方法+成员）
- Modify: `src/ui/widgets/kline_chart.cpp`（控制条按钮 / setDrawMode / 鼠标事件 / paintEvent / drawRangeSelection / 清选区时机）

**Interfaces:**
- Consumes: Task 1 的 `computeRangeStats`（本任务**不直接调用**，仅透传 bars/from/to——弹窗在 Task 3 调用）
- Produces:
  - `KLineChart::signal void rangeSelected(const std::vector<Bar>& bars, int from, int to);`
  - `DrawMode` 加 `Range`；新增私有成员 `int rangeFrom_ = -1, rangeTo_ = -1, rangeDragStart_ = -1; bool rangeDragging_ = false;`

- [ ] **Step 1: 改 `kline_chart.h`**

```cpp
    enum class DrawMode { None, Horizontal, Trend, Range };   // 原 { None, Horizontal, Trend }
```
signals 区（`crosshairDateChanged` 后）加：
```cpp
    /// 区间统计：拖拽松开后发射（bars 为全量，弹窗内截取 [from,to]）
    void rangeSelected(const std::vector<Bar>& bars, int from, int to);
```
private 区 `void drawAnnotations(QPainter& p);` 附近加：
```cpp
    void drawRangeSelection(QPainter& p);
```
`lines_` 成员附近加：
```cpp
    int rangeFrom_ = -1, rangeTo_ = -1;      // 已选区间（-1 = 无）；弹窗关闭后保留高亮
    int rangeDragStart_ = -1;                // 拖拽起点（仅绘制用）
    bool rangeDragging_ = false;             // 正在拖拽选区
```

- [ ] **Step 2: 控制条加「区间统计」按钮**

`kline_chart.cpp` 构造，`addDrawToggle(tr("趋势线"), DrawMode::Trend);` 后加：
```cpp
    addDrawToggle(tr("区间统计"), DrawMode::Range);
```

- [ ] **Step 3: `setDrawMode` 退出时清选区**

`kline_chart.cpp` `setDrawMode` 现实现改为：
```cpp
void KLineChart::setDrawMode(DrawMode mode) {
    if (drawMode_ == DrawMode::Range && mode != DrawMode::Range) {
        rangeFrom_ = rangeTo_ = -1;   // 退出区间模式（切其他工具/取消选中）清选区
        rangeDragStart_ = -1;
        rangeDragging_ = false;
    }
    drawMode_ = mode;
    drawing_ = false;
    setCursor(mode != DrawMode::None ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}
```

- [ ] **Step 4: `clearAnnotations` 同时清区间**

现实现改为：
```cpp
void KLineChart::clearAnnotations() {
    lines_.clear();
    rangeFrom_ = rangeTo_ = -1;
    rangeDragStart_ = -1;
    rangeDragging_ = false;
    update();
}
```

- [ ] **Step 5: `loadStock`/`loadBars` 清选区**

两处 `lines_.clear();` 之后各加（loadStock 与 loadBars 各一处）：
```cpp
    rangeFrom_ = rangeTo_ = -1;
    rangeDragStart_ = -1;
    rangeDragging_ = false;
```

- [ ] **Step 6: `mousePressEvent` 加 Range 分支**

在现有 `if (drawMode_ == DrawMode::Horizontal) { ... }` 之前插：
```cpp
        if (drawMode_ == DrawMode::Range) {
            rangeDragStart_ = indexAtX(event->pos().x());
            rangeFrom_ = rangeTo_ = rangeDragStart_;
            rangeDragging_ = true;
            update();
            return;
        }
```

- [ ] **Step 7: `mouseMoveEvent` 加拖拽更新**

现实现 `if (dragging_ && !bars_.empty()) { ... }` 块之后（`if (!bars_.empty())` mouseIndex_ 块之前）插：
```cpp
    if (rangeDragging_ && drawMode_ == DrawMode::Range && !bars_.empty()) {
        rangeTo_ = indexAtX(event->pos().x());
    }
```

- [ ] **Step 8: `mouseReleaseEvent` 加 Range 提交**

在现有 `if (event->button() == Qt::LeftButton && drawing_ && drawMode_ == DrawMode::Trend)` 块之前插：
```cpp
    if (event->button() == Qt::LeftButton && drawMode_ == DrawMode::Range &&
        rangeDragging_) {
        rangeDragging_ = false;
        unsetCursor();
        const int cur = indexAtX(event->pos().x());
        rangeFrom_ = std::min(rangeDragStart_, cur);
        rangeTo_ = std::max(rangeDragStart_, cur);
        if (!bars_.empty()) emit rangeSelected(bars_, rangeFrom_, rangeTo_);
        update();
        return;
    }
```

- [ ] **Step 9: `paintEvent` 挂绘制 + 实现 `drawRangeSelection`**

`paintEvent` 中 `drawAnnotations(p);` 后加 `drawRangeSelection(p);`
在 `drawAnnotations` 实现之后新增：
```cpp
void KLineChart::drawRangeSelection(QPainter& p) {
    if (rangeFrom_ < 0 || rangeTo_ < 0 || bars_.empty()) return;
    const int from = std::clamp(rangeFrom_, 0, static_cast<int>(bars_.size()) - 1);
    const int to = std::clamp(rangeTo_, 0, static_cast<int>(bars_.size()) - 1);
    const double x1 = barCenterX(from) - bodyWidth() / 2;
    const double x2 = barCenterX(to) + bodyWidth() / 2;
    p.fillRect(QRectF(QPointF(x1, mainRect_.top()), QPointF(x2, mainRect_.bottom())),
               QColor(255, 255, 255, 18));   // 半透明高亮
    p.setPen(QPen(QColor("#ffd700"), 1, Qt::DashLine));
    p.drawLine(QPointF(x1, mainRect_.top()), QPointF(x1, mainRect_.bottom()));
    p.drawLine(QPointF(x2, mainRect_.top()), QPointF(x2, mainRect_.bottom()));
    p.setPen(QColor("#d4d4d4"));   // 首末日期
    p.drawText(QPointF(x1 + 2, mainRect_.top() + 12),
               QString::fromStdString(
                   utils::toDateString(bars_[static_cast<size_t>(from)].time)));
    p.drawText(QPointF(x2 - 90, mainRect_.top() + 12),
               QString::fromStdString(
                   utils::toDateString(bars_[static_cast<size_t>(to)].time)));
}
```
（`<algorithm>` 已 include；`utils::toDateString` 已在文件顶部 include `foundation/utils/datetime.h`。）

- [ ] **Step 10: 构建确认零警告**

Run: `cmake --build --preset with-qt` → 零错误零警告（无单测变更，ctest 保持 395 绿）。

- [ ] **Step 11: Commit**

```bash
git add src/ui/widgets/kline_chart.h src/ui/widgets/kline_chart.cpp
git commit -m "feat: K线区间统计交互——DrawMode::Range 拖拽选区 + 高亮绘制 + rangeSelected 信号"
```

---

### Task 3: RangeStatsDialog + CentralChartWidget 接线 + CMake

**Files:**
- Create: `src/ui/widgets/range_stats_dialog.h`
- Create: `src/ui/widgets/range_stats_dialog.cpp`
- Modify: `src/ui/widgets/central_chart_widget.h`（私有方法声明）
- Modify: `src/ui/widgets/central_chart_widget.cpp`（include + connect + 实现）
- Modify: `src/CMakeLists.txt`（st_ui 加 `ui/widgets/range_stats_dialog.cpp`）

**Interfaces:**
- Consumes: Task 1 `computeRangeStats`；Task 2 `KLineChart::rangeSelected`、`kline_->period()`、`currentName_`。
- Produces: `class RangeStatsDialog : public QDialog`（构造 `(const std::vector<Bar>&, int from, int to, const QString& title, QWidget* parent = nullptr)`）；`CentralChartWidget::openRangeStats`。

- [ ] **Step 1: 建 `range_stats_dialog.h`**

```cpp
#pragma once

#include "foundation/bar.h"
#include <QDialog>
#include <vector>

class QTableWidget;

namespace st {

/// K线区间统计弹窗 — 模态表格（指标/数值两列），红涨绿跌上色
class RangeStatsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RangeStatsDialog(const std::vector<Bar>& bars, int from, int to,
                              const QString& title, QWidget* parent = nullptr);
private:
    QTableWidget* table_ = nullptr;
};

} // namespace st
```

- [ ] **Step 2: 建 `range_stats_dialog.cpp`**

```cpp
#include "ui/widgets/range_stats_dialog.h"
#include "engine/analyzer/range_statistics.h"
#include "foundation/utils/datetime.h"
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace st {

namespace {
const QColor kUpColor("#e54648");
const QColor kDownColor("#2e9e5b");
const QColor kNeutralColor("#d4d4d4");

QString formatVolume(double volume) {   // 股 → 手/万/亿（对齐图表浮框）
    const double hands = volume / 100.0;
    if (hands >= 1e8) return QStringLiteral("%1亿").arg(hands / 1e8, 0, 'f', 2);
    if (hands >= 1e4) return QStringLiteral("%1万").arg(hands / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(hands, 0, 'f', 0);
}
QString formatAmount(double amount) {   // 元 → 万/亿
    if (amount >= 1e12) return QStringLiteral("%1万亿").arg(amount / 1e12, 0, 'f', 2);
    if (amount >= 1e8)  return QStringLiteral("%1亿").arg(amount / 1e8, 0, 'f', 2);
    if (amount >= 1e4)  return QStringLiteral("%1万").arg(amount / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(amount, 0, 'f', 0);
}
}  // namespace

RangeStatsDialog::RangeStatsDialog(const std::vector<Bar>& bars, int from, int to,
                                   const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("区间统计 — %1").arg(title));
    setMinimumSize(360, 420);
    auto* layout = new QVBoxLayout(this);

    const auto rs = computeRangeStats(bars, from, to);
    if (!rs) {
        auto* label = new QLabel(tr("无可统计数据"), this);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color:#888888;"));
        layout->addWidget(label);
        return;
    }

    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({tr("指标"), tr("数值")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);

    auto addRow = [this](const QString& name, const QString& value,
                         const QColor& color = kNeutralColor) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* ni = new QTableWidgetItem(name);
        auto* vi = new QTableWidgetItem(value);
        ni->setForeground(kNeutralColor);
        vi->setForeground(color);
        vi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, ni);
        table_->setItem(row, 1, vi);
    };
    const auto dstr = [](DateTime dt) {
        return QString::fromStdString(utils::toDateString(dt));
    };
    const QColor pctColor = rs->openClosePct >= 0 ? kUpColor : kDownColor;

    addRow(tr("日期范围"), tr("%1 ~ %2").arg(dstr(rs->fromDate), dstr(rs->toDate)));
    addRow(tr("区间天数"), tr("%1 根").arg(rs->barCount));
    addRow(tr("区间涨跌幅"), tr("%1%").arg(rs->openClosePct * 100.0, 0, 'f', 2), pctColor);
    addRow(tr("区间振幅"), tr("%1%").arg(rs->amplitude * 100.0, 0, 'f', 2), pctColor);
    addRow(tr("区间最高"), tr("%1  (%2)").arg(rs->high, 0, 'f', 2).arg(dstr(rs->highDate)));
    addRow(tr("区间最低"), tr("%1  (%2)").arg(rs->low, 0, 'f', 2).arg(dstr(rs->lowDate)));
    addRow(tr("累计成交量"), formatVolume(rs->totalVolume));
    addRow(tr("累计成交额"), formatAmount(rs->totalAmount));
    addRow(tr("区间换手率"), tr("%1%").arg(rs->turnoverSum * 100.0, 0, 'f', 2));
    addRow(tr("均价"), tr("%1").arg(rs->avgPrice, 0, 'f', 2));

    layout->addWidget(table_);
}

} // namespace st

#include "moc_range_stats_dialog.cpp"
```

- [ ] **Step 3: `central_chart_widget.h` 加私有方法**

private 区（`void refreshOverlayButton();` 附近）加：
```cpp
    /// 区间统计弹窗（K线 rangeSelected 信号转发）
    void openRangeStats(const std::vector<Bar>& bars, int from, int to);
```
（头文件已 include `foundation/bar.h`。）

- [ ] **Step 4: `central_chart_widget.cpp` include + connect + 实现**

文件顶部 include 区加：
```cpp
#include "ui/widgets/range_stats_dialog.h"
```
构造 `connect(kline_, &KLineChart::crosshairDateChanged, ...)` 块之后加：
```cpp
    // 区间统计：拖拽选区 → 弹窗
    connect(kline_, &KLineChart::rangeSelected, this,
            &CentralChartWidget::openRangeStats);
```
文件末尾（或 `reapplyTradeMarks` 之后）新增：
```cpp
void CentralChartWidget::openRangeStats(const std::vector<Bar>& bars, int from, int to) {
    if (bars.empty() || from < 0 || to >= static_cast<int>(bars.size())) return;
    QString periodText;
    switch (kline_->period()) {
        case BarPeriod::Daily:    periodText = tr("日线"); break;
        case BarPeriod::Weekly:   periodText = tr("周线"); break;
        case BarPeriod::Monthly:  periodText = tr("月线"); break;
        case BarPeriod::Minute5:  periodText = tr("5分"); break;
        case BarPeriod::Minute15: periodText = tr("15分"); break;
        case BarPeriod::Minute30: periodText = tr("30分"); break;
        case BarPeriod::Minute60: periodText = tr("60分"); break;
        default:                  periodText = tr("日线"); break;
    }
    RangeStatsDialog dlg(bars, from, to,
                         currentName_ + QStringLiteral("（") + periodText + QStringLiteral("）"),
                         this);
    dlg.exec();
}
```

- [ ] **Step 5: CMake + 构建 + 冒烟**

Modify `src/CMakeLists.txt` st_ui 块 `ui/widgets/overlay_dialog.cpp` 后加一行：
```cmake
    ui/widgets/range_stats_dialog.cpp
```
Run: `cmake --build --preset with-qt` → 零错误零警告；`ctest --preset default` → 403 全绿。
手动冒烟（用户执行）：图表拖拽选区 → 弹窗 10 行指标正确；单根 bar；拖拽换区间；弹窗关闭高亮保留；切股/切周期/清除标注/取消按钮清选区。

- [ ] **Step 6: Commit**

```bash
git add src/ui/widgets/range_stats_dialog.h src/ui/widgets/range_stats_dialog.cpp \
        src/ui/widgets/central_chart_widget.h src/ui/widgets/central_chart_widget.cpp \
        src/CMakeLists.txt
git commit -m "feat: 区间统计弹窗 RangeStatsDialog + 中央图表接线（多窗口自动生效）"
```

---

### Task 4: 文档收尾 + 终验

**Files:**
- Modify: `docs/DEVLOG.md`（顶部加 P10 第十六轮条目）
- Modify: `docs/changelog.md`（顶部加版本说明）
- Modify: `CLAUDE.md`（当前阶段 + 测试数 395 → 403）

- [ ] **Step 1: DEVLOG 顶部加条目**（仿 P10 第十五轮格式：需求/实施/验证/已知限制）

```markdown
## 2026-08-12 — P10 第十六轮：K线区间统计（全套指标 + 弹窗表格）

### 需求
补齐需求文档「工具 (P8)」区间统计功能项。用户选定 = K线图上拖拽选区间，弹窗展示全套统计（涨跌幅/最高最低/振幅/天数/量额/换手/均价），选区高亮保留、多窗口自动生效。

### 实施
- `engine/analyzer/range_statistics.{h,cpp}`（NEW）：RangeStats + computeRangeStats —— 纯 C++17 无 Qt 依赖，闭区间 [from,to] 统计，无效 bar 跳过、基准=首个有效 open、量额比均价、除零守卫，全无效区间返回 nullopt
- `ui/widgets/kline_chart.{h,cpp}`：DrawMode 加 Range（控制条「区间统计」按钮同组互斥）——按下记起点、拖拽选区高亮（半透明块+两端虚线+首末日期）、松开发 `rangeSelected(bars,from,to)`；弹窗关闭高亮保留；切股/切周期/清除标注/退出模式清选区；坐标锚定 bar 索引随平移缩放稳定
- `ui/widgets/range_stats_dialog.{h,cpp}`（NEW）：RangeStatsDialog 模态表格（指标/数值两列 10 行，涨跌幅/振幅红涨绿跌，量额 手/万/亿 格式化）
- `ui/widgets/central_chart_widget.{h,cpp}`：接 rangeSelected → 弹窗（标题「股票名（周期）」）；独立图表窗口共用 CentralChartWidget 自动生效
- 测试：RangeStatisticsTest 8 例（test_engine）——正常区间全字段/单根/降序/越界/空/无效跳过/全无效 nullopt/均价除零

### 验证
- 构建零警告；总计 395 → **403** 全绿
- GUI 冒烟由用户手动执行（选区→弹窗、单根、换区间、高亮保留、清除时机、多窗口）
```

- [ ] **Step 2: changelog 顶部加条目**（仿第十五轮格式，简述功能 + 测试数）
- [ ] **Step 3: CLAUDE.md**「当前阶段」行追加 `→ P10 第十六轮 ✅（区间统计：K线拖拽选区 + 全套指标弹窗 + 选区高亮保留）`；「总计: ✅ 395 tests」改 `✅ 403 tests`
- [ ] **Step 4: 终验**

Run: `cmake --build --preset with-qt`（零错误零警告）+ `ctest --preset default`（全绿，403）+ 手动确认关窗不崩（用户）。

- [ ] **Step 5: Commit + push**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: P10 第十六轮区间统计 DEVLOG/changelog/CLAUDE.md"
```
push 由用户确认后执行（`git push`）。
