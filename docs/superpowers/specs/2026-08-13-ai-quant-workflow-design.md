# 设计：AI 量化工作流 4 轮路线（AI 信号 → 策略 → 回测 → 优化 → 建议）

日期：2026-08-13
状态：已确认（分解）→ 待按轮细化

## Context

P10 第十六轮（K线区间统计）已完成，原需求文档（行情看盘/工具/数据）**100% 落地**；实盘交易明确不做。用户选定下一阶段方向 = **AI/量化深化 + 策略开发体验**，合并为一条「**AI 信号 → 策略 → 回测 → 优化 → 建议**」的量化工作流，拆成 **4 个可独立交付的小回合**，每轮独立：引擎纯函数单测 → UI 接线 → 文档收尾 → 合并。

### 现状盘点（已核实）

**AI/量化（P9 Intelligence 层）— 已有组件，但各自独立、未成工作流：**
| 组件 | 位置 | 能力 |
|------|------|------|
| `PatternRecognizer` | `intelligence/pattern/` | 16 种 K线形态（十字星/锤头/吞没/三兵/金叉死叉/放量突破…），`PatternSignal{type,index,confidence,name,description}`，confidence 0~1 |
| `SentimentAnalyzer` | `intelligence/sentiment/` | 舆情情绪 `SentimentScore{score(-1~+1),label,summary}` + `eastmoney_news_provider` 资讯源 |
| `PatternFactor` | `intelligence/screener/` | 形态 → 选股因子 |
| `StrategyAdvisor` | `intelligence/advisor/` | 消费网格搜索结果 → 参数建议/置信度/过拟合与风险警告/中文解释 |
| `utils::indicators` | `foundation/utils/` | sma/ema/macd/rsi/boll 现成 |

**策略开发（UI）— 已有面板：**
| 面板 | 能力 |
|------|------|
| `StrategyPanel` | 内置策略模板库 + 参数编辑（应用回测） |
| `OptimizationPanel` | 网格搜索参数优化（QTableView 表格展示 GridSearchTableModel） |
| `StrategyComparePanel` | 多策略同数据同时回测 + 净值叠加 + 蒙特卡洛置信区间 |

**缺口**：AI 各面板互不相连，无「综合信号」锚点；参数优化只有表格无可视化；选股无 AI 因子整合配置；模板库缺向导式上手。

## 总体愿景

```
数据 → AI 信号(形态+情绪+技术因子 融合) → 策略模板 → 回测/对比 → 参数优化 → 优化建议
       ↑ 第1轮：综合信号面板                    ↑ 第4轮：模板+向导   ↑ 第2轮：热力图
       ↑ 第3轮：AI 选股工作流（AI因子整合）                           ↑（已有 Advisor）
```

AI 深化负责「信号与智能」，策略体验负责「开发与决策」，两半在工作流中汇合。

## 4 轮分解

### 第 1 轮 — AI 综合信号面板（AI 深化锚点）

**功能**：单只股票上融合 K线形态 + 舆情情绪 + 技术指标 → 综合信号评级（强烈买入/买入/观望/卖出/强烈卖出）+ 置信度 + 各分项明细 + 历史信号记录。

