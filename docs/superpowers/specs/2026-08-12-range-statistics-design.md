# 设计：K线区间统计（全套指标 + 弹窗表格）

日期：2026-08-12
状态：已确认

## Context

P10 第十五轮（自选股列表 + 板块成分股下钻）已完成。用户选定下一轮 = **区间统计**——补齐需求文档「工具 (P8)」中**区间统计**功能项（与画线工具并列）。当前 K 线图已有十字光标 OHLC 浮框、滚轮缩放、左键拖拽平移、画线工具（水平线/趋势线），但**没有区间选择与统计**能力。

用户已确认的交互决策：
- **触发**：控制条加「区间统计」按钮（与水平线/趋势线同组互斥），按下后拖拽选区，松开即统计；再次拖拽换区间；按钮取消选中即清掉
- **指标**：全套——区间涨跌幅、最高/最低价+日期、振幅、天数、累计成交量/成交额、区间换手率、均价
- **展示**：**独立弹窗表格**（QDialog）；关闭后选区高亮保留

**技术现状**：
- `KLineChart` 已有 `DrawMode` 枚举（None/Horizontal/Trend）+ 画线工具组按钮（[kline_chart.cpp:129-145](src/ui/widgets/kline_chart.cpp#L129-L145)）+ `drawAnnotations`（[kline_chart.cpp:226](src/ui/widgets/kline_chart.cpp#L226)）+ `indexAtX`/`priceFromY` 坐标辅助
- 鼠标交互：`mousePressEvent`（[kline_chart.cpp:1347](src/ui/widgets/kline_chart.cpp#L1347)）按 drawMode 分派——水平线点击即画、趋势线按下记起点、默认拖拽平移；`mouseReleaseEvent` 提交终点
- `Bar` 字段：open/high/low/close/volume/amount/turnoverRate（0~1 比值），可算全套指标
- 分层：统计逻辑放 engine/analyzer（纯 C++17 可单测，仿 overlay_analysis 先例）；`CentralChartWidget` 为多窗口（主窗口 + 独立图表窗口）共用组件 → 区间统计对所有窗口免费生效

## 功能定义

1. **区间选择**：K线图控制条「区间统计」按钮（与画线工具同组互斥）→ 按下左键记起点、拖拽显示选区高亮（半透明色块 + 两端竖线 + 区间首末日期），松开计算闭区间 `[from, to]` 并保留高亮 + 发射信号
2. **区间统计弹窗**：独立 QDialog，两列表格「指标/数值」，全套指标红涨绿跌上色
3. **选区生命周期**：切股/切周期/清除标注/按钮取消选中 → 清选区高亮；弹窗关闭不影响选区（保留）
4. **范围**：仅 K 线图（日/周/月/分钟级周期均适用，锚定 bar 索引）；分时图不做（无 bar 索引概念，v2 可加）

## 关键设计决策

### 1. 引擎层：`src/engine/analyzer/range_statistics.{h,cpp}`（NEW）

```cpp
/// K线区间统计 — 纯 C++17，无 Qt 依赖，可单测（仿 overlay_analysis 先例）
struct RangeStats {
    DateTime fromDate;      // 区间首 bar 日期
    DateTime toDate;        // 区间末 bar 日期
    int      barCount = 0;  // 区间 bar 数
    double   openClosePct  = 0.0;  // 区间涨跌幅：close[to]/open[from] - 1
    double   high   = 0.0;  // 区间最高价
    DateTime highDate;      // 最高价所在 bar 日期
    double   low    = 0.0;  // 区间最低价
    DateTime lowDate;       // 最低价所在 bar 日期
    double   amplitude   = 0.0;  // 振幅：(high - low) / open[from]
    double   totalVolume = 0.0;  // 累计成交量（股；弹窗经 formatVolume 转 手/万/亿）
    double   totalAmount = 0.0;  // 累计成交额（元）
    double   turnoverSum = 0.0;  // 区间换手率：各 bar turnoverRate 累加（比值）
    double   avgPrice    = 0.0;  // 均价：totalAmount / totalVolume（量额比）
};

/// 计算闭区间 [from, to]（含端点）统计；空 bars / 越界 / from>to → nullopt
/// 规则：跳过 isValid()==false 的 bar 参与统计；high/low 取区间内有效 bar 极值
std::optional<RangeStats> computeRangeStats(const std::vector<Bar>& bars,
                                            int from, int to);
```

- 实现要点：
  - `openClosePct`/`amplitude` 均以**区间首 bar 的 open** 为基准（与通达信区间统计一致）
  - `high`/`low` 遍历区间取有效 bar 极值并记录日期；`barCount` = 区间 bar 总数（含无效 bar）
  - `turnoverSum` = Σ turnoverRate（各 bar 比值，可为 >1 的累计值，弹窗显示 `*100`%）
  - `avgPrice` = totalAmount/totalVolume；`totalVolume <= 0` 时 avgPrice 记 0
  - 纯 std 实现（`std::vector`/`std::optional`/`<chrono>` 日期仅透传，不格式化）

### 2. KLineChart 改：`DrawMode::Range` + 选区高亮 + 信号

**`kline_chart.h`**：
- `enum class DrawMode { None, Horizontal, Trend, Range };`（加 Range）
- public：`signal void rangeSelected(const std::vector<Bar>& bars, int from, int to);`（完整 bars 传出，弹窗内自行截取，避免拷贝截断）
- private：`void drawRangeSelection(QPainter& p);`
- 成员：`int rangeFrom_ = -1, rangeTo_ = -1;`（-1 = 无选区）；`int rangeDragStart_ = -1;`（拖拽中起点，仅绘制用）

**`kline_chart.cpp`**：
- 控制条画线工具组加 `addDrawToggle(tr("区间统计"), DrawMode::Range)`（同组互斥，复用现有 lambda）
- `setDrawMode(Range)`：进入时若已存在选区 → 保留（等待新拖拽覆盖）；退出（含切到其他模式/取消选中）→ `rangeFrom_ = rangeTo_ = -1`（清选区）
- `mousePressEvent` 加 Range 分支：`rangeDragStart_ = indexAtX(x); rangeFrom_ = rangeTo_ = rangeDragStart_; update(); return;`（不进入拖拽平移）
- `mouseMoveEvent`：Range 模式下拖拽更新 `rangeTo_ = indexAtX(x)`（clamp 到可见范围），高亮实时跟随
- `mouseReleaseEvent` 加 Range 分支：提交 `from = min(start, cur), to = max(start, cur)`；若 `from == to`（单根 bar）也统计（弹窗显示单日统计）；`rangeDragStart_ = -1;` 发射 `rangeSelected(bars_, from, to)`（入参拷贝整个 bars_，若 bars_ 空则不发）
- `paintEvent`：`drawAnnotations(p)` 后插 `drawRangeSelection(p)`
- `drawRangeSelection`：`rangeFrom_/rangeTo_` 均 ≥0 时——半透明色块（`QColor(255,255,255,18)`）覆盖 `[barCenterX(from)-bodyW/2, barCenterX(to)+bodyW/2]` 全高（mainRect 高），两端 1px 竖线（`#ffd700` 虚线），顶部绘制首末日期（`bars[from].time`/`bars[to].time` 格式化）；拖拽中实时更新
- 清选区时机：`loadStock`/`loadBars`（切股/重载）、`setPeriod`（经 loadStock）、`clearAnnotations()`（清除标注同时清区间）、`setDrawMode(None)`（取消选中）
- 坐标锚定 bar 索引 → 平移缩放自然跟随，无需重算

### 3. RangeStatsDialog（新 `src/ui/widgets/range_stats_dialog.{h,cpp}`）

```cpp
/// K线区间统计弹窗 — 模态表格（指标/数值两列），红涨绿跌上色
class RangeStatsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RangeStatsDialog(const std::vector<Bar>& bars, int from, int to,
                              const QString& title, QWidget* parent = nullptr);
};
```

- 构造：调用 `computeRangeStats(bars, from, to)`；nullopt → 显示「无可统计数据」灰色占位
- UI：QDialog 标题 `tr("区间统计 — %1").arg(title)`（title 由外部传「股票名（周期）」）；QTableWidget 两列「指标/数值」，行：
  1. 日期范围 `fromDate ~ toDate`
  2. 区间天数 `barCount` 根
  3. 区间涨跌幅 `openClosePct*100`% —— 红涨绿跌（`#e54648`/`#2e9e5b`/`#d4d4d4`）
  4. 区间振幅 `amplitude*100`% —— 同上色
  5. 区间最高 `high` + `（highDate）`
  6. 区间最低 `low` + `（lowDate）`
  7. 累计成交量 `formatVolume(totalVolume)`
  8. 累计成交额 `totalAmount` 亿/万格式化
  9. 区间换手率 `turnoverSum*100`%
  10. 均价 `avgPrice`
- 表格只读、选中整行、`resizeColumnsToContents` + 列宽拉伸；数值右对齐；关闭即销毁（无持久状态）

### 4. CentralChartWidget 接线

- 成员 `std::shared_ptr<EastMoneySectorProvider>` 已有；新增连接 `connect(kline_, &KLineChart::rangeSelected, this, [this](const auto& bars, int f, int t){ openRangeStats(bars, f, t); })`
- 私有 `void openRangeStats(const std::vector<Bar>& bars, int from, int to)`：`bars` 空或越界 → return；构造标题 `currentName() + "（" + periodLabel + "）"` → `RangeStatsDialog dlg(bars, from, to, title, this); dlg.exec();`（模态）
- 独立图表窗口（ChartWindow）共用 CentralChartWidget → 区间统计自动生效，无需额外接线
- 分时图不接（TimelineChart 无此交互）

### 5. 测试：`tests/test_engine/test_range_statistics.cpp`（NEW，~8 例）

`computeRangeStats` 纯函数单测（仿 test_overlay_analysis.cpp，Bar 用手工构造 + `utils::parseDate`）：
1. 正常区间：5 根 bar [0,4] → 涨跌幅/最高最低+日期/振幅/天数/量额/换手累加/均价 全字段断言
2. 单根 bar：from==to → barCount=1，涨跌幅=0，最高=最低=该 bar 高低
3. 降序输入（from>to）→ nullopt
4. 越界（to >= size / from < 0）→ nullopt
5. 空 bars → nullopt
6. 无效 bar 跳过：区间内某 bar open/high/low/close 为 0 → 不参与极值/涨跌幅基准
7. 换手率累加：两根 bar turnoverRate 0.01/0.02 → turnoverSum = 0.03
8. 均价计算：volume+amount 已知 → totalAmount/totalVolume；totalVolume=0 → avgPrice=0

## 文件改动

| 文件 | 改动 |
|------|------|
| `src/engine/analyzer/range_statistics.{h,cpp}` | NEW — RangeStats + computeRangeStats（纯 C++17） |
| `src/ui/widgets/kline_chart.{h,cpp}` | DrawMode::Range + 按钮 + 选区高亮 + rangeSelected 信号 + 清选区时机 |
| `src/ui/widgets/range_stats_dialog.{h,cpp}` | NEW — 区间统计弹窗 |
| `src/ui/widgets/central_chart_widget.{h,cpp}` | 接 rangeSelected → 弹窗 |
| `tests/test_engine/test_range_statistics.cpp` | NEW — 8 例 |
| `src/CMakeLists.txt` | st_engine + range_statistics.cpp；st_ui + range_stats_dialog.cpp；test_engine + test_range_statistics.cpp |
| `docs/DEVLOG.md` / `docs/changelog.md` / `CLAUDE.md` | 收尾 |

## 测试与验证

- 基线 395 → 预计 **403**（+8 computeRangeStats；纯函数无 UI 测试）
- 构建零警告 + ctest 全绿
- 冒烟：区间统计按钮选区→弹窗指标正确；单根 bar；拖拽换区间；弹窗关闭选区高亮保留；切股/切周期/清除标注/取消按钮清选区；独立图表窗口同样可用；关窗不崩

## 风险与对策

- **拖拽与平移冲突**：Range 模式独占左键（按下即选区，不触发拖拽平移），退出模式恢复平移——复用现有 drawMode 分派，无冲突
- **区间统计基准（首 bar open）**：若首 bar 无效（open=0）→ 跳到区间内首个有效 bar 作基准（实现时取首个 `isValid()` bar 的 open）；弹窗日期范围仍显示原始首末
- **`from == to` 单根统计**：openClosePct = close/open - 1 为当日涨跌，正常显示
- **bars_ 拷贝开销**：rangeSelected 传整个 bars_（最多 ~800 根/次），拷贝代价可忽略；弹窗内仅截取区间
- **分时图不做区间统计**：范围限定 K 线图；需求未强制分时，v2 可扩展
- **AVG 除零**：totalVolume<=0 → avgPrice=0（弹窗显示 0）