**引擎层**（新 `engine/analyzer/composite_signal.{h,cpp}`，纯 C++17 可单测）：
```cpp
struct SignalComponent {          // 分项
    std::string name;             // 中文名（"K线形态"/"舆情情绪"/"动量RSI"/...）
    double score = 0.0;           // -1 ~ +1
    double weight = 0.0;          // 该分项权重
    std::string detail;           // 分项说明（形态名/情绪摘要/RSI值）
};
enum class SignalRating : uint8_t { StrongBuy, Buy, Neutral, Sell, StrongSell };
struct CompositeSignal {
    SignalRating rating = SignalRating::Neutral;
    double score = 0.0;           // 加权综合 -1 ~ +1
    double confidence = 0.0;      // 0~1（分项一致度/覆盖度）
    std::vector<SignalComponent> components;
    std::string summary;          // 中文一句话结论
};
CompositeSignal composeSignal(
    const std::vector<pattern::PatternSignal>& patterns,  // 形态信号（当前窗口）
    const sentiment::SentimentScore& sentiment,            // 舆情情绪
    double rsi,                                            // 当前 RSI(14)
    const MacdResult& macd,                                // 当前 MACD（dif/dea/hist）
    double close, double prevClose);                       // 当前收盘 + 前收
```
- 分项打分规则：形态 bullish/bearish 映射 ±0.5~1（按 confidence 加权）；情绪 score 直接作为分项；RSI 超卖(<30)→正、超买(>70)→负；MACD 金叉/死叉 → 正/负。权重默认 形态 0.4 / 情绪 0.3 / 技术 0.3，可调。
- 综合 score = Σ(score×weight)/Σweight；评级按阈值切分（如 |score|>0.5 Strong、>0.2 普通、其余 Neutral）。
- confidence = 分项覆盖度×一致度（缺数据的分项按权重折减；各分项同向越一致越高）。

**UI 层**（新 `ui/panels/ai_signal_panel.{h,cpp}`）：Dock 面板，绑定主窗口中央图表（复用 `crosshairDateChanged`/`currentCodeChanged`）——显示综合评级大字、置信度、各分项条 + 说明、历史信号列表。异步拉取（PatternRecognizer 本地计算 + sentiment fetch + 指标）安全异步模式。

**测试**：`tests/test_engine/test_composite_signal.cpp` ~10 例（纯打分规则：多头形态+正面情绪+RSI 超卖 → StrongBuy；空头 + 负面 → StrongSell；数据缺失折减；权重调整；评级阈值边界）。

### 第 2 轮 — 参数优化可视化（策略体验）

**功能**：网格搜索结果从表格升级为**热力图**（2 参数网格）或**曲面/折线**（单参数），悬停显示参数组合与目标值。

**引擎层**（新 `engine/optimizer/grid_heatmap.{h,cpp}` 或并入 grid_search，纯函数可单测）：
```cpp
// 网格结果 → 热力图矩阵（x/y 轴参数名、行列值、目标值矩阵）
struct HeatmapMatrix {
    std::string xParam, yParam;
    std::vector<double> xValues, yValues;   // 行列参数值（升序、去重）
    std::vector<std::vector<double>> values; // values[y][x] = 目标值
};
std::optional<HeatmapMatrix> buildHeatmap(
    const std::vector<GridSearchResult>& results,
    const std::string& xParam, const std::string& yParam, Objective obj);
```
- 从 `results`（各组合含参数键值 + objectiveValue）映射到矩阵；缺格留 NaN。
- 单参数时退化为 1D 序列线。

**UI 层**：`optimization_panel` 加 QChart（QHeatmapSeries 或自绘）——表格与热力图 tab 切换，悬停 tooltip 显示参数组合 + 目标值，最佳组合高亮。参数对选择下拉（从网格中存在的参数选出前 2）。

**测试**：`tests/test_engine/test_grid_heatmap.cpp` ~6 例（矩阵构建/去重升序/缺格 NaN/单参数退化/空结果/无效参数名）。

### 第 3 轮 — AI 选股工作流（AI 深化）

**功能**：把 PatternFactor（形态）+ 情绪因子 + 现有多因子（估值/成长/动量/质量/情绪/波动）整合成**可配置的 AI 选股流程**，screener 面板可选 AI 因子并给出综合评分排序。

**引擎层**（扩展 `engine/screener/` 或新 `intelligence/screener/ai_screener.{h,cpp}`，纯函数可单测）：
```cpp
struct AiScreenerConfig {
    bool usePattern = true;      // 形态因子
    bool useSentiment = true;    // 情绪因子
    std::vector<std::string> factors;  // 现有因子 id 子集
    // 权重可配置
};
// 股票池 → 综合 AI 评分排序
std::vector<AiScore> runAiScreener(const AiScreenerConfig& cfg,
                                   const std::vector<StockContext>& stocks, ...);
```
- 复用现有 FactorLibrary 因子打分 + PatternFactor；情绪因子异步（每只股票 fetch 成本高 → 分批/限股票数）。
- 输出每只：综合分 + 分项分 + 排序。

**UI 层**：screener 面板加「AI 因子」配置区（勾选形态/情绪 + 因子权重），结果列加 AI 综合分，可按 AI 分排序。

**测试**：`tests/test_engine/test_ai_screener.cpp` ~8 例（纯评分/排序/权重/缺情绪数据降级）。

### 第 4 轮 — 策略模板增强 + 向导（策略体验）

**功能**：扩充 `StrategyPanel` 模板库（动量/均值回归/突破/RSI/双均线…）+ 交互式参数向导（点选模板 → 参数描述 → 一键应用回测）。

**引擎层**（扩展 `engine/strategy/` 内置策略，纯 C++ 可单测）：
- 新增模板策略（`MomentumStrategy`/`MeanReversionStrategy`/`BreakoutStrategy`/`RsiStrategy`/`DualMaStrategy`），每个带默认参数 + 参数说明（中文）。

**UI 层**：`StrategyPanel` 模板列表按策略类型分组；选中模板 → 右侧参数向导（参数名/说明/范围/默认值 + 建议值来自 Advisor 可选）；「应用回测」一键跳转。

**测试**：`tests/test_engine/` 每个新策略 3~5 例（信号正确性/参数边界）；baseline 增长 ~15。

## 跨轮共享设计

- **引擎纯函数一律进对应 analyzer/optimizer/screener/strategy 层**，无 Qt 依赖，可单测（贯穿项目分层原则）。
- **安全异步**：UI 拉取（情绪/选股）走 IO 池 + QPointer 守卫 + seq 去陈旧；本地指标计算可在 UI 线程（轻量）。
- 第 1 轮 `CompositeSignal` 作为后续轮的信号锚点；第 3 轮复用其分项逻辑。
- 每轮结束更新 `docs/DEVLOG.md` / `docs/changelog.md` / `CLAUDE.md`（测试数）。

## 测试与验证（基线 407）

| 轮 | 新增单测 | 预计总数 |
|----|---------|---------|
| 第 1 轮 AI 综合信号 | ~10（test_composite_signal） | ~417 |
| 第 2 轮 参数优化可视化 | ~6（test_grid_heatmap） | ~423 |
| 第 3 轮 AI 选股工作流 | ~8（test_ai_screener） | ~431 |
| 第 4 轮 策略模板 + 向导 | ~15（各策略） | ~446 |

每轮：构建零警告 + ctest 全绿 + 手动冒烟（用户）。

## 风险与对策

- **情绪数据源**：`ISentimentProvider` 曾有「暂无实现」注释——已核实 `eastmoney_news_provider` 存在；第 1/3 轮以其为源，失败时情绪分项降级为「无数据」并折减权重（不阻塞综合信号）
- **综合信号评级主观**：评分规则集中在引擎纯函数 + 单测固化，UI 只展示；阈值可调但默认值经单测锁定
- **热力图性能**：网格结果量通常 ≤ 数百组合，矩阵构建 O(n)，无性能风险；QChart 渲染正常量级
- **AI 选股情绪 fetch 成本**：每股票一次网络请求 → 限股票数（如 top 50）+ 分批 + 缓存；无情绪数据降级为纯因子分
- **第 3/4 轮依赖前序**：第 3 轮依赖第 1 轮分项逻辑、第 4 轮独立；均可在 main 上顺序开发（每轮独立 merge，无 worktree 长驻需求）
- **范围蔓延**：每轮功能固定如上，YAGNI——第 1 轮不做历史信号曲线图，第 2 轮不做 3D 曲面（v2 可加）

## 收尾

按轮执行：本轮设计文档确认后 → 写第 1 轮实施计划 → SDD 开发 → 用户冒烟 → 合并 push；随后逐轮推进。
