# 开发日志 (Development Log)

## 2026-08-16 — P10 第三十三轮：模拟交易搜索选股 + 多股票同时模拟

### 需求（用户反馈）
模拟交易 tab 股票要能直接搜索；能够同时进行多个股票一起模拟交易。

### 实施
- **引擎多股票化**（`engine/paper_trade/paper_trade_engine.{h,cpp}`）：
  - `addStrategy(code, strategy)` 新增：每只股票绑定独立策略实例（避免共享实例状态跨股票串扰）；`addStrategy(strategy)` 保留（单股票兼容，未绑定策略在首次报价股票上驱动）
  - `pendingOrder_` 单值 → `std::map<code, Order>`（挂单按股票隔离，B 的报价不会成交 A 的挂单）
  - `lastPrice_` 单值 → `std::map<code, Price>`（buyByAmount 按各自股票最近报价换算股数）
  - `onQuote` 只驱动该股票绑定的策略 + 撮合该股票挂单
- **面板**（`ui/panels/paper_trade_panel.{h,cpp}`）：
  - `stockCombo_`（精选 130 只下拉单选）→ `StockPoolPicker`（全市场 5213 只搜索多选，与其余量化面板一致）
  - 启动：IO 逐只拉历史 → 主线程为每只股票建独立策略实例 + 播种
  - 轮询：`batchQuote(全部选中)` 逐只驱动引擎；日志追加全部新成交（不只最后一条）
  - 状态区新增「股票数」显示

### 验证
- 构建零警告；476/476 全绿（Debug + Release；+3 例：多股票独立策略/挂单按 code 路由/按 code 最近价换算）
- release GUI exe 因用户进程占用未重链（LNK1104），代码正确性由测试验证；用户关闭程序后重建即可
- 手动复测由用户执行：模拟交易 tab 股票池搜索多选（代码/名称/拼音），多选后启动同时模拟

## 2026-08-16 — 修复轮⑩：优化建议 tab 进度满但无结果（两阶段进度映射错乱）

### 背景（用户反馈）
优化建议 tab 进度条已满、预计时间 0s，但结果未出——是否要等压力测试？

### 根因
优化建议是**两阶段任务**：网格优化 → 压力测试（最优参数跑 2015 股灾/2016 熔断/2018 熊市/2020 疫情/2024 微盘 + 2015 至今全期基线）。原进度映射：网格 50~100%，压力测试 90~100%——**网格完成时进度条已到 100%、ETA 显示 0s**，压力测试阶段进度条**跳回 90%** 重新爬（全市场每窗口一次全市场回测，耗时与网格相当），且 stress 回调**不更新 ETA**（停在 0s）→ 用户看到「满了 + 0s + 无结果」误判卡死。

### 修复
- `advisor_panel.cpp`：网格阶段映射改为 **0~90%**，压力测试 **90~100%**（单调不倒退，阶段过渡可见）；stress 回调同步更新 ETA 标签（真实剩余时间）
- `stress_test.cpp`：修复窗口子进度 idx 时序 bug（原 `out.windows.size()+1` 在 push 前求值导致窗口1 与基线同 idx → 进度倒退）；改为调用方传入 idx（基线 0、窗口 1..N）；基线完成补上报 `1/total`

### 验证
- 构建零警告；473/473 全绿（Debug + Release）
- 手动复测由用户执行：优化建议全市场全选 → 进度条 0→90% 网格 → 90→100% 压力测试，ETA 全程更新，完成后出结果

## 2026-08-16 — P10 第三十二轮：全市场回测内存优化 + 进度修复（其他 tab 同参数优化）

### 需求（用户反馈）
参数优化 tab 全市场全选：78% 后预计时间一直增长、内存占用高；用户要求其他 tab（回测/策略对比/压力测试等）同样优化（测试都选全部股票）。

### 诊断
- **内存大头实测**（`tools_mem_bench`，全市场 5213 只 × 876 天 = 440 万 bar）：
  - 拉数据阶段峰值 830MB（DataCache 本身，Bar≈96B × 440 万 = 数据固有成本）
  - 回测阶段 1630MB：timeline 每组合拷贝 440 万 Bar（~420MB）+ equitySnapshots 900 份完整 Portfolio 快照（含持仓向量）+ 多 lane 并行翻倍
- **进度 78% 停滞**：GridSearchOptimizer 只统计第一个未完成组合的子进度，并行 lane 的其他组合进度不计入 → 进度停滞、ETA 虚高增长

### 实施
- **BacktestEngine**：
  - `BacktestConfig.keepEquitySnapshots`（默认 true）：false 时不存每日完整 Portfolio 快照，净值曲线由内部 `equityCurve_` 累积（新增 `Account::netValue()` 零拷贝取净值）——全项目无任何消费者读 equitySnapshots，UI 净值曲线用 performance.equityCurve
  - timeline 改存 `const Bar*`（指向 DataCache 的 shared_ptr<BarSeries>）：消除每组合 440 万 Bar 值拷贝（~420MB）
- **各调用方**：回测面板（makeConfig）、策略对比、压力测试 → `keepEquitySnapshots=false`；网格搜索默认 false
- **并行度**：优化/建议 Release 下 `parallelLanes` 上限 4（原 = 全部核数，16 lane × 全市场 = 内存 ×16）
- **进度**：`reportProgress` 改为**所有在跑组合子进度加权平均** + **单调保护**（compare_exchange，进度只前进不倒退，消除并行 lane 完成顺序导致的倒退/停滞）
- **预估提示**：优化/建议运行开始时显示「共 N 组合 × M 只（全市场大池预计较久）」

### 验证
- 构建零警告；473/473 全绿（Debug + Release，Release 需 clean-first 重建——类布局变更后增量构建残留导致 0xc0000409 假崩）
- mem_bench 实测：回测 76s / 峰值内存 1628MB（数据本身 830MB + 回测工作集；快照开关对比 1630 vs 1643MB 无差异 → 大头是数据，已无可省）
- GUI 冒烟通过；手动复测由用户执行（全市场优化看进度条持续动 + 组合数预估提示）

### 已知限制
- 全市场 × 网格搜索绝对耗时仍长（每组合 ~76s × 90 组合 ≈ 1.9 小时，Release 4 lane）：这是计算量数学必然，进度条现在持续动 + 预估提示提前告知；如需更快建议缩小股票池或参数范围

## 2026-08-16 — 修复轮⑨：量化工作台打开崩溃（0xC0000374 堆损坏）

### 背景（用户反馈）
点「量化工作台」菜单直接崩溃（release-qt 版，Windows 事件日志 0xC0000374 堆损坏 ×3 + 0xC0000005）。

### 排查过程
- `tools_quant_repro`（构造 QuantWindow 循环）复现：**连接未就绪 + 无主线程延迟 = 稳定崩溃**；等连接就绪 / 加任何主线程打点延迟 = 不崩（竞态窗口微秒级）
- 排除：20 并发 getStockList 压力测试不崩、10 面板手动构造（无 QuantWindow）不崩、Debug 不崩（时序不同）
- **根因**：`TdxProvider::ensureConnected` 在 `state_ != Connecting`（Disconnected/Failed）时**每个 executeCommand 都 spawn 一个 detached doConnect 线程**。量化工作台打开 = 8+ 组件（6 StockPoolPicker + 2 StockSearchBar）同时 submitIO → 连接失败/慢时**线程风暴**：多个 doConnect 并发执行 `transport_ = std::move(transport)` 竞争 → use-after-free / 双重释放 → 堆损坏。`lastConnectAttempt_` 字段（冷却预留）声明了但从未使用
- 主程序场景：启动后立即点量化工作台（连接建立中）+ 全市场列表首次拉取慢 → 触发

### 修复
- `tdx_provider.cpp`：`ensureConnected` 连接失败路径加 **2s 冷却**（lastConnectAttempt_ 生效）——冷却期内并发 executeCommand 只等待现有连接尝试，不再风暴式 spawn；Connecting 态也统一走等待
- `stock_pool_picker.cpp` / `stock_search_bar.cpp`：IO 加载任务**先等 provider 连接就绪**（≤15s 轮询）再拉列表——消除连接建立期与 doConnect 的并发竞争窗口
- 保留 `tools_quant_repro` / `tools_concurrent_list` 复现工具

### 验证
- 原始崩溃配置（不等待连接 + 无打点 + 循环构造 30 次）× 20 轮全部通过
- GUI 实测：release 版启动后 2s 内 Alt+Q 打开量化工作台 × 5 全部存活
- 构建零警告；473/473 全绿（Debug + Release 双配置）
- 手动复测由用户执行

## 2026-08-15 — P10 第三十一轮：Release 构建 + 进度细化（全市场 tab 卡住修复）

### 需求（用户反馈 + 确认）
全市场全选后：参数优化进度条 78% 停滞，其余 tab 各自到某进度后不动。诊断：**不是卡死，是计算量爆炸 + 进度粒度太粗**。用户确认方案三（Release 构建）+ 方案二（进度细化 + 剩余时间）。

### 实施
- **Release 构建（方案三）**：
  - `CMakePresets.json` 新增 `release-qt` preset（MSVC Release + Qt，binaryDir `build/release-qt`，复用 `build/vcpkg_installed` 已装库避免重新下载；显式 `CMAKE_CXX_COMPILER=cl` 防 PATH 里 mingw64 cmake 误选 GCC）
  - 新增 `run-release.bat`（Qt bin PATH + 启动 release exe）
  - 实测基准 `tools_opt_bench.cpp`（300 只 × 12 组合 MACross）：**Debug 454.8s vs Release 54.2s，快 8.4 倍**，最优目标值一致 43.67
- **进度细化（方案二）**：
  - `grid_search.cpp`：evaluateOne 接入 BacktestEngine 子进度回调（按回测日期推进，全市场 ~900 点/组合），`comboSub[]` + mutex 汇总到全局进度——组合内进度持续动，不再每组合静默几分钟
  - 修复：worker 中 `results[i] = evaluateOne(...)` 赋值丢失（release 测试 GridSearchTest.SortsByObjectiveDescending 暴露，Debug 未捕获）
  - 新增共享 `ui/utils/progress_eta.h`（ProgressEta：已用/剩余时间格式化）
  - 5 面板（优化/建议/回测/对比/压力）：IO 阶段节流上报（每 2% 或末只）+ 进度条旁「已用 X · 预计剩余 Y」实时标签，运行开始 reset、结束隐藏

### 验证
- Debug 构建零警告 + 473/473 全绿；release-qt 构建零警告 + ctest 473/473 全绿（含 GridSearch 修复验证）
- release-qt GUI 冒烟 10s 存活（需 Qt bin 在 PATH，run-release.bat 已处理）
- 实测性能对比见上（8.4 倍）；用户复测：日常用 `run-release.bat` 启动，全市场优化/回测进度条持续动 + 时间估算

### 已知限制
- 全市场参数优化仍是大计算量（Release 下 12 组合 300 只 = 54s，全市场 5213 只 ≈ 15~20 分钟/12 组合），进度虽动但耗时客观存在；如需更快可后续加方案①（优化限股票数）或 SQLite 持久化
- Debug 开发构建速度不变（保留 build/ 目录）；两版共存互不影响

## 2026-08-15 — P10 第三十轮：量化面板全市场股票池（替换内置精选 130 只）

### 需求（用户反馈）
参数优化/优化建议/选股/回测/策略对比/压力测试/模拟交易要使用全市场股票，而不是内置小股票池。用户确认范围：6 面板（优化/建议/选股/回测/对比/压力）+ 模拟交易；模拟交易要求搜索选取多支（引擎单股票，拆下一轮做引擎改造）。未选 SQLite 持久化——全市场首次拉 K 线仍慢（10~30 分钟），已提醒，视体验再定。

### 实施
- **共享控件** `ui/widgets/stock_pool_picker.{h,cpp}`（新）：
  - 异步（IO 池 + QPointer 守卫）拉 `getStockList(SH/SZ)` → 过滤可交易 A 股（TDX 实连实测：5213 只，SH 2315 + SZ 2898，首次约 4s，provider 缓存后更快）
  - 搜索框过滤：代码/名称/拼音首字母（`setHidden` 过滤，勾选状态保留）；「全选/清空」作用于当前过滤结果；状态行「共 N 只 · 已选 M」
  - 加载完成默认全选（用户意图全市场运行）；批量插入先 `setUpdatesEnabled(false)` + 先设 checkState 再 addItem（避免 5k 次 itemChanged 信号风暴）
- **6 面板替换**（优化/建议/选股/回测/对比/压力）：`QListWidget* stockList_` → `StockPoolPicker* stockPicker_`，`selectedSymbols()` 委托给 picker；结果表/成交表名称映射从精选静态表 → picker 全市场列表；压力测试首次获得股票池选择 UI（原先固定全部精选）
- 选股「输出前N」上限 129 → 1000（全市场规模）；股票池/策略预设最小宽 280 保留
- 探针工具 `tools_stock_list_probe.cpp`（TDX 全市场列表耗时/数量实测，保留）

### 验证
- 构建零警告；473/473 全绿（纯 UI 装配，无新增单测）；GUI 冒烟 10s 存活
- 探针实测：TDX 全量 SH 2315 只 / SZ 2898 只可交易，列表拉取 2s + 1.7s
- 手动复测由用户执行：6 面板股票池应显示「加载中…」→ 全市场 5213 只全选，搜索过滤、全选/清空、计数正常；跑选股/回测确认全市场范围生效

### 已知限制
- 无 SQLite 持久化：全市场选股/回测每次联网拉 K 线（预计 10~30 分钟），进度条可见；建议后续轮接入 DataRepository（已有实现未接线）
- 模拟交易仍为内置精选下拉单选——下一轮：PaperTradeEngine 多股票改造（pendingOrder 按 code、策略 per-code 实例化防串状态）+ 面板换 StockPoolPicker 搜索多选

## 2026-08-15 — P10 第二十九轮：因子库扩充（11 → 24，估值类首落地）

### 需求（用户选择）
因子库扩充（不做自定义快捷键）。用户选定范围：纯技术因子 + 估值因子（成长类因无财报数据源本轮不做）。

### 实施
- **引擎（`engine/screener/factor_library.{h,cpp}`）**：
  - 新增 `ST_DECLARE_SCORED_FACTOR` 宏（支持自定义 toScore 映射）
  - 新增 13 个因子：
    - 动量（+4）：`cci_14` CCI(14)（(v+100)/2 映射）/ `williams_r` 威廉%R(14)（100+v，超买高分）/ `bias_6` 乖离率（50+v，双向）/ `up_streak` 连涨天数（×20 封顶 100）
    - 波动（+2）：`boll_pos` 布林带位置 0~1 / `amplitude_20` 20日均振幅%
    - 质量（+3）：`ma_cross` MA5/MA20 金叉态 / `price_pos_52w` 52周价格位置 / `ma20_slope` MA20 斜率（50+v×100）
    - 量价（+2）：`mfi_14` MFI(14) / `vol_price` 量价配合（近5日涨放量/跌缩量占比）
    - 估值（+2）：`pe_ttm` 市盈率TTM（100-v，低估高分）/ `market_cap` 总市值（115-log10×5，小市值偏好）
- **估值因子数据链路**：
  - `screener_types.h`：`FactorContext` 增加 `const QuoteFundamentals* quote`（Data 层结构，Engine 依赖 Data 合法）
  - `stock_screener.{h,cpp}`：`setQuoteFundamentals(map)` 注入 fullCode→快照，evaluate 时填入 ctx
  - `idata_provider.h`：新增 `batchQuoteFundamentals` 虚方法（默认逐个调单只接口）；腾讯/AKShare 覆盖为已有批量实现（标 override）；`MultiProvider` 转发：主源整体批量优先，全无效回退备源（不拼接防口径错配）
- **UI（`ui/panels/screener_panel.{h,cpp}`）**：IO 阶段批量拉基本面快照 → worker 注入 screener；因子勾选区自动 24 项（中文名映射补齐 13 个新因子）；估值因子无快照时降级缺失 → 中性 50 分，不阻塞选股

### 验证
- 构建零警告（96/96）
- ctest 473/473 全绿（新增：test_factors +15 例含各新因子行为/估值映射/缺失降级；test_screener +2 例估值注入排序/无注入降级）
- GUI 冒烟由用户复测：量化工作台→选股面板应见 24 个因子勾选（估值类需联网拉快照）

### 已知限制
- 成长类因子（ROE/营收增速等）仍缺数据源：`IFundamentalProvider` 仅接口无实现、TDX 财务协议(0x0A04)未解析——留待后续轮
- `bias_6`/`cci_14`/`williams_r` 的 toScore 为简单线性映射，阈值未做统计校准（可后续用真实数据调参）

## 2026-08-15 — P10 第二十八轮：量化工作台提升为菜单栏顶层项

### 需求（用户反馈）
量化工作台直接放菜单栏，不再放在「量化」子菜单中。

### 实施
- `ui/main_window.cpp`：删除「量化(&Q)」下拉菜单，「量化工作台(&Q)」改为菜单栏顶层动作（与「市场」「资金数据」并列，直接点击即开，行为不变 openQuantWindow）

### 验证
- 构建零警告；456/456 全绿（纯 UI 装配）；GUI 冒烟 8s 存活
- 手动复测由用户执行（菜单栏直接点击打开量化工作台）

## 2026-08-15 — 修复轮⑧：表单行内控件拉伸分配（离屏渲染几何验证）

### 背景（用户复测反馈）
行式布局后用户仍觉「跟修改前没有区别」。**离屏渲染几何 dump 定位真正元凶**：QHBoxLayout 行内 QLabel（如「从/到/步长」）与 QSpinBox 均为可拉伸策略，被**均分拉伸**——「从」label 被拉宽到 104px（文本靠左、右侧 76px 空白），视觉上「标签与选项间隔远」；此前 Preferred 策略无效（Preferred 仍参与拉伸）。

### 修复
- 行内控件显式 **stretch 分配**：spin/日期框 `addWidget(控件, 1)`（独占剩余空间，宽度不收缩甚至更宽），行内 QLabel 不加 stretch（保持文本宽 12px，紧贴相邻控件）
- 覆盖：4 面板 makeRangeRow（从/到/步长）+ 日期行（startDate/endDate）
- 新增 `tools_form_render`（离屏渲染 + 几何 dump 验证工具，保留）

### 验证（几何实测）
- 「快线周期」右缘 59 → 「从」x=65（6px）；「从」右缘 77 → spin x=83（6px）；日期框与「日期」标签 6px
- spin 宽 191px / 日期框 328px（均宽于此前，不收缩）
- 构建零警告；456/456 全绿；GUI 冒烟 8s 存活
- 手动复测由用户执行

## 2026-08-15 — 修复轮⑦：表单改「标签紧贴行式」布局（间距 6px）

### 背景（用户复测反馈）
移除 FieldsStayAtSizeHint 后控件宽度恢复，但「标签与选项的间隔又变很远了」。根因：QFormLayout 的**标签列**结构使间距 ≥ 标签列宽（最宽标签决定），无法缩小。

### 修复
- 4 面板（优化/建议/回测/对比）配置表单：所有 `addRow(label, field)` 两参数行改为**单行式**——`QHBoxLayout(label + field stretch 1)` 整行（QFormLayout::addRow(QLayout*) 无标签列）
- 效果：标签与控件间距 = 行内 spacing 6px（紧贴）；控件 stretch 1 占满剩余宽度（宽度与之前拉伸一致，不收缩）
- 股票池/策略预设列表、日期行、参数行（从/到/步长）全部行式化

### 验证
- 构建零警告；456/456 全绿（纯 UI 布局，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（4 面板标签紧贴控件、控件宽度正常）

## 2026-08-15 — 修复轮⑥：表单控件宽度恢复（不收缩）

### 背景（用户复测反馈）
紧凑化后「选项收缩了」，要求按之前宽度。

### 修复
- 移除 4 面板 `setFieldGrowthPolicy(FieldsStayAtSizeHint)`（该策略让 QComboBox/QDateEdit 等收缩到 sizeHint）→ 恢复默认拉伸宽度；移除股票池/策略预设 `setMinimumWidth(280)`
- 保留 `setLabelAlignment(Qt::AlignLeft)` + 水平间距 10 / 垂直间距 6（标签间距紧凑诉求仍满足）

### 验证
- 构建零警告；456/456 全绿；GUI 冒烟 8s 存活
- 手动复测由用户执行（控件宽度与之前一致、标签间距紧凑）

## 2026-08-15 — P10 第二十七轮：优化/建议/回测/对比面板表单紧凑化

### 需求（用户反馈）
参数优化（快慢周期、日期）、优化建议（均线周期、超跌阈值、日期）、回测（日期）、策略对比（日期）等**选择栏与标签隔得太远**。

### 实施
- 4 面板 QFormLayout 紧凑设置：`setLabelAlignment(Qt::AlignLeft)`（标签左对齐）+ `setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint)`（字段保持自身宽度，不再被拉伸）+ 水平间距 10 / 垂直间距 6
- 副作用防护：股票池/策略预设 QListWidget `setMinimumWidth(280)`（FieldsStayAtSizeHint 下不拉伸，保最小宽度可读）

### 验证
- 构建零警告；456/456 全绿（纯 UI 布局，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（4 面板标签与控件紧凑相邻、股票池宽度正常）

## 2026-08-15 — P10 第二十六轮：压力测试数据红绿着色

### 需求（用户反馈）
压力测试 tab 的相关数据标红/绿色标明。

### 实施
- `ui/panels/stress_test_panel.cpp`：着色辅助 `colorPnl`（≥0 红 `#e54648` / <0 绿 `#2e9e5b`）+ `colorMdd`（回撤 >20% 警示红 / ≤20% 可控绿，阈值与 Advisor riskWarning 一致）
- 覆盖数据：
  - 当前窗口绩效：总收益/年化/夏普（正红负绿）、最大回撤（阈值警示，替代原固定红）、胜率（50% 分界）、盈亏比（1.0 分界）、期末净值（1.0 本金分界）
  - 基线对比：基线总收益/收益差（正红负绿）、基线最大回撤（阈值警示）；基线失败态红字「失败」
- 切换压力场景窗口（onWindowChanged）重新着色

### 验证
- 构建零警告；456/456 全绿（纯 UI 样式，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（跑压力测试 → 各指标红绿、切窗口着色刷新）

## 2026-08-15 — P10 第二十五轮：量化工作台表格列宽均分 + 居中对齐（全量，舆情除外）

### 需求（用户明确范围）
量化工作台**所有表格类列表**列宽均分 + 居中对齐；**舆情情绪除外**（新闻标题需显示全）。

### 实施
- 列宽均分：7 个 QTableView header `Stretch`（形态识别/参数优化/优化建议/选股/回测成交/策略对比/模拟成交）
- 单元格居中：5 个模型 data() 加 `TextAlignmentRole → AlignCenter`（Pattern/GridSearch（优化+建议共用）/ScreenResult/Trade（回测+模拟共用）/Comparison）——共享模型一次覆盖多面板；移除 advisor 面板上一轮遗留的 CenterDelegate（机制统一）
- **舆情例外**：标题列 `Stretch` 弹性占满 + 左对齐（标题完整显示），评分/情绪列 `ResizeToContents` + item 居中
- 股票池/策略预设 QListWidget 保持左对齐（上轮确认项，未动）；搜索范围未动
- 注：构建日志 findstr 误报（`/showIncludes` 输出含 `system_error` 字样匹配 "error"），实际零警告（456 全绿佐证）

### 验证
- 构建零警告；456/456 全绿（纯 UI 样式，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（各 tab 表格均分居中、舆情标题完整、股票池左对齐不变）

## 2026-08-15 — P10 第二十四轮：优化建议 tab 网格结果表列宽均分 + 居中

### 需求（用户明确范围）
只对量化工作台**优化建议 tab 的网格结果表**做列宽均分 + 居中对齐（此前全局均分轮已回退，本轮按精确范围重做）。

### 实施
- `ui/panels/advisor_panel.cpp`：结果表 header `Stretch`（列宽均分）+ **view 级 CenterDelegate**（`initStyleOption` 覆写 `displayAlignment=AlignCenter`）——不动共享模型（GridSearchTableModel 同时被参数优化使用），不影响其他面板
- 注：构建日志 findstr 曾误报（/showIncludes 输出含 `system_error` 字样匹配 "error"），实际零警告

### 验证
- 构建零警告；456/456 全绿（纯 UI 样式，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（优化建议跑网格 → 结果表均分居中，参数优化 tab 不受影响）

## 2026-08-15 — P10 第二十一轮：策略模板共享目录（全策略面板自动同步）

### 需求（用户反馈）
策略 tab 的 6 个模板要**自动同步**到其他与策略有关的 tab（优化/优化建议/压力测试/模拟交易/策略对比）。

### 实施
- `ui/strategy_catalog.h`（NEW）：**策略模板共享目录（单一数据源）**——`StrategySpec`（id/类别/显示名/说明/参数名+键名/参数说明/默认值/范围）+ `all()`/`byId()`/`makeParams()`；新增策略只需在此登记 + 引擎注册，全部面板自动同步
- 5 个面板改造（去硬编码、统一走目录 + makeStrategy）：
  - `strategy_panel`：builtinTemplates 删除，直接引用 `catalog::all()`；apply 用 `makeParams`
  - `optimization_panel` / `advisor_panel`：下拉遍历目录；onStrategyChanged 按 spec 设置标签/范围（搜索范围=默认值~默认值+20 步长 2，防组合爆炸）；GridSearch 参数名用 spec 键名（原来硬编码 fastPeriod/entryPeriod）
  - `stress_test_panel`：下拉/标签/默认值按 spec；cfg.params 键名用 spec
  - `paper_trade_panel`：makeStrategy 统一走 `GridSearchOptimizer::makeStrategy`（消除本地 MACross/Turtle 分支）；下拉/标签/默认值按 spec
  - `strategy_compare_panel`：预设 6 → **10 个**（新增动量×2/突破/均值回归/RSI×2），StrategyComparator 本就走 makeStrategy 自动支持
- 受益面：BacktestPanel/GridSearch/StressTest/StrategyComparator 全部经 makeStrategy 统一构造

### 验证
- 构建零警告；456/456 全绿（纯 UI 装配重构，无引擎逻辑变更，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（5 面板下拉均见 6 策略、参数标签随策略切换、优化/建议跑新策略网格）

### 已知限制
- 策略对比面板为预设组合列表（非下拉），模板同步以「预设扩充」体现
- 优化/建议面板搜索范围统一「默认值~+20 步长 2」（可手动调整）

## 2026-08-15 — 修复轮④：策略向导参数说明常显 + 回测成交表名称列

### 背景（用户测试第 4 轮反馈）
1. 策略面板参数说明**悬停不显示**（tooltip 在 QSpinBox 子控件上触发不可靠）
2. 回测 tab 成交明细表**没有股票名称**列

### 实施
- `ui/panels/strategy_panel.{h,cpp}`：参数说明从悬停 tooltip 改为**常显灰色小字**（p1DescLabel_/p2DescLabel_ 置于各自 SpinBox 下方，wordwrap + 11px 灰字，模板切换即更新）——不再依赖悬停
- `ui/models/trade_table_model.{h,cpp}`：成交表 10 → **11 列**，代码后插「名称」列（`setNameByCode` 注入全码→名称映射，空映射显示 "--"；方向列 ForegroundRole 索引 2→3）
- `ui/panels/backtest_panel.cpp`：onResult 用精选池静态表构建名称映射注入成交表（与选股/自选面板同源）

### 验证
- 构建零警告；456/456 全绿（纯 UI 改动，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（策略面板参数说明常显、回测成交表名称列）

## 2026-08-15 — P10 第二十轮：策略模板增强 + 向导（AI 量化工作流 · 第 4 轮）

### 需求
按 [AI 量化工作流 4 轮路线设计](docs/superpowers/specs/2026-08-13-ai-quant-workflow-design.md) 第 4 轮（收尾轮）：策略模板库扩充 4 个新策略 + StrategyPanel 模板按类别分组 + 参数说明向导（悬停 tooltip）+ 「应用回测」一键跳转。

### 实施
- 引擎 4 个新策略（`engine/strategy/templates/`，纯 C++ 可单测）：
  - `momentum_strategy`：Momentum——N 日收益率 > 阈值（默认 5%）买入，收盘跌破 M 日均线离场（lookbackPeriod/exitPeriod 可配）
  - `breakout_strategy`：Breakout——**收盘价**突破 N 日最高收盘买入、跌破 M 日最低收盘离场（与海龟盘中高低价区分，收盘确认减少假突破）
  - `mean_reversion_strategy`：MeanReversion——收盘低于均线 X% 买入（超跌反弹）、回到均线上方离场（maPeriod/deviationPct）
  - `rsi_strategy`：Rsi——RSI 超卖买/超买卖（buyLevel/sellLevel，period 固定 14）
  - 共享 `strategy_helpers.h`（smaAt/hasPosition，不动 IStrategy/Turtle）；`GridSearchOptimizer::makeStrategy` 注册 4 个 id
- UI：
  - `strategy_panel`：Spec 加 category/p1Desc/p2Desc；列表「[类别] 名称」分组（趋势跟踪/动量/突破/均值回归/反转）；参数 spinbox + 标签悬停显示参数说明 tooltip；apply 参数映射扩展 6 个 id
  - `backtest_panel`：策略下拉扩 6 项（带类别前缀）；makeStrategy 6 分支；onStrategyChanged/updateParamLabels 按 id 设置标签与默认值；loadStrategy 接收新参数名（lookbackPeriod/maPeriod/deviationPct/buyLevel/sellLevel）
- 测试：StrategyTemplateTest **16 例**（test_engine，手动 ctx 驱动 + 下单记录）——每策略 买入触发/不触发/卖出/数据不足 4 例；修 2 个实现细节（阈值千分数 vs 百分数单位、爬升序列本身即突破的测试数据）

### 验证
- 构建零警告；Engine 174 → **190**，总计 440 → **456** 全绿；GUI 冒烟 8s 存活
- 手动冒烟由用户执行（策略面板 6 模板切换/参数说明悬停/应用回测、回测面板 6 策略跑通、优化面板 makeStrategy 兼容）

### 已知限制
- 每策略暴露 2 个主要参数（Rsi period 固定 14、Momentum 阈值固定 5%，其余参数固定默认）；优化/对比/压力/模拟面板的策略下拉仍为 2 项（v2 扩展）
- 向导 v1 无 Advisor 建议值（静态向导无回测上下文，v2 可加）

## 2026-08-15 — 修复轮③：形态识别结果表按最新时间倒序

### 需求（用户反馈）
量化工作台形态识别面板：信号列表把**最新时间的放在最上面**。

### 实施
- `ui/models/pattern_table_model.cpp`：`setRows` 内反转行序（`PatternRecognizer::detect` 输出按 bar 索引升序 → 反转后最新在前）——模型层一处改动，所有调用方自动生效

### 验证
- 构建零警告；440/440 全绿（纯 UI 模型改动，无新增单测）；GUI 冒烟 8s 存活
- 手动复测由用户执行（形态识别结果首行应为最近日期）

## 2026-08-15 — 修复轮②：AI 因子勾选未生效（全选与单选情绪 AI 分相同）

### 背景（用户复测反馈）
选股面板「全选两个 AI 因子」与「单选情绪」得到的 AI 分**一样**。

### 根因
`aiEnabled()` 只判断「是否任一勾选」；worker 里 `AiScreenerConfig.useSentiment` **硬编码 true**，且**形态勾选状态从未传入引擎**——`runAiScreener` 总是计算 形态+情绪+技术 全部分项 → 两种勾选组合结果相同。

### 修复
- `intelligence/screener/ai_screener.h`：`AiScreenerConfig` 增 `usePattern`（对齐设计文档 `{usePattern, useSentiment, factors}`）；字段序  weights/usePattern/useSentiment
- `ai_screener.cpp`：`usePattern=false` → 传入空 patterns（形态分项缺失、权重折减，技术指标恒参与）
- `screener_panel.cpp`：worker 捕获真实勾选状态 `usePattern/useSentiment` 并传入配置（移除硬编码 true）
- 测试：AiScreenerTest **+2**（PatternDisabled：usePattern=false → patternScore nullopt 且情绪保留；PatternToggleChangesScore：全选 vs 单选情绪的 compositeScore 必须不同、多头形态贡献正分 → 全选更高）；修正 SentimentDisabled 聚合初始化（字段序变化导致 false 误赋 usePattern）

### 验证
- 构建零警告；Intelligence 73 → **75**，总计 438 → **440** 全绿；GUI 冒烟 8s 存活
- 手动复测由用户执行（形态/情绪勾选组合 → AI 分应各不相同）

## 2026-08-15 — 修复轮：选股/回看日期区间根因 + 热力图轴标签 + 优化结果上下文

### 背景（用户复测反馈）
1. 参数优化：结果表看不出与哪支股票相关；热力图横轴数字标到表格上方、「慢线周期」标识显示不全
2. 选股：4 支股票全部「总分 50 / AI 分 0 / 因子 --」

### 根因
**选股全 -- 根因（P7 遗留 bug）**：`utils::addTradingDays(dt, n)` 对 `n <= 0` 直接 `return dt`（只支持向后）——选股面板 `addTradingDays(end, -250)` 得到 `start = end = 当天` → TDX `getBars(当天, 当天)` 严格区间返回空 → 不缓存 → 全部因子 nullopt（toScore 默认 50 分）、AI 分 0（ai 结果空）。腾讯源忽略 start/end 掩盖了此 bug，P9 换 TDX 严格区间后选股即坏，用户本轮测试才发现。

### 修复
- `foundation/utils/datetime.cpp`：`addTradingDays` 支持负天数——向前从 dt 前一日开始数 |n| 个非周末日（与向后对称）；`n==0` 返回原值；单测 +2（向后跨周末/回看 250 交易日与 tradingDaysBetween 自洽）
- `ui/widgets/grid_heatmap_widget.cpp`：x 轴刻度从「第一行格子下方」移到「最后一行格子下方」（底部留白区，不再压表格）；y 轴名改**竖排**（旋转 -90°，中文长名不再截断）
- `ui/panels/optimization_panel.{h,cpp}`：结果区新增上下文信息行——「股票池: 贵州茅台、平安银行 等 4 只 · 目标: 总收益 · 区间: 2023-01-01 ~ 2026-08-15」（运行优化时填充，明确结果对应池子/目标/区间）

### 验证
- 构建零警告；Foundation 56 → **58**（+2 日期测试），总计 436 → **438** 全绿；GUI 冒烟 8s 存活
- 手动复测由用户执行（选股出真实因子分/AI 分、热力图轴标签位置、结果信息行）

### 影响面排查（函数级修复，5 处负天数调用全部自动受益）
- `screener_panel` 选股回看起点、`stock_screener.evaluate` 过滤区间、`pattern_panel` 形态识别 120 日、`paper_trade_panel` 模拟交易播种历史 120 日、`stock_key_data_widget` 量比 5 日均量——修复前全部 start=当天（无数据/空历史），修复后正确回看；这也解释了形态识别/模拟交易此前「无反应」的现象

## 2026-08-15 — P10 第十九轮：AI 选股工作流（AI 量化工作流 · 第 3 轮）

### 需求
按 [AI 量化工作流 4 轮路线设计](docs/superpowers/specs/2026-08-13-ai-quant-workflow-design.md) 第 3 轮：选股面板增加「AI 因子」配置（形态/情绪勾选）→ 复用第 1 轮 `composeSignal` 分项逻辑对池内股票算 AI 综合分（0~100）→ 结果表新增「AI 分」列并按 AI 分排序；情绪数据限量拉取（池前 30 只，其余降级缺失）。

### 实施
- `intelligence/screener/ai_screener.{h,cpp}`（NEW）：`AiScreenerConfig`（权重可配，默认 形态0.4/情绪0.3/技术0.3）+ `AiScore`（综合分 + 三可选分项 + 一句话结论）+ `runAiScreener(pool, barsByCode, sentiments, cfg)` 纯函数——复用 `signal::composeSignal`（设计文档「第 3 轮复用其分项逻辑」：形态 detectAt(3) + RSI/MACD/动量 + 情绪）；三数组等长防御、bars 空跳过、情绪缺失降级；分项/综合 = (score+1)/2×100 映射；按综合分降序 stable_sort
- `ui/models/screen_result_model.{h,cpp}`：可选「AI 分」列——`setResults` 增 `aiScores` 参数（空=无列）；列布局 排名/代码/名称/总分/**AI 分**/因子明细；AI 分红涨绿跌着色（≥60 红/≥40 灰/<40 绿，与总分一致）
- `ui/panels/screener_panel.{h,cpp}`：选股配置加「AI 因子: [x]形态 [x]情绪」行（提示「情绪仅拉取池中前 30 只」）；IO 阶段拉 bars 时对池前 30 只同步 `fetchNews(code,10)` → `averageScore`（sentiments 与池对齐，其余 nullopt）；worker 跑完 StockScreener 后额外 `runAiScreener`（bars 从 cache 取回）→ AI 分按 code 对齐结果行 → 启用 AI 时按 AI 分降序重排（同分保持总分顺序）
- 测试：AiScreenerTest **8 例**（test_intelligence）——排序/分数映射范围/情绪禁用/情绪缺失降级/自定义权重主导/空池/空 bars 跳过/分项映射精确值

### 验证
- 构建零警告；Intelligence 65 → **73**，总计 428 → **436** 全绿；GUI 冒烟 8s 存活
- 手动冒烟由用户执行（选股面板 AI 因子勾选/取消、结果表 AI 分列与排序、情绪限量、无情绪降级）

### 已知限制
- v1 不把现有 11 因子融合进 AI 分（StockScreener 流程独立，总分列保留；AI 分 = 形态+情绪+技术）；「AI 分排序」= 结果表按 compositeScore 降序
- 情绪限量池前 30 只（按池子顺序，非按排名——排序前无法预知 topN）；全部取消 AI 勾选 → 行为与旧版完全一致
- 情绪权重未暴露 UI（v2 可加）

## 2026-08-15 — P10 第十八轮：参数优化热力图（AI 量化工作流 · 第 2 轮）

### 需求
按 [AI 量化工作流 4 轮路线设计](docs/superpowers/specs/2026-08-13-ai-quant-workflow-design.md) 第 2 轮：网格搜索结果从表格升级为**热力图**（2 参数网格，悬停显示参数组合与目标值，最优组合高亮，双击格应用参数）。注：用户曾要求移除热力图、后改为「先做出来看看效果」——本轮按恢复实现。

### 实施
- `engine/optimizer/grid_heatmap.{h,cpp}`（NEW）：`HeatmapMatrix`（x/y 轴参数名 + 升序去重行列值 + values[y][x] 目标值矩阵）+ `buildHeatmap(results, xParam, yParam)` 纯函数——空/同参/参数名不存在 → nullopt；缺格 NaN；同格 last wins
- `ui/widgets/grid_heatmap_widget.{h,cpp}`（NEW）：GridHeatmapWidget 自绘热力图——优红劣绿灰中渐变着色（objective 方向自适应：MaxDrawdown 越小越好自动反转）、缺格深色、最优组合白框高亮、格子够大显示数值、悬停 QToolTip（参数组合 + 目标值）、双击格子 cellActivated、右侧优→劣图例
- `ui/panels/optimization_panel.{h,cpp}`：结果区改 QTabWidget「结果表/热力图」双视图；onResult 构建矩阵喂热力图（单参数/空结果禁用 tab）；双击热力图格 → 应用该组参数到回测面板（与表格单击一致）；英文参数名透传（lastP1Param_/lastP2Param_）
- 测试：GridHeatmapTest **8 例**（test_engine）——3×3 矩阵/乱序去重/缺格 NaN/单列退化/重复格 last wins/同参拒绝/未知参数/空结果

### 验证
- 构建零警告（globalPos→globalPosition 修复 C4996）；Engine 166 → **174**，总计 420 → **428** 全绿；GUI 冒烟 8s 存活
- 手动冒烟由用户执行（优化运行 → 热力图 tab 出图、悬停浮框、最优格高亮、双击应用参数、单参数禁用）

### 已知限制
- 仅 2 参数网格可出图（GridSearchConfig 本就 1~2 参数）；单参数时热力图 tab 禁用（v2 可做 1D 折线）
- 热力图不随结果表联动选中（表选行不反映到热力图；v2 可加）
- 颜色映射按当前结果集 min/max 归一化（跨轮次不可比）

## 2026-08-14 — P10 第十七轮：AI 综合信号面板（AI 量化工作流 · 第 1 轮）

### 需求
按 [AI 量化工作流 4 轮路线设计](docs/superpowers/specs/2026-08-13-ai-quant-workflow-design.md) 第 1 轮：单只股票上融合 K线形态 + 舆情情绪 + 技术指标（RSI/MACD/动量）→ 综合信号评级（强烈买入/买入/观望/卖出/强烈卖出）+ 置信度 + 分项明细 + 历史信号记录，主窗口右侧 Dock，绑定中央图表开图路径自动跟随。

### 实施
- `intelligence/signal/composite_signal.{h,cpp}`（NEW）：`SignalRating/SignalComponent/CompositeSignal` + `composeSignal` 纯函数——**分层修正**：设计文档写 engine/analyzer，但入参含 `pattern::PatternSignal`/`sentiment::SentimentScore`（intelligence 类型）而 st_engine 不链接 st_intelligence → 放 `intelligence/signal/`（intelligence 依赖 engine 方向合法，避免循环依赖）
  - 打分规则：形态取 |direction×confidence| 最大者（多形态不叠加）；情绪=score；技术=RSI/MACD/动量三子分平均（RSI <30→+1 超卖 / >70→-1 超买 / 中部线性；MACD 金叉+hist>0→+0.5 / 死叉→-0.5；动量 ±3%=满分）
  - 权重默认 形态 0.4 / 情绪 0.3 / 技术 0.3（可覆盖）；缺失分项（无形态/无新闻/指标 NaN）按权重折减覆盖度；confidence = 覆盖度 × 一致度(1-极差/2)；评级阈值 |score|≥0.5 Strong、≥0.2 普通；中文摘要含驱动分项
- `ui/panels/ai_signal_panel.{h,cpp}`（NEW）：AiSignalPanel Dock——评级大字（强买红/买浅红/观望灰/卖浅绿/强卖绿）+ 得分/置信度/信号日期 + 一句话结论 + 分项自绘条（SignalBarWidget：-1~+1 横条 0 轴居中红正绿负 + 数值 + 说明）+ 历史信号表（本会话 50 条，双击开图）；安全异步：IO 拉日K(2015→now)+东财新闻（仅 A 股个股，指数跳过情绪）→ Worker 算指标/形态/综合信号 → QPointer 守卫 + gen 世代守卫
- MainWindow 装配：右侧与筹码分布 tabify（默认显示）+ 视图菜单 toggle；5 处开图路径接线（搜索/量化/资金/日志/openStockChart）+ 历史行双击开图
- 测试：CompositeSignalTest **13 例**（test_intelligence）——全多头 StrongBuy/全空头 StrongSell/混合 Neutral/情绪缺失折减/单分项/RSI 边界/MACD 状态/评级阈值边界/全缺失/动量贡献/自定义权重/摘要含评级名/ratingName 覆盖
- 环境备注：本次会话构建需 `danger-full-access`（沙箱阻止 ninja 管道捕获子进程输出导致挂起）+ vcvars64 环境

### 验证
- 构建零警告（修复 3 个编译问题：Qt 6.11 QStringLiteral 宏=`u"" str` 拼接**只接受字面量**，传变量需 QString::fromUtf8；头文件前向声明与 cpp 匿名命名空间类冲突；MSVC 嵌套 lambda init-capture 不能引用外层 lambda 捕获成员——局部变量中转）
- Intelligence 52 → **65**，总计 407 → **420** 全绿；GUI 冒烟 8s 存活
- 手动冒烟由用户执行（开股出评级/分项/历史、切股刷新、无新闻降级、历史双击开图、关面板不崩）

### 已知限制
- 情绪分项依赖东财资讯接口（失败/无新闻 → 缺失折减，不阻塞信号）；历史信号仅本会话内存（v2 可持久化 JSON）
- 指数支持信号（无情绪分项）；自定义指数（CIxxx）不触发面板（未接 loadCustomIndex 路径）
- 评级阈值/权重为默认值（引擎参数可调，UI 未暴露设置 v2）

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
- 构建零警告；Engine 158 → **166**（+8 RangeStatisticsTest），总计 → **407** 全绿
- GUI 冒烟由用户手动执行（选区→弹窗、单根、换区间、高亮保留、清除时机、多窗口）
（注：Foundation 55→56 / Data 109→112 为此前「基本面回退腾讯 + 自选名称」轮计数未同步所致，本轮仅 Engine +8 RangeStatisticsTest；总计 399→407）

## 2026-08-12 — P10 第十五轮：自选股列表 + 板块成分股下钻 + 市场收编视图菜单

### 需求
补齐需求文档「自选股」与「板块成分股下钻」功能项。用户选定本轮 = 自选股列表（主 Dock 持久化 watchlist.json + 行情表 + 图表按钮双向同步）+ 板块成分股弹窗（右键「查看成分股」→ 弹窗行情表 + 双击开图）+ 市场窗口收编视图菜单（默认隐藏 toggleViewAction + 自定义指数 Dock 下移）。

### 实施
- `foundation/watchlist_store.{h,cpp}`（NEW）：WatchlistStore —— JSON 持久化自选股列表到 configDir/watchlist.json（QStringList 按添加顺序，save/load/add/remove/contains/isEmpty/list/clear 操作，阻塞 IO 简化无回调错误），无 Qt Widget 依赖，纯 C++17 可单测
- `ui/models/watchlist_model.{h,cpp}`（NEW）：WatchlistModel —— QAbstractTableModel（3 列「代码/名称/涨跌幅%」，quoteMap_ cache 从 WatchlistPanel 刷新灌入，涨跌幅列红涨绿跌 / 颜色文字统一），`stockFromIndex(index)` 获取 StockCode
- `ui/panels/watchlist_panel.{h,cpp}`（NEW）：WatchlistPanel —— 左侧主 Dock（QDockWidget「自选股」+ QTableView + 右键菜单「移除自选」+ 双击开图 emitStockSelected）+ 10s 交互优先级定时刷新（yieldToInteractive 抢占 TDX 批量刷新 + seq 去陈旧）+ `refresh()` + `syncCurrentStock(code)`（当前图表代码变化触发 UI 按钮态同步）
- `ui/widgets/central_chart_widget.{h,cpp}`：周期栏新增「加入自选」checkable QPushButton——已是自选股显示「已在自选」checked 态 + 按下切换 add/remove + tooltip 提示；`onWatchlistChanged()` 与 `currentCodeChanged` 信号双重同步；通过 WatchlistStore/LogManager 依赖注入传入
- `ui/panels/sector_constituents_dialog.{h,cpp}`（NEW）：SectorConstituentsDialog —— 板块行右键「查看成分股」→ 模态弹窗（QDialog「板块名 成分股」+ QTableWidget 4 列「代码/名称/最新价/涨跌幅%」+ 按涨跌幅降序红涨绿跌 + 双击行开图 emitStockSelected + 窗口标题含板块名称）+ `loadSector(code, name)`（TDX getSectorConstituents + batchQuoteInteractive 灌行情）
- UI 装配：MainWindow — 自选股 Dock 左侧主位（`addDockWidget(Qt::LeftDockWidgetArea, watchlistPanel_)`）；市场 Dock 收编到「视图→市场」菜单（默认隐藏 toggleViewAction）；自定义指数 Dock 在自选股下方竖排（`splitDockWidget(watchlistDock_, customIndexPanel_, Qt::Vertical)`）；板块成分股右键菜单通过 MarketPanel 角色列过滤转发 connect
- 测试：WatchlistStoreTest 3 例（test_foundation）——SaveLoadRoundTrip / LoadEmptyWhenMissingOrBad / SaveEmptyProducesValidFile

### 验证
- 构建零警告；Foundation 52 → **55**（+3 WatchlistStore），总计 392 → **395** 全绿
- GUI 冒烟由用户手动执行（见 task-6-report.md 附录清单）

### 已知限制
- 自选股行情刷新依赖 TDX batchQuoteInteractive 交互优先级（10s 定时，批量报价大时可能延迟）
- 板块成分股弹窗初始加载无缓存（每次打开重拉数据）
- 自选股 Dock 右侧无盘口/关键数据联动（绑定主窗口中央图表交互；v2 可加信号）

## 2026-08-11 — P10 第十四轮：市场窗口合并板块窗口

### 需求
把独立的板块窗口合并进市场窗口作为平级 tab，统一刷新机制、减少 Dock 数量。用户选定本轮 = 市场窗口合并板块：4 平级 tab（涨幅榜/跌幅榜/行业板块/概念板块）+ 统一错峰刷新 + 板块双击开图 + 板块表模板同步涨幅榜；自定义指数 Dock 独立竖排。

### 实施
- `ui/panels/market_panel.{h,cpp}`：MarketPanel 由双 tab（涨幅榜/跌幅榜）重构为 4-tab（涨幅榜/跌幅榜/行业板块/概念板块）；市场宽度仍固定底栏；新增 `refresh()` 公开方法统一入口——行业/概念板块表用 TDX `getSectorIndices`（880xxx 全量源，行业 132 / 概念 438）+ `batchQuoteInteractive` 获取行情；新增 `buildSectorTab(type)`——创建 QTableView + SectorListModel（固定类型列，虚拟化渲染），双击行 `emitStockSelected(code, name, MarketType::Sector)`
- `ui/panels/sector_list_page.{h,cpp}`（NEW）：SectorListModel（QAbstractTableModel：3 列「板块/涨跌幅%/成交额」，table-driven ColMeta 列映射，SectorRow 存储，`setRows` 整表替换 + `data/sort` 委托）+ SectorListPage（QWidget 组装 QHeaderView + QTableView + 排序切换 + 双击板开图信号）
- `ui/panels/sector_panel.{h,cpp}`：SectorPanel 独立 Dock 移除——面板逻辑内嵌到 MarketPanel 的行业/概念 tab 中；原先独立的 `refresh()`/`batchQuoteInteractive` 调用链关闭
- `ui/main_window.{h,cpp}`：`customIndexPanel_` Dock 从与 sector 水平分拆改为独立竖排（`splitDockWidget(marketDock_, customIndexPanel_, Qt::Vertical)`）；移除 sectorDock 的 register/visibility/toggleViewAction 调用；`RefreshQuotes` 定时任务简化为只调 `marketPanel_->refresh()`
- 刷新错峰：MarketPanel::refresh() 内——市场池 `batchQuoteInteractive` 立即发起 t=0；行业 +1s `QTimer::singleShot`；概念 +2s `QTimer::singleShot`；seq 去陈旧防重复刷新
- 视觉同步：行业/概念板块表模板同步涨幅榜——移除硬编码黑底灰字、跟随 app 主题、涨跌幅列红涨绿跌

### 验证
- 构建零警告；392 全绿（纯 UI 重构+装配，无引擎/数据层变更，无新增单测）
- GUI 冒烟由用户手动执行（见 task-5-report.md 附录清单）

### 已知限制
- 市场宽度 tab 仍固定在底部（非平级 tab），因宽度数据与排名共用行情池
- 双击板块开 K 线图不联动右侧盘口/关键数据面板（绑定主窗口中央图表交互；v2 可加信号）

## 2026-08-11 — P10 第十三轮：全量 CSV 导出

### 需求
补齐需求文档「工具 (P8)」中**数据导出CSV/Excel**剩余项。用户选定 = 全量 CSV 导出，6 面板全覆盖。此前仅回测面板（手工 CSV）+ K线单图表（klineToCsv）有导出，选股/市场/板块/资金/日志均无。

### 实施
- `foundation/utils/csv.{h,cpp}`：新增 `csv::tableToCsv(rows)` 纯函数——「表头+行数据」→ CSV 文本，**UTF-8 BOM**（`\xEF\xBB\xBF`）保证 Excel 中文不乱码；复用 `joinRow` 转义；空表仅 BOM
- `ui/utils/table_csv_export.{h,cpp}`（NEW）：通用表格视图导出工具——`tableViewToCsv(QAbstractItemView*)`（表头=model->headerData 横向、单元格=data DisplayRole，QTableView/QTableWidget 统一）+ `exportViewToCsv(view, parent, defaultName)`（空表返回 false 不弹框；QFileDialog 保存；QFile 写 BOM；LogManager 记录）
- 6 面板接入「导出」按钮：选股结果（resultView_/screener_result.csv）、市场全景（当前 tab 涨幅榜/跌幅榜 → gainersView_/losersView_/market_ranking.csv）、板块行情（table_/sector_quotes.csv）、资金数据（龙虎榜 FundsDragonTigerPanel + 融资融券 FundsMarginPanel 各一按钮 → funds_lhb.csv/funds_rzrq.csv）、交易日志（recordsTable_/journal_records.csv）
- 回测面板**保留原导出逻辑**（现有 onExportClicked 含绩效指标 14 行 + 净值曲线 + 成交明细，多于表格内容，按 brief 规则不统一）
- 测试：CsvExportTest 5 例（test_foundation）——基础表/逗号引号转义/空表仅BOM/数字不科学计数/多行字段包引号

### 验证
- 构建零警告；387 → **392**（Foundation 47 → 52，+5 CSV 导出）
- 手动：6 面板导出 → CSV 用 Excel 打开中文正常（BOM）、列对齐、逗号/引号字段转义正确

### 已知限制
- 只做 CSV 不做 Excel(.xlsx)（CSV 可被 Excel 打开，覆盖需求；xlsx 需引库 v2 可选）
- 市场面板第 3 个 tab（市场宽度）点导出导的是跌幅榜（losersView_），与当前视图无关（brief 给定代码；UX 语义缺口可 v2 优化）
- 空表格时导出按钮静默无效（无提示；工具设计如此）
- 回测面板保留的手工导出无 UTF-8 BOM（既有行为，与新版工具编码不一致，后续可统一）

## 2026-08-11 — P10 第十二轮：多窗口开图对比

### 需求
补齐需求文档「工具 (P8)」中**多窗口模式**剩余项。用户选定 = 多窗口开图对比：把当前图表或任意股票开新窗口，多个完整图表窗口同时对比（各自独立缩放/周期/叠加/交易标记）。确认：①完整图表窗口（含周期栏/叠加/交易标记）；②中央图表「新窗口」按钮 + 右键菜单打开；③新窗口去筹码分布按钮（避免多窗口筹码联动复杂度）。

### 实施
- `ui/widgets/central_chart_widget.{h,cpp}`：构造加 `bool standalone = false`（standalone 时不建筹码分布按钮）+ `currentName()` getter + `openNewWindow(code, name)` 信号 + 周期栏「新窗口」按钮（loadStock/loadCustomIndex 后 enable）+ 右键菜单「在新窗口打开」→ `emitOpenNewWindow()`；修复：新签名在 parent 前插 bool 导致主窗口 `centralStack_` 隐式转 bool 误开 standalone——显式 `/*standalone=*/false`
- `ui/panels/chart_window.{h,cpp}`（NEW）：`ChartWindow : QMainWindow` 独立图表窗口——`new CentralChartWidget(provider, /*standalone=*/true)` 作中央 widget，`loadStock`（标题「名称 - 图表」+ 加载 + 刷新标记），`refreshTradeMarks()`（public，marshal 主线程 + currentCode 判空 + entries()→collectTradeMarks/deriveHoldings→setTradeMarks，仿 MainWindow），`openNewWindow` 信号转发（级联开窗）
- MainWindow 装配：`openNewChartWindow(code, name)`——new ChartWindow + `WA_DeleteOnClose` + 递归 `openNewWindow` 连接 + 级联偏移（`80+(cascade%6)*30, 60+(cascade%6)*30`）+ loadStock + show + `chartWindows_`（QPointer 容器）push_back；主窗口 centralChart_ 的 `openNewWindow` → openNewChartWindow；`refreshTradeMarks` 末尾遍历 chartWindows_ 分发（**onChange 覆盖式，窗口不自己注册 setOnChange**，由 MainWindow 统一刷新）
- 环境：vcpkg 目录被上次 worktree 删除误清 → 重新克隆官方 vcpkg + `VCPKG_MANIFEST_INSTALL=OFF` + `VCPKG_APPLOCAL_DEPS=OFF` + `VCPKG_INSTALLED_DIR` 复用主仓库 build/vcpkg_installed（worktree 无需 vcpkg.exe）

### 验证
- 构建零警告；387 全绿（无引擎新逻辑，无新增单测）
- 手动冒烟（用户验证清单）：主窗口开股 → 点「新窗口」→ 独立窗口完整功能；右键再开（递归）；多窗口并存互不影响；关窗不崩；录日志主+新窗口交易标记同步刷新

### 已知限制
- 新窗口不联动主窗口盘口/关键数据（绑定主窗口中央图表；v2 可加信号）
- 自定义指数图不从新窗口打开（入口在主窗口 customIndexPanel_ Dock）
- `chartWindows_` 只增不减（QPointer 自动置空，无真实泄漏；长期大量开窗 vector 缓慢增长）
- 主窗口无有效股票时日志变更不刷新新窗口标记（分发循环在 currentCode 判空后，边界场景）

## 2026-08-10 — P10 第十一轮：K线持仓标注 + 交易标记

### 需求
补齐需求文档「K线图 9 层渲染系统」中**持仓标注**与**交易标记**两层。用户选定本轮 = K线持仓标注+交易标记，并排除实盘券商 API 接入。确认：①模拟+实盘都画（颜色区分）；②成本线+买卖箭头+悬停浮框；③K线（日/周/月）+分时都标；④**实盘也推导持仓成本线（方案 B）**——实盘 ManualNote 手动录入，若只记买入不记卖出 FIFO 会误判为仍在持有，此风险用户已知并选择。

### 实施
- `engine/journal/trade_journal.{h,cpp}`：新增 `TradeMark`（模拟/实盘买卖点：code/type/direction/price/volume/fees/strategy/note/time）与 `HoldingLine`（当前持仓线：type/quantity/avgCost）结构；`collectTradeMarks(entries, code)` 按代码提取全类型标记（时间升序）；`deriveHoldings(entries, code)` 按 JournalType 各自独立 FIFO 推导当前持仓（买入含费用摊销、卖出从队首扣减、超量忽略、全卖不产出）；`TradeJournalEngine::setOnChange` 变更回调（addEntry/updateEntry/removeEntry/clear/appendAuto 锁外触发）——纯 C++17 无 Qt 依赖，可单测
- `ui/widgets/kline_chart.{h,cpp}`：`setTradeMarks(marks, holdings)`——模拟成本线青 #00e5ff 虚线 + 实盘橙 #ff9800 虚线（右端 `[模拟/实盘]持仓 N @ 价` 标签）+ 红▲买/绿▼卖箭头（QPolygonF）+ 悬停浮框追加交易行；`buildMarkBarIndex` 日线同日/周月周期包含对齐（bar.time=周期首日，交易日期落在 [bar.time,nextBar.time) 归前一根）；成本线纳入主图量程；**切股清空、切周期保留**（仅 code 变化才清——因 setPeriod 内部走 loadStock，无条件清会误清周期切换）
- `ui/widgets/time_line_chart.{h,cpp}`：`setTradeMarks`——只画当日交易（data_.date 同天判断），红▲买/绿▼卖箭头，切股清空；v1 不做分时悬停浮框（注释注明 v2 可加）
- `ui/widgets/central_chart_widget.{h,cpp}`：`setTradeMarks` 缓存转发（marks_/holdings_ 缓存 + reapplyTradeMarks 在 loadStock/setPeriod/自定义指数喂数据后重注入——因两图的 loadStock 会清标记）
- UI 装配：MainWindow `refreshTradeMarks`（currentCode 非法跳过；`journal_->entries()` → collectTradeMarks/deriveHoldings → centralChart_->setTradeMarks）+ `journal_->setOnChange` 注册（QueuedConnection marshal 主线程）+ 7 处中央图表加载点接线（搜索/指数条/市场/自定义指数/量化/资金/日志）
- 测试：TradeMarkTest 11 例（test_engine）

### 验证
- 构建零警告；376 → **387**（Engine 147 → 158，+11 交易标记）
- 手动：模拟交易成交 → K线/分时出现标记 + 成本线；手动录入实盘日志 → 红标 + 橙色成本线；悬停显示浮框；切日/周/月保留重定位、切股清空；删除/清空日志标记消失

### 已知限制
- 实盘成本线依赖录入完整性（方案 B 固有风险）：只记买入不记卖出会误判为仍在持有；模拟线因引擎自动落库始终可靠
- 分时悬停浮框 v1 不做（v2 可加）
- 点击箭头跳转详情 v1 不做（仅悬停浮框）
- Signal 类型条目在标注中全量显示（不过滤）

## 2026-08-08 — P10 第十轮：定时任务（刷新行情 / 跑选股 / 抓数据 / 提醒）

### 需求
用户选定下一轮 = 定时任务。设置菜单 → 独立「定时任务」窗口，支持四种动作（定时刷新行情 / 跑选股 / 抓数据 / 提醒）+ 两种触发方式（固定时间 / 周期），任务增删改即时生效并持久化到 `configDir/scheduled_tasks.json`。

### 实施
- `foundation/scheduler/scheduled_task.{h,cpp}`：ScheduledTask 数据模型（id / type / kind / timeOfDay / intervalSeconds / target / enabled / lastResult / running）+ `shouldFire` 调度判定纯函数（Daily 固定时间到点、Interval 按间隔；禁用/非法时间/零间隔不触发；Daily 距上次执行 > 60 秒防重窗）——纯 C++17 无 Qt 依赖，可单测
- `foundation/scheduler/scheduled_task_store.{h,cpp}`：JSON 持久化（configDir/scheduled_tasks.json，nlohmann 读写，目录自动创建，损坏回退空）
- `core/task_scheduler.{h,cpp}`：TaskScheduler（QTimer 10s tick）——任务容器 + CRUD（add/update/remove，变更即回调 onTasksChanged_ 通知 UI 落盘）+ `runNow` 立即执行 + 执行器注入（`setExecutor`，调度与动作解耦，UI 层注入动作实现）；`running` 标志防重入；Daily 任务触发后 lastRun_ 置当天 23:59:59 同日去重
- `engine/scheduler/screener_scope.{h,cpp}`：ScopeResolver 选股/抓数据范围解析——target JSON `{"scope":"all"}` 全部 A 股（getStockList + isTradableAShare/isIndexCode 过滤）/ `{"scope":"sector","sector":"880xxx"}` 板块（TDX 板块代码 → 名称 → 东财成分接口拉成分股）/ `{"scope":"last"}` 复用上次手动选股（v1 待接线，空则退化全部 A 股）；畸形 JSON 回退全部
- `ui/panels/task_window.{h,cpp}`：TaskWindow 独立窗口（设置菜单「定时任务(&T)…」，top-level QMainWindow 仿资金窗口）——任务表（类型 / 触发 / 启用 / 上次结果）+ 新建/编辑/删除/立即执行；TaskEditDialog 新建/编辑（类型→动作专属表单：选股范围 / 提醒文本；触发方式 Daily 时间 QTimeEdit 或 Interval 分钟 QSpinBox；板块下拉异步 fetchBoards；启用勾选）
- UI 装配：MainWindow `runScheduledTask` 动作执行器——RefreshQuotes 调 marketPanel_/sectorPanel_ refresh（SectorPanel/MarketPanel 公开刷新方法）、Remind 走 NotificationService 通知、RunScreener/FetchData 走 IO 池异步（QPointer 守卫 + runningAsync_ 防重入 + QueuedConnection 回主线程更新 lastResult）；scheduled_tasks.json 启动加载 + 变更即保存
- 测试：ScheduledTaskTest 7 + ScheduledTaskStoreTest 2 = 9 例（test_foundation）；ScopeResolverTest 5 例（test_engine）

### 验证
- 构建零警告；358 → 372 全部通过（Foundation 38 → 47，+9 任务模型/调度/存储；Engine 142 → 147，+5 范围解析）
- 手动：任务增删改即时保存到 scheduled_tasks.json、立即执行触发对应动作、lastResult 回填

### 已知限制
- `scope=last`（复用上次手动选股）为 **v2 待接线**——v1 中 lastScreenerConfig_ 未写入，此范围退化到全部 A 股
- 无交易日历感知（周末/节假日不跳过）
- 任务执行历史只存 lastResult（不存多轮）

## 2026-08-09 — 板块成分数据源（东财 datacenter）+ 定时任务板块选股修复

### 背景
用户测试定时任务「板块选股/抓数据」发现无效：`ScopeResolver::sectorStocks` 原实现返回 `StockCode("SH"+code)`（如 SHBK0475）——非法代码，选股/抓数据静默产空。审查定位为 I1。

### 数据源可行性实测
- TDX 网络协议**无板块成分命令**（命令集只有行情/列表/K线等）；TDX 成分在本地板块文件（tdxhy.cfg），用户无通达信安装目录
- 东财 `datacenter-web.eastmoney.com` **可用**（未被 clist 行情接口的 IP 封锁波及）——按板块名称查成分接口 `RPT_F10_CORETHEME_BOARDTYPE` 实测返回真实成分（煤炭 → BK0437 → 33 只）
- 板块名称桥接：定时任务存 TDX 880xxx 板块代码 → `getSectorIndices` 找名称（如 880301 → 煤炭）→ 东财按名称查成分

### 实施
- `data/eastmoney_sector_constituents.{h,cpp}`：`fetchConstituents(boardName)`——thread_local QNAM + 同步 fetch（超时/重试/Referer，仿 EastMoneyFundsProvider）+ 逐页拉全 + 纯静态 `parseConstituents`（可单测）+ URL 名称编码（QUrl::toPercentEncoding）
- `ScopeResolver::sectorStocks`：TDX 880 代码 → `getSectorIndices` 找名称 → 东财成分接口拉成分；找不到名称/网络失败返回空（runScheduledTask 提示）
- 测试 372 → **376**（+4：解析字段/空/畸形跳过/URL 编码）
- 实连验证：煤炭板块 33 只真实成分（甘肃能化/新大洲A/美锦能源/兖矿能源等）

## 2026-08-08 — P10 第九轮：交易日志（模拟vs实盘对比 + 费率设置）

### 需求
为 P10 交易日志模块添加完整功能：交易记录 CRUD、模拟vs实盘精确配对对比（逐笔价差/月度收益/已实现盈亏/按策略汇总/双序列收益曲线）、以及可编辑的四项费率设置。

### 实施
- `engine/journal/trade_journal.{h,cpp}`：TradeJournalEngine 内存存储 + 线程安全（mutex）；JournalEntry 含代码/名称/方向/价格/数量/费用/策略/注解；`appendAuto` 自动落库（指纹去重）；`computeStats` 对比回顾（GroupStats + 逐笔配对 + 月度汇总 + 按代码已实现盈亏 + 按策略统计）
- `engine/journal/trade_journal_store.{h,cpp}`：JSON 持久化（configDir/trade_journal.json + journal_config.json），条目数组 + 费率独立读写
- `ui/panels/journal_window.{h,cpp}`：JournalWindow 独立窗口（top-level QMainWindow，仿资金窗口）；两 tab——「交易记录」（CRUD + 筛选 + 类型徽标）+「对比回顾」（统计卡片 + 双序列收益曲线 EquityCurveWidget + 逐笔配对表 + 月度收益表 + 持仓已实现表 + 按策略表）；JournalEntryDialog 新建/编辑（StockSearchBar + 方向/价格/数量/费用自动算/策略/注解）；JournalFeeDialog 费率设置（佣金/最低佣金/印花税/过户费四行 QDoubleSpinBox + 保存到 journal_config.json）
- UI 装配：MainWindow 顶部「日志」菜单 + PaperTradePanel 自动落库回调（QPointer + QueuedConnection 安全异步）
- 测试：TradeJournalTest (5) + TradeJournalStatsTest (12) + TradeJournalPairTest (6) + TradeJournalStoreTest (5) = 共 28 例（全在 test_engine）

### 验证
- 构建零警告；358/358 全部通过（引擎 112 → 142，+28 交易日志相关单测）
- 手动：交易记录 CRUD + 筛选 + 模拟成交自动落库 + 对比回顾统计正确 + 费率设置保存/加载
- 已知限制: 已存日志条目不因费率变更重算费用（费率仅影响后续新建/编辑）；StockSearchBar 当前传 nullptr（无下拉建议）；JournalWindow 双击开图需 IDataProvider（当前通过 openChart 信号 → 主窗口中央图表）

## 2026-08-08 — 资金数据：移除北向资金（2024 披露调整）

### 背景
用户反馈北向资金无数据。排查确认：**自 2024-05-13 起交易所停止披露沪深股通（北向）盘中实时净买入/成交额**（[港交所公告](https://kxstock.hexun.com/2024-05-11/212816705.html)），且盘后仅保留每日成交总额 + 十大成交活跃股。

### 尝试（全部失败）
- kamt 快照/分钟：周末/任何时间返回额度占位值或 0（实时净买入已停披露）
- kamt.kline 历史：字段单位不可解释（沪股通 2.7 亿明显不对），且跨日数值陈旧
- 十大成交活跃股：datacenter-web 报表名（RPT_MUTUAL_DEAL_DET 等）全部「报表配置不存在」

### 结论
按用户指示（都不行则移除），删除「北向资金」tab + 北向数据层 + 相关单测。**保留** 龙虎榜（交易日下拉）+ 融资融券（市场总览 + 个股明细）。

## 2026-08-08 — P10 第八轮：资金数据（龙虎榜 / 北向资金 / 融资融券）

### 需求
用户选定下一轮 = 资金数据面板，并要求先测数据源可行性再决定。实测东财 datacenter-web + kamt 接口全部可用（封锁的是 clist/K线/分时，数据中心类不受影响）。呈现方式改为顶部独立「资金」菜单 + 独立窗口（仿量化工作台）。

### 数据源（全部实连验证）
| 数据 | 接口 | 验证 |
|---|---|---|
| 龙虎榜 | datacenter-web RPT_DAILYBILLBOARD_DETAILSNEW（按日期） | 周五 68 条 |
| 北向快照 | push2 kamt/get | 沪/深/南 净买入 |
| 北向分钟 | push2his kamt.rtmin（s2n） | 241 点 |
| 两融个股 | datacenter-web RPTA_WEB_RZRQ_GGMX（按股票） | 茅台 120 条 |
| 两融市场 | datacenter-web RPTA_RZRQ_LSHJ | 沪深 26398 亿 |

### 实施
- `data/eastmoney_funds_provider.{h,cpp}`：URL 构建 + 纯静态解析 + thread_local QNAM 同步 fetch（可任意线程）
- `ui/panels/funds_window.{h,cpp}`：FundsWindow 独立窗口 3 tab——龙虎榜（日期+榜单+双击开图）/ 北向资金（快照+分钟折线自绘）/ 融资融券（市场总览+个股明细，龙虎榜双击联动）
- 顶部「资金」菜单 → openFundsWindow；龙虎榜双击 → 主窗口中央图 + 联动两融明细
- 安全异步：QPointer 守卫 + shared_ptr + submitIO；closeEvent 不 waitForDone（吸取量化窗口教训）

### 验证
- 构建零警告；330/330 通过（+7 资金解析器单测）
- 实连：龙虎榜周五 68 条、两融市场 26398 亿、茅台两融 120 条、北向分钟 241 点
- 已知限制: 北向快照/分钟在周末为 0（数据源特性，交易日有值）；v1 不做北向历史图表/席位明细/两融历史图

## 2026-08-08 — P10 第七轮：自定义指数

### 需求
用户选定下一轮 = 自定义指数，要求：① 权重支持 手动 + 默认等权；② 管理面板 + 图表加载；③ 历史 + 实时点位。

### 设计（用户已确认）
- **计算口径**：历史 K 线用「价格加权 + 基点重定基」`指数(t) = 基点 × Σ[wᵢ·Pᵢ(t)] / Σ[wᵢ·Pᵢ(T₀)]`（P 为前复权收盘，T₀ = 首个共同数据日），日线算完聚合出周/月线（与真实指数一致）；分时/实时用「从指数昨收做加权涨跌幅外推」`昨收 × (1 + Σ wᵢ·(Pᵢ(t)/昨收ᵢ − 1))`——绕开复权价与实时裸价的衔接断裂
- **图表集成**（方案 A 中央图表外部数据模式）：KLineChart/TimelineChart 各加 `setExternalReloader(fn)` + `loadBars/loadIntraday`；CentralChartWidget::loadCustomIndex 编排，切日/周/月/分时都异步重算喂数据；切回普通股票退出该模式

### 实施
- `engine/analyzer/custom_index.{h,cpp}`：数据模型 + computeIndexBars/computeIndexIntraday/computeIndexLive/lastCompletedClose/normalizeWeights（fetch 回调注入可单测；内部自行归一化权重）
- `engine/analyzer/custom_index_store.{h,cpp}`：JSON 持久化（configDir/custom_indexes.json）
- `ui/widgets/custom_index_editor.{h,cpp}`：编辑器（StockSearchBar 加股、权重默认等权、均分按钮、基点）
- `ui/widgets/custom_index_panel.{h,cpp}`：左区 dock（与板块 tab 并列）——列表 + 实时点位（订阅成分股行情）+ 新建/编辑/删除/打开图表
- 图表外部数据模式 + CentralChartWidget 编排
- `tools_custom_index_live.cpp`：实连校准工具

### 验证
- 实连: 茅台+平安+招行等权组合日线 2086 根（2018-01-02→2026-08-07），基准日=1000，昨收 3049.54，分时 240 点，实时外推 -0.36%（= 今日三只涨跌幅 +0.05/-0.71/-0.44% 加权，一致）
- 构建零警告；322/322 通过（+22 引擎：指数公式/周月聚合/停牌 carry-forward/上市晚剔除/分时外推/实时外推/权重归一化 17 + store 5）
- 已知限制: 实时为「面板点位 + 图表按需加载」，图表最后一根蜡烛不实时刷新；市值加权（③）未做

## 2026-08-07 — 板块/概念叠加改用通达信板块指数（脱离东财封锁）

### 背景
叠加板块/概念一直失败：东财 clist 被 IP 封锁 → 板块列表降级新浪（new_xxx 代码）→ 东财 K线接口不认；随后东财 push2his（板块 K线/分时源）也被封锁。指数/个股叠加走 TDX 正常，唯独板块无可用数据源。

### 可行性测试（TDX 板块指数）
- TDX 标准服务器（:7709）返回 880xxx 板块指数 K线，但记录格式同指数（多 4 字节涨跌家数）——用 isIndex=true 解码完美（880301 煤炭 1834→1895 连续日期）
- SH 股票列表含全部板块指数（652 个 880xxx：大盘 880001-099 / 地域 8802xx / 行业 8803xx-8804xx / 概念 8805xx+），带名称，可自动构建代码列表
- 分时（getIntraday）对 880xxx 也支持（240 点）

### 实施
- `IDataProvider::getSectorIndices()`（默认空；TdxProvider 从 SH 列表过滤 880xxx + 缓存；MultiProvider 转发）
- `tdx::isSectorIndexCode`：880xxx 判为指数格式；getBars/fetchBarsRaw 用它做 isIndex 解码
- CentralChartWidget 启动后台预热板块指数缓存（与股票搜索/市场面板复用 SH 列表缓存）
- OverlayDialog 行业/概念 tab 改用 TDX 板块指数（行业 8803xx-8804xx、概念 8805xx+，过滤大盘/地域；每 tab 保留搜索框）
- fetchOverlayData：880xxx 板块代码走 TDX getBars/getIntraday；非 880 仍走东财（BK/suggest）

### 验证
- 实连: 652 板块指数列表；煤炭/证券/5G概念/DeepSeek 日K 640 根 + 分时 240 点正常
- 构建零警告，300/300 通过（isSectorIndexCode + isTdxSectorCode 2 例）
- 板块/概念叠加现在完全脱离东财，稳定可用

## 2026-08-07 — 行情/K线加载变慢排查与修复（非本轮功能代码）

### 症状
用户反馈「股票的数据和K线图表等加载速度变慢了很多」。

### 排查（实测取证）
- chart_render 走真实 app 路径：TDX 连接 + K线 + 分时在 3.5s 固定等待内完成 → **单次 K 线加载并不慢**
- **tdx_market_probe 实锤**：MarketPanel 同款操作（全 A 股列表 + ~5211 只 batchQuote）耗时 **68.6s**（~87 次串行 TDX 往返，当前每请求 0.4~1.6s）
- MarketPanel 每 30s 刷新一次，刷新耗时 > 间隔 → 批量报价近乎常驻占用 TDX 连接 + 1 个 IO 线程
- **IO 池仅 2 线程**：批量报价占 1 线程 + 行情轮询占另 1 线程 → 期间选股 getBars 排队 ~68s
- 本轮叠加功能**未改动加载路径**（loadStock/getBars 未动，只加了按需触发的叠加）

### 修复
1. `ThreadPool::ioPool` 2 → **6 线程**（[thread_pool.cpp:18](src/core/thread_pool.cpp#L18)）
2. `TdxProvider::batchQuote` 每 chunk 间 `sleep_for(5ms)` 让步（[tdx_provider.cpp:510](src/data/tdx/tdx_provider.cpp#L510)）——TDX 连接为每命令加锁（executeCommand 单命令持锁），等待中的交互请求得以插入

### 验证
- 临时 io_contention 工具实测：批量报价期间 getBars 由「等完整批次 ~68s」降至「~4-6s 完成（日 K = K线+除权 2~3 命令）」
- 服务器延迟极不稳定（0.4~1.6s 波动），测量受噪声主导；机制成立
- 构建零警告，295/295 通过
- **根因主因是 TDX 服务器当前延迟高**；全市场刷新是放大器。服务器恢复后刷新将回到 ~4s，交互加载不受影响

## 2026-08-07 — P10 第六轮：指数/板块/概念叠加对比（分时与日/周/月各自独立叠加）

### 需求（用户三连追加）
1. 叠加标的**可自选指数、板块、概念**
2. **分时图、日线、周线、月线都要能叠加**
3. **叠加按视图隔离**——在分时叠加就只叠加分时，在日/周/月叠加就只叠加 K 线图，不跨视图同时生效

### 功能
- 中央周期栏「叠加对比」按钮（作用于当前显示的图）→ 对话框三来源：指数/个股（4 大指数快捷 + StockSearchBar 任意搜索）、行业板块列表、概念板块列表（按 tab 懒加载，IO 池异步）
- 分时（TimelineChart）：叠对方分时价格线（按分钟对齐，起点与首匹配点重合）
- 日/周/月（KLineChart）：叠对方对应周期 K 线收盘线（按日期对齐）+ 可选「相对强弱」副图（个股/叠加标比值，锚点=100 + 参考虚线 + 轴标签）
- 生命周期：切股清叠加（作废在途）；K 线图内切日/周/月**保留叠加并重取**（setPeriod save/restore）；分时 10s auto-refresh 只重对齐缓存分时不重拉

### 引擎（可单测）
- `overlay_analysis.{h,cpp}`：`OverlayTarget`（Security/Sector 二态）+ `alignOverlay`（K 线按日期对齐）+ `alignIntradayOverlay`（分时按分钟对齐）
- rebase 锚点 = 可见区第一个 matched，每帧在 computeVisibleRange/computeRanges 重算 → 平移/缩放起点始终与左缘对齐；叠线值纳入主图量程保持可见

### 数据
- `EastMoneySectorProvider` 扩展东财 push2his：`fetchSectorKline`（kline/get，secid=90.BKxxxx，klt 101/102/103）+ `fetchSectorTrends`（trends2，ndays=1）；3-host 回退；纯静态解析可单测
- **实连校准通过**：行业 BK0475 日 K 与分时均返回真实数据（该主机群未被 clist 封锁波及，push2his 正常）

### 验证
- 构建零警告（clean-first，因改了 kline/time_line/central 三处头文件）
- 测试 274 → **295**（alignOverlay 7 + alignIntradayOverlay 5 + 板块K线解析 5 + 板块分时解析 4）

## 2026-08-07 — 筹码分布默认收起，视图菜单按需打开

筹码分布 Dock 改为默认隐藏（restoreState 后强制 `chipDock_->hide()`，不随历史布局常驻），视图菜单加 `chipDock_->toggleViewAction()` 勾选开关。隐藏期间面板保留上次计算结果，打开即显示（crosshair 日期联动仍生效）；数据在个股切换/十字光标时照常计算。构建零警告，274/274 通过。

## 2026-08-07 — 板块模块简化：全量 treemap → 涨跌幅 Top10 榜单

用户反馈板块全量热力图太复杂。将 `SectorPanel` 的 SectorHeatmap（Squarified treemap，行业 49 / 概念 175 格）替换为 **QTableWidget 前十榜单**（板块/涨跌幅/领涨股/成交额 4 列，红涨绿跌，按涨跌幅降序取前 10）。删掉悬停详情与空态提示改 QStackedWidget（表/暂无数据）。treemap 引擎代码与 6 个单测保留（可复用）。构建零警告，274/274 通过。

## 2026-08-07 — 板块行情数据一直拉不到 → 根因：东财 clist 被 IP 级封锁，加新浪兜底

### 症状
板块热力图恢复显示后，数据一直拉不到（面板空态/获取失败）。

### 排查（curl 实测取证）
- `sector_calib` 复现：东财 clist 全部主机 `Connection closed`，count=0
- **curl 独立验证**：`/api/qt/clist/get` 连 curl 都 HTTP 000 空回复；参数/Referer/ut 时间戳/子域/HTTPS 全试遍 → 全部 000
- 同主机其他接口 `ulist.np`（实时行情）、`push2his`（K线）→ **HTTP 200 正常**
- 结论：**东财 CDN/WAF 对本 IP 封锁了 clist 整条路径**（早前压测触发），非代码 bug、非普通限流

### 修复（新浪板块行情兜底，akshare stock_sector_spot 同款）
- `EastMoneySectorProvider::fetchBoards`：东财 clist 优先（重试降到 1 次/主机，有兜底不必久等）→ 连续 2 次全败后进程内隔离（`g_eastMoneyStrikes` 原子计数），直接走新浪
- 新浪接口：行业 `vip.stock.finance.sina.com.cn/q/view/newSinaHy.php`、概念 `money.finance.sina.com.cn/q/view/newFLJK.php?param=class`
- `parseSinaPage`（纯静态可单测）：**先整体 GBK→UTF-8 再解析 JSON**（GBK 第二字节可能含 `"`/`\`，先转码才安全）；字段 代码/名称/平均价/涨跌幅/成交额/领涨股涨跌幅/领涨股名称
- 新浪无换手率/涨跌平家数 → 对应字段 0；悬停详情条件显示涨跌平段（全 0 省略）
- 进程重启自动复测东财，封锁解除即回切富数据源
- 验证：行业 49 / 概念 175 条真实数据；首刷 ~1s（原来 ~7s）；测试 270 → **274**（新浪解析 4 例）
- **已知取舍**：新浪行业仅 49 个（东财 86+）；无换手率/涨跌平家数（详情已隐藏）

## 2026-08-07 — 恢复板块热力图面板

定位关闭堆损坏时（见下）为二分临时注释掉的板块 Dock 未恢复，导致左区板块热力图消失。根因已确认与板块面板无关（陈旧对象/ABI 错位，clean rebuild 解决），现恢复 `SectorPanel` Dock 创建（行业/概念 Treemap + 30s 自动刷新）。构建零警告，ctest 270/270 通过。

## 2026-08-07 — 修复②：`_CrtIsValidHeapPointer` 崩溃（同根因=陈旧对象，clean rebuild 解决）

### 症状
用户复测关闭又弹 `Debug Assertion Failed! Expression: _CrtIsValidHeapPointer(block)`（debug_heap.cpp:904，free 收到无效指针）。

### 排查
1. 用户回滚了代码后未 rebuild → **又是陈旧对象/ABI 错位**（与上一轮同根因）
2. 全量 `--clean-first` rebuild → 复测 3/5 轮干净退出、0 CRT 报告
3. 检测方法修正：进程存活 ≠ 崩溃——waitForDone 会等限流的东财 IO（启动板块/换手率拉取，每主机 3 重试），关闭需 ~8s，之前误判为「慢退出即崩溃」

### 修复
- 全量 clean rebuild（陈旧对象一次性根因）
- 顺手：移除 closeEvent 里 `provider_->disconnect()`（与在飞 IO 线程竞争 TDX 内部状态）——~MainWindow 已在 waitForDone 排空后再 disconnect，更安全
- 移除 datetime.cpp 未引用 `from_time_t`（C4505 警告）
- 移除全部临时诊断（stderr 重定向/栈回溯 hook/检查点）
- **已知小问题**：东财限流期间启动后立即关闭，waitForDone 等启动 IO → 关闭慢 ~8s（非崩溃，限流恢复后消失）

## 2026-08-07 — 修复：关闭时堆损坏（根因=陈旧对象/ABI 不一致，clean rebuild 解决）

### 症状
用户测试画线工具后关闭 StockTerminal，**每次必现** `HEAP CORRUPTION DETECTED: after Normal block`（非确定性块号 #8014/#8016/#8018）。

### 排查（系统化二分）
1. 临时把 CRT 报告重定向 stderr（main.cpp `_CrtSetReportMode`）→ **无人值守静默复现**：启动+WM_CLOSE 必现
2. **基线对比**：`git stash` 后测 58de071（round 3）→ **干净退出** → 确定是 round 4/5 未提交改动引入
3. 二分：禁用板块面板 + K线画线按钮 → 仍崩；移除 closeEvent disconnect → 仍崩
4. 加 `_CrtCheckMemory()` 检查点 → **`HEAP CORRUPT at startup`**（窗口 show 后、事件循环前堆已损坏）→ 定位到 `new CentralChartWidget` 构造段
5. **根因**：`git stash` 恢复时增量构建混用新旧目标文件 → KLineChart 等类**布局不一致（ABI 错位）** → 成员写入越过堆缓冲区，非确定性块号即源于此
6. **全量 `--clean-first` rebuild → 修复**，启动+关闭干净退出

### 修复（代码层面）
- 全量 clean rebuild 消除陈旧对象（一次性根因，非代码 bug）
- **趋势线预览跟随**：mouseMoveEvent 顶部先记录 `mouseX_/mouseY_`（原始光标坐标，不依赖 bar 量化），预览线改 2px 金色实线 → 拖拽时逐像素跟随光标
- 画线/导出按钮移到 K线图**右上角**（stretch 前置 + 右对齐），显眼样式（亮字+边框+激活金色高亮+竖分隔线）
- 移除临时诊断（main.cpp / main_window.cpp crtdbg 检查点）
- 测试 270/270 全过；构建零警告

### 经验
- **git stash 恢复后必须 clean rebuild**：增量构建可能残留按旧 header 编译的目标文件，导致类布局 ABI 错位 → 隐蔽堆损坏
- 排查大法：`_CrtSetReportMode(MODE_FILE)` 重定向 CRT 报告到 stderr 可无人值守复现；`_CrtCheckMemory()` 检查点定位损坏发生时机

## 2026-08-07 — P10 第五轮：数据导出 CSV + K线截图 + 画线工具

### 背景
用户选定范围 = 数据导出 CSV + K线截图 + 画线工具（纯本地工程，不依赖网络，不受东财限流影响）。给图表补齐「留痕」能力：标注画线、保存图像、导出数据。

### Step 1 — CSV 工具（新建 `foundation/utils/csv.{h,cpp}` + 4 测试）
- `csv::escape`（逗号/引号/换行转义）、`csv::joinRow`、`csv::klineToCsv`（日期,OHLC,量(股),额(元),换手率）
- **修一个坑**：默认 ostream 精度 6 把大额（1050000）输出成科学计数 `1.05e+06` → 数值列改 `std::fixed` 定精度（价格/额 2 位、换手率 4 位）
- 测试 `test_foundation/test_csv.cpp` 4 例全过

### Step 2 — 画线工具（改 `kline_chart.{h,cpp}`）
- 控制条追加 水平线 / 趋势线（互斥 checkable，可再点取消）+ 清除标注 + 导出
- `ChartLine` 锚定（bar索引, 价格）→ 用已验证坐标逆函数 `indexAtX`/`priceFromY`，平移/缩放时标注随数据稳定
- 交互：水平线点击即画；趋势线按下记起点 → 拖拽预览 → 松开提交（退化同 bar 同价忽略）；非画线模式恢复正常十字/平移
- 绘制：paintEvent 在十字线前插 `drawAnnotations`，亮黄 #ffd700 虚线，水平线右轴标价；`loadStock` 切股清空标注
- 导出：`exportData()` → QFileDialog → `csv::klineToCsv(bars_)` 写文件 → LogManager 记路径

### Step 4 — K线截图（改 `central_chart_widget` + `main_window`）
- CentralChartWidget 加 `currentCode()` getter
- 文件菜单「截图当前图表(&S)…」：`centralChart_->grab()` → `dataDir/screenshots/`（QDir mkpath）→ `screenshot_<code>_<时间戳>.png` 自动保存 → statusBar + LogManager

### Step 5 — 回测结果导出（改 `backtest_panel` + `trade_table_model`）
- TradeTableModel 加 `trades()` getter；BacktestPanel 存上次 `Performance`+`std::vector<Trade>`
- 「导出结果」按钮 → QFileDialog → CSV 三段：绩效指标（14 项）/ 净值曲线（序号,净值）/ 成交明细（时间,代码,方向,价格,数量,成交额,费用）

### 验证
- 测试 266 → **270**（csv 4 例）；构建零警告；应用 6s 存活
- 待用户 run.bat 冒烟：画线/截图/导出

## 2026-08-06 — P10 第四轮：板块指数 + 概念热力图（东财板块行情 + Squarified Treemap）

### 背景
用户选定下一步范围 = 板块指数/概念热力图。交付左区「板块」面板：行业/概念板块 Squarified Treemap 热力图（面积∝成交额、颜色∝涨跌幅红涨绿跌），悬停详情，30s 自动刷新。

### Step 1 — 数据层（新建 `src/data/eastmoney_sector_provider.{h,cpp}`）
- 东财 `clist/get` 板块接口：`fs=m:90+t:2` 行业 / `fs=m:90+t:3` 概念；字段 f2指数 f3涨跌幅 f6成交额 f8换手率 f12代码 f14名称 f104/f105/f106涨跌平家数 f128领涨股 f136领涨股涨跌幅
- **实测：板块接口每页上限 100（pz=600 也被截断）→ fetchBoards 自动分页直到 total；行业 ~496 / 概念 ~504 细分板块（东财 m:90+t:2 现已含 500 细分行业）**
- **限流健壮性**：主域 `push2.eastmoney.com` 被 CDN 限流（clist 连接被丢弃，ulist.np 正常）→ **主机列表回退**（push2/2.push2/1.push2/3.push2，每主机尝试分页，首个非空返回）；fetch 记录 errorString
- `parsePage` 纯静态可单测：兼容 `data.diff` 数组/对象形态；字段缺失默认值；畸形返回空

### Step 2 — 引擎层（新建 `src/engine/analyzer/treemap.{h,cpp}`）
- `Treemap::layout(w, h, weights)`：Squarified 矩形树（Bruls et al.），权重降序 + 行贪心（行最差纵横比不再改善即翻行）+ 溢出守卫
- 不变量：矩形铺满 w×h、不重叠、面积守恒（单测锁定）

### Step 3 — UI 面板（新建 `src/ui/panels/sector_panel.{h,cpp}`）
- 布局：顶行 [行业板块][概念板块] tab + 刷新 + 更新时间；中部自绘 `SectorHeatmap`（treemap：tile 填充色按 |涨跌|/5% 饱和度插值，平盘灰 #3a3a3c，字号按 tile 自适应）；底部悬停详情（名称/涨跌幅/领涨股/涨跌家数/成交额）
- 异步：IO 池 fetchBoards（shared_ptr provider + QPointer 守卫 + gen_ 世代守卫）→ 主线程 applyBoards → treemap 布局（主线程，~500 项微秒级）+ repaint；resize 重排
- 30s 定时器；showEvent 启动 / hideEvent 停止；**获取失败空态明确提示「板块数据源暂不可用，将自动重试」**（限流可自愈）

### Step 4 — 接入 MainWindow + CMake
- 左区 `sectorDock`（市场面板下方垂直分割，tr("板块")）；st_data +eastmoney_sector_provider、st_engine +treemap、st_ui +sector_panel、工具 `sector_calib`
- tests：test_data +test_sector_provider（5 例）、test_engine +test_treemap（6 例）

### Step 5 — 实连验证
- **sector_calib 实连成功**（限流前）：行业 496 只 top=钨+6.85%/种子+6.07%/焦煤+5.82%（中钨高新领涨 8.83%）、概念 504 只 top=昨日连板+5.93% —— 全部真实合理
- **东财 clist 限流事件**：连续校准请求（~20 次/2min）触发 CDN IP 级过滤（clist 连接 http_code=000 被丢弃，ulist.np 仍正常，1.push2/2.push2/3.push2 均受影响）→ 增加主机回退 + 空态提示；限流为临时状态，正常运行（30s 间隔）不触发
- 测试 255 → **266**（板块解析 5 + treemap 6）；构建零警告；应用 6s 存活
- 待用户 run.bat 复验（限流恢复后热力图出数据）

## 2026-08-06 — P10 第三轮细化②：筹码面板精简（去掉当日成交/区间统计，加 70% 成本区间）

### 用户反馈
筹码面板只保留**平均成本 / 获利盘 / 集中度 / 90%成本区间**，去掉「当日成交分布」图和其余统计（区间涨跌幅/振幅/换手率/均价/量额），**新增 70% 成本区间**。

### 实现
- **引擎**：`ChipDistResult` 新增 `costLow70/costHigh70`（筹码加权 P15/P85）；`compute()` 统一计算 P5/P15/P85/P95
- **UI 面板**：自绘区只画筹码云（全宽横条，低于现价红获利/高于现价绿套牢，现价虚线）；统计网格 10 项 → 5 项（平均成本/获利盘/集中度/90%成本区间/70%成本区间）；`requestData` 不再拉分时、`computeAndApply` 不再算成交分布与区间统计；移除 `txn_/range_/intraday_` 成员
- 引擎 `TransactionDistribution`/`RangeStats` 类保留（chip_calib 仍验证），仅面板不再消费
- 测试 +1（70% 区间窄于 90% 区间，双价位 P15≈10/P85≈20）→ **255/255**；构建零警告；应用 6s 存活
- 待用户 run.bat 复验

## 2026-08-06 — P10 第三轮细化：筹码按日期锚定（K线十字光标联动）

### 用户反馈
1. **筹码区间乱码**：统计标签/区间按钮用 `QLatin1String` 包裹 UTF-8 中文 → 单字节解析变乱码。修复：`QString::fromUtf8`（其他文字走 tr/QStringLiteral 本就 UTF-8 安全）
2. **筹码不要窗口区间**：去掉「全部/250/120/60 日」预设，筹码分布改为**按日期锚定**——默认最新一天，K线图（日/周/月任一）十字光标悬停某根 K 线 → 筹码面板显示**该日期的筹码分布**（全部历史截至该日）

### 实现
- `KLineChart` 新增信号 `crosshairDateChanged(std::optional<DateTime>)`：mouseMoveEvent 悬停 K 线日期变化时发出（lastEmittedDate_ 防重复）；leaveEvent/数据重载发 nullopt；`CentralChartWidget` 转发（分时不发）→ MainWindow connect → `ChipPanel::setAsOfDate`
- `ChipPanel`：`computeAndApply` 由「尾部 N 根窗口」改为「as-of 索引前缀」（所有 time<=目标日期的日K，nullopt=最新）；区间统计取截至该日的近 250 根（避免全历史前复权涨跌幅失真）；`pending_` 处理计算在飞时的新日期请求（完成后补算）；顶部新增「筹码截至 YYYY-MM-DD」标签
- 日K拉取起点 2015 → 2005（覆盖周/月线更早日期）；as-of 早于最早数据 → 取最早一根
- 修复：`setAsOfDate` 在 bars_ 未加载时忽略；快速切股/悬停竞态由 gen_ + busy_/pending_ 处理
- 构建零警告；应用 6s 存活；待用户 run.bat 复验（悬停各周期 K 线看筹码随日期变化）

## 2026-08-06 — P10 第三轮：筹码分布 + 成交分布 + 区间统计（看盘分析工具）

### 背景
用户选定下一步范围 = 筹码分布 + 成交分布 + 区间统计（专业行情软件核心看盘工具）。纯本地计算、不依赖新网络接口。本轮交付一个独立右区 Dock「筹码分布」面板 + 引擎计算模块 + 校准工具。

### Step 1 — 引擎计算模块（新建 `engine/analyzer/chip_distribution.{h,cpp}`）
- **`ChipDistribution`**：经典「三角分布 + 换手率衰减」模型。逐日把当日成交量按三角形分布摊到 [low,high] 价位桶（峰值在 typical=(H+L+C)/3），每日先按当日换手率（volume/floatShares）衰减既有筹码再累加；200 个价位桶。派生：平均成本（筹码加权）/ 获利盘比例（≤现价筹码占比）/ 90% 成本区间（筹码加权 P5/P95）/ 集中度（(P95-P5)/(P95+P5)）
  - floatShares>0 时总量归一化为流通股本；floatShares=0（东财不可用）→ 纯量模式不衰减不归一化，UI 相对显示
- **`TransactionDistribution`**：当日分时（累计量差分）或逐笔 → 价格×成交量直方图（默认 60 桶）
- **`RangeStats`**：区间涨跌幅（(末close-首open)/首open）/ 振幅 / 换手率（需 floatShares）/ 均价（额/量）/ 总成交量额
- 全部纯 C++17、无 Qt 依赖、静态方法可单测

### Step 2 — 单元测试（新建 `tests/test_engine/test_chip_distribution.cpp`，13 例）
- 筹码：单价位集中 / 双价位获利盘≈50% / **换手率衰减**（100% 换手日旧筹码归零聚向新价）/ 纯量模式不崩 / 空与非法 bar / 集中度紧密<宽松
- 成交分布：逐笔分桶 / 分时差分分桶 / 空输入
- 区间统计：涨跌幅 / 高低温振幅 / 均价额量 / 空与非法
- **ctest 241 → 254 全过；构建零警告（MSVC /W4）**

### Step 3 — UI 面板（新建 `ui/widgets/chip_panel.{h,cpp}`）
- 布局：顶行（名称代码+现价+刷新）→ 区间预设（全部/250/120/60 日，QButtonGroup 互斥，默认 250）→ 自绘区 → 统计网格 10 项
- **自绘区**（私有 ChipChartArea::paintEvent）：左 55% 筹码云（每价位桶从中心竖线向左横条，宽∝筹码；**低于现价红获利 / 高于现价绿套牢**，现价虚线）+ 右 45% 当日成交分布（竖向柱，高∝量）+ 共享价格轴（5 档刻度）
- 统计：平均成本 / 获利盘 / 90%成本区间 / 集中度 / 区间涨跌幅（红涨绿跌）/ 振幅 / 换手率（无流通股本显 "—"）/ 均价 / 总成交额 / 总成交量
- **安全异步**（沿用项目模式）：IO 池拉日K(2015→now)+分时+东财流通股本（fundProvider_ shared_ptr 按值捕获 + 移回主线程析构）→ Worker 池计算 → QPointer 守卫 + `gen_` 世代守卫回主线程；预设切换仅重算已缓存数据不重拉
- `setStock` 重置 `busy_=false` 允许快速切股时新拉取启动（旧任务由 gen_ 丢弃），修复了「切股时在飞任务导致卡加载」的竞态

### Step 4 — 接入 MainWindow + CMake
- 右区新增 `chipDock`（盘口下方垂直分割，宽 260-400）；3 处 connect lambda（搜索栏/市场面板/量化窗口 openChart）各加 `chipPanel_->setStock`；main_window.cpp include chip_panel.h
- `src/CMakeLists.txt`：st_engine +chip_distribution.cpp、st_ui +chip_panel.cpp、新增 `chip_calib` 工具；`tests/CMakeLists.txt`：test_engine +test_chip_distribution.cpp

### Step 5 — 校准工具 + 实连验证
`chip_calib`（TDX 日K/分时 + 东财流通股本 → 引擎计算打印）实连茅台 600519：
- 近 250 日：平均成本 1347.42 / 现价 1308.55 / **获利盘 29.8%**（股价自 ~1470 回落，七成套牢合理）/ 90%区间 1193.74~1463.53 / 集中度 10.2%
- 区间统计：涨跌 -6.60% / 振幅 27.78% / **换手 83.75%**（年换手率与茅台实际吻合）/ 均价 1391.04
- 当日成交分布：51 桶 / 总量 254.6万股（≈当日成交）
- 全部 11 年窗口涨跌幅极大（前复权老价被压低）属预期；面板默认 250 日
- 应用 6s 存活无崩溃；ctest 254/254 全过

### 待办
- [ ] 用户 run.bat 冒烟：切股出筹码云/成交分布/区间统计；预设切换更新；无东财时换手率 "—" 不崩
- [ ] K 线图内嵌筹码云（本轮独立面板，叠加留后续）
- [ ] 全窗口（全部）前复权极端涨跌幅展示优化（当前技术上正确）

## 2026-08-06 — 市场面板补换手率（东财 ulist 批量）

- **背景**：主窗口市场面板（涨幅榜/跌幅榜）换手率列 TDX 报价不含该字段 → 恒显"—"
- **实现**：
  - `AKShareProvider::batchQuoteFundamentals(codes)` — 东财 `ulist.np/get` 一次请求多代码，返回批量基本面（复用 `parseFundamentals`）
  - `MarketRankModel::updateTurnover(fullCode, turnover)` — 按代码回填换手率 + `dataChanged` 刷新该行
  - `MarketPanel` 挂 `fundProvider_`（shared_ptr\<AKShareProvider\>，与关键数据 widget 一致），`onQuotesReady` 后异步批量拉显示股票（涨幅榜+跌幅榜 top30）换手率回填；安全异步（shared_ptr 按值 + QPointer 守卫）
- 构建零警告；ctest 241/241 全过

## 2026-08-06 — 修复：关闭时堆损坏（第三轮：QuantWindow 关闭竞态，已解决）

### 症状
关量化工作台仍偶发 `HEAP CORRUPTION DETECTED`，用遍所有功能后必现。

### 排查（三轮审计 + 无头验证）
1. 第一轮：修复异步 lambda 悬垂 this/DataCache 裸指针（7 面板 shared_ptr cache + QPointer）
2. 第二轮：修复 TdxProvider detached 线程 stopThreads_ 语义 + MainWindow 关闭 waitForDone + 定时器析构
3. 第三轮：**PatternPanel 漏改**（`cache_.get()` 裸指针异步捕获）→ 关量化工作台直接原因，修复
4. **无头引擎堆压力测试**（tools_heap_stress，15 轮真实 2015-2026 数据跑 网格/压力/蒙特卡洛/形态识别，每步 `_CrtCheckMemory`）：**引擎全部堆完整** → 排除计算越界写

### 最终根因
**QuantWindow 关闭时后台任务与面板销毁竞态**：面板在飞任务持有 QPointer 守卫 + shared_ptr cache，但关窗瞬间 worker 的 `invokeMethod(QPointer)` 跨线程读 QPointer（与主线程销毁并发）存在 use-after-free 窗口，非确定性触发。

### 最终修复
- **`QuantWindow::closeEvent` 先 `ThreadPool::ioPool()->waitForDone()` + `workerPool()->waitForDone()` 再销毁面板**：串行化「任务完成 → 排空 → 面板销毁」，彻底消除竞态窗口；正常关闭无在途任务时立即返回不卡顿
- 叠加 MainWindow::~MainWindow 排空（app 退出路径）+ 全部面板 QPointer 守卫 + shared_ptr cache

### 验证
- 用户复测：用遍量化工作台功能 + 关闭，**不再弹堆损坏**
- 构建零警告；ctest 241/241 全过；新增 `heap_stress` 无头诊断工具（可复跑定位未来越界写）

## 2026-08-06 — 修复：P10 第二轮引入的堆损坏（异步 lambda 悬垂 DataCache / this）

### 症状
用户运行后关闭时再次弹出 `HEAP CORRUPTION DETECTED: after Normal block(#12337)`。

### 根因（code-reviewer 专项审计确认）
1. **AdvisorPanel / OptimizationPanel / Screener / Backtest / StressTest / StrategyCompare 的 IO/worker lambda 捕获指向成员 `unique_ptr<DataCache>` 的裸 `DataCache*`**：面板销毁后 cache_ 释放，worker 继续 `cacheBars()`（写入已销毁的 unordered_map）+ `cfg.cache` 悬垂读取 → 越界写堆
2. **kline_chart / time_line_chart / 各面板** 直接捕获裸 `this`（上一轮只修了轮询部件，图表与量化面板遗留）
3. **StockKeyDataWidget 的 `fundProvider_`（shared_ptr<AKShareProvider>）在 IO 线程析构** → `~AKShareProvider` → `http_->clearAccessCache()` 跨线程访问主线程 QNAM

### 修复（全部统一为安全异步模式）
- **7 个面板**（advisor/optimization/screener/backtest/stress_test/strategy_compare）`cache_` 改 `shared_ptr<DataCache>`，lambda 按值捕获 shared_ptr + QPointer 守卫
- **kline_chart / time_line_chart / paper_trade** 改 QPointer 守卫 + provider 按值捕获，回调全部走 `guard->`
- **fundProvider_** 的 shared_ptr 移入主线程回调再释放（IO 线程用完、主线程析构，避免跨线程 clearAccessCache）
- 全量 `src/ui` 复扫：`submitIO([this` / `submitWorker([this` **0 处遗留**

### 验证
- 构建零警告；ctest 241/241 全过；待用户 run.bat 稳定性复测

## 2026-08-06 — 修复：关闭时堆损坏（第二轮全局审计）

### 症状（用户复测）
修复第一轮后仍弹 `HEAP CORRUPTION DETECTED: after Normal block (#7718230)`（块号很大，会话后期分配），仍非确定性、关闭时出现。

### 根因（code-reviewer 第二轮全局关机审计）
1. **`TdxProvider::disconnect()` 末尾把 `stopThreads_` 重置为 false**（tdx_provider.cpp:96）：closeEvent 第一次 disconnect 后 stopThreads_=false → 关闭窗口间隙里面板定时器（分时图 10s / 模拟交易 3s）触发异步任务 → `executeCommand` → `ensureConnected()` 派生**不受管理的 detached doConnect 线程** → provider 析构后该线程访问已释放的 this → use-after-free → 块号大（provider 释放后内存被 widget 清理复用，doConnect 写入复用块）
2. **分时图/模拟交易面板的轮询定时器**在关闭窗口可能触发、提交持有裸 provider_ 的异步任务
3. `refreshQuotes()` 无关闭守卫（F5 快捷键在关闭窗口仍可提交任务）

### 修复
- `stopThreads_` 语义改为「运行中」：`disconnect()` 不再重置（注释说明），`connect()` 启动时置 false（保留重连能力）；`ensureConnected()` 加 `if (stopThreads_.load()) return false;` 阻止关闭后触发重连；`refreshQuotes()` 加 `if (stopThreads_.load()) return;`
- `MainWindow::~MainWindow` 先 `ThreadPool::ioPool()->waitForDone()` + `workerPool()->waitForDone()` 排空在途任务，再 `provider_->disconnect()`（事件循环已停止，不会提交新任务，无死锁）
- `TimelineChart` / `PaperTradePanel` 加析构函数显式停止轮询定时器（并停引擎）
- 复用第一轮模式：fundProvider_ 的 shared_ptr 移入主线程回调释放；AKShareProvider::disconnect 不再跨线程调 QNAM clearAccessCache

### 第三轮修复（用户复测：关量化工作台仍崩）
**根因：PatternPanel 漏改**——`startDetect` 的 IO lambda 仍捕获指向成员 `unique_ptr<DataCache>` 的裸 `cache_.get()`（pattern_panel.cpp:99），关 QuantWindow（形态识别面板在飞）时写入已释放 cache → 堆损坏。这也是「关量化工作台必崩」的直接原因。
- PatternPanel `cache_` 改 `shared_ptr<DataCache>` + lambda 按值捕获
- 全量复扫：`src/ui` 无 `std::unique_ptr<DataCache>` 残留、无裸 `cache_.get()` 异步捕获

### 验证
- 构建零警告；ctest 241/241 全过；待用户 run.bat 复测稳定性（打开/操作各面板/关闭不弹错误框）

## 2026-08-06 — P10 第二轮：压力测试接入 / 舆情资讯流 / 多数据源 / 基本面数据

### 背景
P10 第二轮待办 4 项（用户确认全做）：StressTest 接入 AdvisorPanel、ISentimentProvider 真实资讯流、多数据源 MultiProvider、基本面 8 字段填充。实盘交易（需券商账户）留后续。

### Step 1 — StressTest 接入 AdvisorPanel（改 2 文件）
- `onRunClicked` 数据加载范围从用户 start 扩大到 `min(start, 2015-01-01)`（BacktestEngine 按 config 日期过滤，网格回测结果不变）
- `onAllDataFetched` Worker 里用最优参数组跑 `StressTest::run(cfg, defaultWindows())`（2015股灾/2016熔断/2018熊市/2020疫情/2024微盘），替换硬编码 `actx.stressTest = nullopt` → `StrategyAdvisor::detectRisk()` 自动生效（任一窗口回撤>20% → riskWarning + 置信度-0.15）
- 建议区新增 `advStress_` 压力摘要行：`压力测试: 2015-06 股灾 -35.2% / ...`，任一>20% 红字；无窗口数据提示

### Step 2 — 东财新闻 ISentimentProvider（新建 2 + 改 2 + 测试 6 例）
- 新建 `EastMoneyNewsProvider : ISentimentProvider`（intelligence/sentiment/）：调东财 search-api-web JSONP 接口（akshare stock_news_em 同款），静态 `parseNews` 剥离 JSONP 取 `result.cmsArticleWebOld[]`（title/content截断200/mediaName/date前10位）；fetch 用 thread_local QNAM + QEventLoop + 重试3次
- `SentimentPanel` 构造注入 `shared_ptr<ISentimentProvider>`，新增「拉取东财新闻」按钮（IO 池安全异步 + QPointer 守卫），抽 `analyzeItems` 复用逐条打分逻辑；拉取失败优雅回退手动输入
- `QuantWindow` 注入 `make_shared<EastMoneyNewsProvider>()`
- 测试：JSONP 剥离/任意回调名/内容截断/limit/空/畸形

### Step 3 — MultiProvider 多数据源（新建 2 + 改 2 + 测试 6 例）
- 新建 `MultiProvider : IDataProvider`（主源优先，空/失败整体回退备源，不拼接避免价量错配）；`providerName()` = `multi(主源→备源)`
- `provider_factory` 抽 `makeDataProviderByName`，注册 `"akshare"`；`data.provider="multi"` 时按 config `data.multi.primary/fallback`（默认 tdx→tencent）装配
- config/default.json 加 `data.multi`；默认 `"tdx"` 不变（零回归）
- 测试：主源采用/主空回退/主 connect 失败/首选备源/name 拼接/全空不崩
- 注：AKShareProvider batchQuote/intraday 仍为空桩，实时行情建议 tdx/tencent 主源

### Step 4 — 基本面 8 字段填充（新建 2 + 改 3 + 测试 4 例）
- **实连校准发现**：东财 `stock/get` 单只接口在部分网络被掐断返回空，`ulist.np/get`（指定代码批量）明文 HTTP 稳定可用 → 主路径定为 ulist.np；clist 整表按涨跌幅分页会漏股票，弃用
- 新建 `QuoteFundamentals`（Data 层自包含 struct）；`IDataProvider::getQuoteFundamentals` 默认 nullopt；`AKShareProvider` 实现（东财 ulist.np/get，f8换手率/f115市盈静/f20总市值/f21流通值/f38总股本/f39流通股，fltt=2 裸值），换手率(实)=换手率×流通股/总股本，**TTM 市盈 clist 无可靠值 → 显示"—"**
- **关键**：fetch 用 thread_local QNAM（IO 线程可调），不复用成员 QNAM（主线程亲和）
- 校准工具 `tools_fundamental_calib.cpp`：茅台 总市值1.636万亿/总股本12.5亿股/市盈静19.78/换手率0.20% 全部与常识吻合
- `StockKeyDataWidget` 新增 `fundTimer_`(60s 慢刷新) + `requestFundamentals`（安全异步）→ 填充 8 字段（市值/股本 万亿亿/亿股 格式化）；无数据源（如 tdx）优雅显示"—"
- 测试：茅台 payload 解析/部分流通换手率(实)计算/畸形回退/非A股 nullopt

### 验证
- 构建零警告（MSVC /W4）；ctest 225 → **241/241** 全过（Foundation 34/Core 21/Data 78/Engine 56/Intelligence 52）
- 冒烟待用户 run.bat：优化建议出压力摘要、舆情拉东财新闻、config 切 multi/akshare、基本面字段变实值

## 2026-08-06 — 看盘细节修复 + 内盘/外盘数据核查

### 1. 个股关键数据：高/低/开颜色逻辑修复
- **原**：最高/最低/开盘三栏统一跟随现价涨跌（`q.change`）上色，且 `q.change >= 0.0` 把平盘判成红色
- **改**：三栏各自与昨收（零轴）比较 —— 高于昨收红、低于昨收绿、**持平默认黑**（[stock_key_data_widget.cpp](src/ui/widgets/stock_key_data_widget.cpp)）

### 2. 内盘/外盘数据核查（结论：数据正确，非 bug）
用户对比同花顺发现外盘/内盘数值不同（茅台 我们1.78/2.49 vs 同花顺1.55/2.72；太极实业 我们234.99/234.95 vs 同花顺227.14/242.80）。排查结论：
- **解码结构正确**：五档价格合理（记录对齐对）、外盘+内盘=总成交量（精确）、方向归属验证过（茅台 s_vol/b_vol 与全天逐笔 buyorsell 求和吻合，外盘=主动买/内盘=主动卖）
- **根因是行情软件统计口径差异，非本软件缺陷**：600667 当日外部各源外盘 206.6万~263.3万（差 27%），同花顺主界面口径最偏内盘、新浪最偏外盘，各源互不一致；同花顺龙虎榜自身也显示 227.14/226.87（近对半）。我们用的是通达信服务器 s_vol/b_vol（通达信口径），与通达信客户端完全一致
- 新增 `TdxProvider::getDayTransactions(code)`：0x0FC5 分页拉全当日逐笔（start 为从当日末尾倒数偏移），供核查与后续成交明细全貌展示；`getTransactions` 重构复用 `toTicks`
- `tools_depth_probe` 改为可传代码参数的聚焦诊断工具（单票：报价 s_vol/b_vol + 五档 + 全天逐笔方向对照）

### 3. 个股关键数据：两列间隔
- 布局列由 `标签|值|标签|值` 改为 `标签|值|间隔|标签|值`，第 2 列固定 16px 分隔左右两组数据

### 验证
- 构建零警告；数据层测试 68/68 全过

## 2026-08-06 — 修复：堆损坏崩溃（异步 lambda 悬垂 this）

### 症状（用户实测）
- 运行 run.bat 有时打不开软件；有时能打开但**关闭时弹出 MSVC CRT**：
  `HEAP CORRUPTION DETECTED: after Normal block(#12334). wrote to memory after end of heap buffer`
- 崩溃**非确定性**（有时启动即崩、有时关闭时检测），事件日志 APPCRASH P7=c0000374（STATUS_HEAP_CORRUPTION）

### 根因（代码审查确认）
**ThreadPool 异步 lambda 捕获裸 `this` 指针**：连续轮询部件（盘口 2.5s / 个股关键数据 5s / 市场面板 30s / 搜索栏）的 `submitIO([this, code]{ provider_->... })` 在 IO 线程读取 `this->provider_`。若用户在轮询进行中**关闭窗口**，widget 先被销毁、IO 线程后执行 lambda → 读已释放内存中的 `provider_` 得到垃圾指针 → 经垃圾指针调方法 → **随机堆写**，覆盖相邻堆块哨兵 → 释放时 CRT 检测到 "after end of heap buffer"。

### 修复（9 处 + 1 处次要）
统一改为**安全异步模式**：IO 线程**按值捕获 provider**（不读 `this->provider_`），结果投递用 **`QPointer` 守卫**（widget 销毁后 guard 变 null，`QMetaObject::invokeMethod` 安全跳过）：
- `MarketDepthWidget::onPoll`（盘口+成交明细轮询）
- `StockKeyDataWidget::onPoll` + `setStock` 日K异步
- `MarketPanel` 全A股池加载 + `refresh()` 轮询
- `StockSearchBar` 股票池加载
- `PatternPanel::startDetect` + `onAllDataFetched` worker
- `AdvisorPanel::onRunClicked` IO + `onAllDataFetched` worker
- 次要：`SentimentPanel` 舆情表 `setItem` 前先 `insertRow`（避免 QStandardItemModel 内部状态不一致）

### 验证
- 构建零警告；ctest **225/225** 全过；15s 稳定性运行正常
- 二分确认：P9 基线 3 次启动无崩溃，P10 全量有时崩

## 2026-08-06 — P10 第一轮：扩展增强（量化工作台 / 盘口五档 / 形态因子接入）

### 背景
P9 完成后进入 P10「扩展增强」。需求文档三项：实盘交易（券商API）、盘口数据、多数据源。**用户选定范围**：量化独立窗口（AI 三面板）+ 盘口五档 + 形态因子接入选股面板；实盘交易（需真实券商账户）与多数据源留后续轮次。

### 已完成（构建零警告 / ctest 223/223 全过）
1. **盘口五档数据层**：
   - `TdxQuoteRec` 增加 `DepthLevel{price,volume}` + `bids/asks` 各 5 档（[tdx_models.h](src/data/tdx/tdx_models.h)）
   - `decodeQuote` 五档循环由「仅推进」改为「消费并保存」；**实连验证（tools_depth_probe）发现五档价格是相对现价的差分（分）**，绝对价 = priceFen + diff 再 ÷100，量 手→股（[tdx_models.cpp](src/data/tdx/tdx_models.cpp)）；字段消费顺序不变，现有 fixture 回归无影响
   - **验证**：茅台现价 1306.45 → bestBid 1306.45 / bestAsk 1306.46 / spread 0.01，买1=现价、卖1=现价+0.01，买卖档位单调正确
   - **外盘/内盘解析**：`decodeQuote` 保存 s_vol（内盘）/b_vol（外盘）→ `TdxQuoteRec` → `Quote.outerVol/innerVol`；实连验证 外盘17819手+内盘24870手=42689手≈成交量42688手，且内盘>外盘对应下跌行情
5. **个股关键数据窗口** `StockKeyDataWidget`（盘口上方独立 Dock）：20 项（最高/最低/开盘/昨收/成交量/成交额/量比/振幅/涨停价/跌停价/换手率/换手率(实)/外盘/内盘/市盈(静)/市盈(TTM)/总市值/流通值/总股本/流通股）
   - 数据来自 batchQuote + 计算：振幅=(高-低)/昨收、涨停/跌停按板块（创业板/科创板20%其余10%）、量比=今日量/5日均量（异步取 5 根日K）
   - 高/低/开/昨收 跟随涨跌红绿着色
   - **换手率/市盈/市值/股本显示 "—"**：实测 TDX 财务命令 0x0A04（pytdx get_finance_info）本服务器返回 FAILED（不支持），基础数据源待 P10 后续（换数据源或另寻财务接口）
   - `IDataProvider::getMarketDepth()` 非纯虚默认 nullopt（Tencent/Akshare/fake 无需改）；`TdxProvider::getMarketDepth` 单只报价请求 → `st::MarketDepth`（[tick.h](src/foundation/tick.h) 原有结构首次启用）
2. **量化独立窗口 QuantWindow**（主窗口菜单「量化 → 量化工作台」，非模态 WA_DeleteOnClose，重复触发只 raise）——**承载全部量化面板**：3 个 Intelligence 面板 + 原主窗口 quantDock 的选股/参数优化/策略对比/压力测试/模拟交易 + 策略/回测，共 10 个 tab；**内部跨面板信号**（策略/参数优化/优化建议 → 回测面板），双击结果行冒泡 openChart → 主窗口中央图；主窗口只留「市场/盘口/日志」看盘面板：
   - `PatternPanel` 形态识别：复用 `StockSearchBar`（代码/名称/拼音）+ `KLineChart` 联动 + 新 `PatternTableModel`（日期/形态/置信度/方向/说明，方向列红涨绿跌）；IO 拉日K → Worker `PatternRecognizer::detect` → 填表；`detectGen_` 世代守卫防快速切股竞态；双击行 → 主窗口中央图
   - `AdvisorPanel` 优化建议：照 OptimizationPanel 配置区（策略/参数范围/objective/股票池/日期/资金）+ 网格搜索 → 从最优净值曲线派生日收益跑 `MonteCarlo` → `StrategyAdvisor::advise` + `suggestRefinedRanges`；建议区显示 建议参数/置信度/警告(红字)/正文+理由/精化网格；「应用参数」跳回测面板、「用精化网格再优化」回填范围重跑
   - `SentimentPanel` 舆情情绪：纯本地关键词打分（无真实资讯源，顶部注明待接入 ISentimentProvider）；多行标题 → 逐条 analyze + averageScore
3. **PatternFactor 接入选股面板**：`screener_panel.cpp` 的 `candidateFactors_` 追加 `make_shared<PatternFactor>()` 权重 1.0 + `factorDisplayName` 加「形态评分」（选股面板因子勾选区现 12 个因子）
4. **盘口 UI**：`MarketDepthWidget`（QGridLayout+QLabel：标题=**个股名称 + 代码**（如「贵州茅台 600519」）；左上角最新价/涨跌幅，**涨红跌绿平盘默认**；卖5..卖1 → **分隔线** → 买1..买5，每档标价格（价两位/量按手；**卖价绿/买价红**）；**卖盘上下顺序修正**——asks[0]=卖1（价最低）在最下，asks[4]=卖5 在最上）+ **成交明细表**（盘口下方，QTableView 时间/价格/量/方向，量红买绿卖、最新在前；`IDataProvider::getTransactions` 新增默认空 + `TdxProvider` 用 0x0FC5 逐笔实现，最新 limit 条反转）+ 主窗口「盘口」Dock（右区，宽 260-400）；QTimer 2.5s → IO 池 getMarketDepth+batchQuote+getTransactions → 回主线程；showEvent 启动 / hideEvent 停止（dock 收起省网）；`polling_` 防重叠；**切换个股全路径联动**——搜索栏/市场面板/选股面板/量化窗口开图都同步盘口（`MarketPanel::openChart` 信号增加 name 参数，市场窗口进入的个股能识别名称；指数路径不联动）

### 关键决策
- **盘口用单只单独请求**而非 batchQuote 缓存：`executeCommand` 已 mutex 串行，最简、永远新鲜；主面板 5s 轮询与盘口 2.5s 错开。缓存留后续优化
- **PatternFactor 走 UI 层追加**而非改 `defaultFactorSet()`：P9 已批准「调用处接入，引擎无需改动」；UI→Intelligence 是合法依赖方向，零文件迁移、零测试搬迁。若未来要全引擎默认含形态因子，再把 pattern 下移到 st_engine（纯算法，仅依赖 foundation）
- **QuantWindow 非模态 + WA_DeleteOnClose + destroyed→置空**，面板信号（openChart/applyParams）冒泡到主窗口
- **AdvisorPanel 本轮只跑 MonteCarlo 风险**（从最优净值曲线派生，便宜）；StressTest 接入留 P10 第二轮

### 测试（新增 3 用例，220 → 223）
- `test_tdx_protocol.cpp`：`DecodeQuoteFiveLevelDepth`（合成完整五档 payload，断言各档价/量 + price/preClose 不受影响）
- `test_tdx_provider.cpp`：`GetMarketDepth`（FakeTdxTransport + 完整五档 payload，断言 bestBid/bestAsk/spread/买五卖五/量）、`GetMarketDepthEmptyResponseReturnsNull`（空 payload → nullopt）
- UI 面板不自动测，启动冒烟：StockTerminal 5s 存活无崩溃

### 待办（P10 第二轮）
- [ ] StressTest 接入 AdvisorPanel（真实压力窗口回撤警告）
- [ ] ISentimentProvider 接真实资讯流（东方财富/同花顺）
- [ ] 多数据源（AKShare 接入 factory + MultiProvider fallback）
- [ ] 实盘交易（券商API，需真实账户）

## 2026-08-05 — P9 Intelligence 层（形态识别 + 参数优化建议 + 形态因子 + 舆情桩）

### 背景
P9 剩余部分为 Intelligence 层（AI 智能）。TDX 数据源 + UI 图表打磨已完成并推送。本轮新建 `src/intelligence/`（此前为空）的智能服务层。**用户决策**：核心 3 项完整实现（形态识别 / 策略优化建议 / 形态因子），舆情情绪做接口桩（无新闻源）；**本轮不做 UI 面板**（量化功能后续拆独立窗口），只做 C++ 服务层 + 单元测试。

### 约束与选型
- 无 ML 库、无新闻源、**离线**。形态识别用**纯 C++ 规则**（确定性、可单测），复用 `st::indicators::sma`；**不引入 TA_CDL\* 和 pybind11**
- 依赖：`st_intelligence` PUBLIC 链接 `st_engine st_core st_foundation`（st_engine 传递依赖 st_data，DataCache 可用）；UI 本轮不链接

### 已完成（本日，构建零警告 / ctest 218/218 全过）
1. **PatternRecognizer（16 种 K 线形态，纯规则）** `src/intelligence/pattern/`
   - `pattern_types.h`：`PatternType` 枚举（16 种）+ `PatternSignal{type,index,confidence,name,description}` + `PatternDetectResult`
   - `pattern_recognizer`：`detect()` 全量检测 / `detectAt()` 只检最后 3 根；可调阈值 `setMaPeriods(5,20)` / `setVolumeRatio(2.0)` / `setMinBars(40)`
   - 16 形态：十字星(0.6)、锤头/倒锤头/吊颈/流星(0.75)、看涨/看跌吞没(0.75，实体≥2×前实体+0.1)、早晨/黄昏之星(0.8)、红三兵/三只乌鸦(0.8)、金叉/死叉(0.8)、均线多头/空头排列(0.7)、放量突破(0.75，收盘破前10根高+量≥2×均量)
   - 影线类阈值：实体≤35%振幅、影线≥2×实体、对侧影线≤35%振幅；十字星（实体≤10%振幅）不与影线类重复
   - `typeName()` 中文名 + `isBullish/isBearish`（十字星为中性）
2. **StrategyAdvisor（策略参数优化建议）** `src/intelligence/advisor/`
   - `advisor_types.h`：`AdvisorContext{strategyId, results, monteCarlo?, stressTest?, objective, topN}` + `AdvisorSuggestion{recommendedParams, confidence, overfitWarning, riskWarning, text, rationale}`
   - **不重跑回测**：复用 `GridSearchOptimizer::objectiveMinimized` 按 objective 方向选优，直接消费已排序结果
   - 过拟合探测：最优目标值相对其余组合中位值差 >50%；风险探测：蒙特卡洛 probOfLoss>0.4 或 p5<0.85，或任一压力窗口回撤>20%；**网格整体不佳探测**：最大化目标 best≤0 或 MaxDrawdown best>15% → 提示更换策略；置信度 0.85 起，每项警告 -0.15
   - `suggestRefinedRanges()` 围绕推荐参数生成 value±1 精化网格
3. **PatternFactor（智能选股形态因子）** `src/intelligence/screener/`
   - 实现 `IFactor`：`name()="pattern_score"`、`category()=Momentum`、数据不足（<50 根）返回 nullopt
   - score = 50 + 12×(近30根看涨-看跌形态数) + 均线排列±15，clamp [0,100]；接入：`screener.addFactor(make_shared<PatternFactor>(), 1.0)` 引擎无需改动
4. **SentimentAnalyzer（舆情情绪，接口桩）** `src/intelligence/sentiment/`
   - `ISentimentProvider` 纯虚接口（`fetchNews(code, limit)`）——暂无实现，P10 接真实资讯流；`SentimentAnalyzer` 本地关键词表（利好/增长/盈利…积极；利空/亏损/减持…消极）独立打分，`analyze/averageScore/analyzeStock/setKeywordTable`，平滑公式 `tanh 前身 = (pos-neg)/3` clamp [-1,1]，标签阈值 ±0.15
   - 可无 provider 独立工作；注入 provider 后 `analyzeStock()` 聚合

### 测试（新增 46 用例，174 → 220）
- `tests/test_intelligence/`（test_main 复用 test_engine 的 QCoreApplication 入口）：
  - `test_pattern_recognizer.cpp`（20）：合成 K 线精确控制 OHLCV 逐一验证 16 形态 + 吞没置信度加成 + 数据不足返回空 + detectAt 只返回近 3 根 + typeName/isBullish 枚举全覆盖（8 看涨 / 7 看跌 / 1 中性）
  - `test_strategy_advisor.cpp`（14）：按 objective 选优 / Minimized 方向 / 跳过失败结果 / 平台无警告 / 尖峰过拟合警告 / 蒙特卡洛 / 压力测试 / 双警告置信度 / 网格整体不佳 / MaxDrawdown 不佳阈值 / 空结果 / 精化范围 ±1 / objective 中文名
  - `test_pattern_factor.cpp`（5）：上升 >50 / 下降 <50 / 数据不足 nullopt / name+category / 接入 StockScreener 的 run() 含 pattern_score
  - `test_sentiment_analyzer.cpp`（7）：积极 / 消极 / 中性 / 混合平均 / 无 provider nullopt / MockProvider 注入 / 自定义关键词表

### 验证工具（tools_intelligence_demo）
- `tools_intelligence_demo.cpp` → `intelligence_demo`：连 TDX 拉真实日K（失败回退合成序列），跑通 4 模块并打印
- 实测发现 advisor 对**整体亏损网格**仍给 0.85 置信度 → 增加「网格整体不佳」风险信号（最优目标值为负 / MaxDrawdown>15%），置信度 -0.15 并提示更换策略
- 实测输出：茅台 640 根日K → 近 10 条信号全为均线多头排列（升势合理）；pattern_score=100（强多头）；MACross 网格整体 -10.57% → 建议降置信度 0.70 + 「谨慎采用」；舆情 积极/消极/综合 打分正确

### 修复
- **Qt `signals` 宏冲突**：`<QCoreApplication>` 将 `signals` 宏展开为 `public`，`result.signals` 编译报 C2059。`PatternDetectResult::signals` 改名为 `items`（Qt 项目避免用 signals 作标识符；单测不引 Qt 头所以未暴露）

### CMake
- `src/CMakeLists.txt`：新增 `st_intelligence` 静态库（4 个模块 .cpp），PUBLIC 链接 st_engine st_core st_foundation；新增 `intelligence_demo` 工具
- `tests/CMakeLists.txt`：新增 `test_intelligence`（`if(BUILD_WITH_QT)` 内），链接 st_intelligence st_engine st_data

### 关键决策
- **形态识别纯规则**：确定性可单测，符合无 ML 库约束；置信度用简单启发式而非统计模型
- **advisor 不重跑回测**：直接消费 GridSearchOptimizer 输出，避免重复计算
- **ST_intelligence 本轮未接入 UI**：量化面板后续随「量化独立窗口」一起规划

### 待办
- [ ] P10：ISentimentProvider 接真实资讯流（东方财富/同花顺资讯接口）
- [ ] 量化独立窗口：形态识别/优化建议/情绪结果的 UI 展示（不占主窗口）
- [ ] PatternFactor 接入选股面板因子选择器

## 2026-08-05 — 分时图 UI 精修 + 搜索框拼音搜索 & 焦点修复

### 已完成（用户实测迭代，未提交）
1. **分时图面板分隔**：量区/MACD 区像日线图一样**交替底色 + 顶部分隔线**，主图与指标明显分隔
2. **昨收标签重叠**：昨收标签与右下 高/低 标签重叠 → 标签 `clamp(top+32, bottom-46)` 夹在中间空白带
3. **分时主图动态分段轴**：涨/跌**各 6 段**对称等分（`symRange_=max(单侧波动)`，昨收居中虚线 0%），左轴价格、右轴涨跌幅（红涨绿跌）；替换原右上现价/涨跌幅、右下高/低临时标注；新增左轴带宽 56px，xFor/柱宽/十字线改用 mainRect_ 定位
4. **搜索框无拼音**：根因 TDX 股票列表只有代码+名称，`pinyinInitials` 硬编码空串。实现 `foundation/utils/pinyin.h/.cpp`：
   - 内嵌 44435 条 Unicode→拼音表（mozillazg/pinyin-data 生成，`scripts/generate_pinyin.py` 可重生成，去声调/ü→v/取首读音）
   - `pinyinInitials()`（首字母）与 `pinyinFull()`（全拼）
   - **多音字词覆盖**（银行→yh、重庆→cq、西藏→xz、厦门→xm），匹配优先于单字表
   - TdxProvider 填充 `pinyin`/`pinyinInitials`；StockSearchIndex 增加**全拼匹配**通道（第4通道）
5. **搜索框抢焦点**：`Qt::Popup` 弹层显示时抓取全应用鼠标键盘 → 其他控件无法交互。改 `Qt::ToolTip`（不抓取）+ `qApp` 级事件过滤器：点击编辑框/弹层之外关闭弹层**且不拦截事件**，一次点击同时关弹层+交互目标控件
6. **十字线信息框分时量**：信息框「量」原显示累计量 → 改为**对应分钟的分时量**（`volume[i]-volume[i-1]` 增量，与量柱一致，万为单位）
7. **指标标签与指标图分离**：分时量/MACD 图例原直接画在面板内叠加在柱上 → 面板顶部留 **16px 独立图例条带**（`kPaneHeaderH`），标签放条带中，量柱/MACD 柱/0轴在图例条带下方绘制（volToY/macdToY/柱高/0轴 均改用去条带高度）
8. **量显示单位 股→手**：用户对比发现第一根分时量柱 1000.95万 vs 参照软件 10.01万，差 100 倍。诊断实证：0x0FC5 逐笔量原始单位是**手**（全日记合计 37475 手 ≈ 报价 37450 手），getIntraday ×100 转股正确 → 首根柱 10,009,500 股 = 100,095 手。**根因是显示层把量当「万股」显示，而 A 股惯例是「手」**。修复：两个图量显示统一走 `formatVolume()`（股→手自动缩放：≥1亿手→`X.XX亿手`、≥1万手→`X.XX万手`、否则→`X手`），量轴/十字线/图例全部接入；`decodeTransaction` 量注释 股→手 防再误读
9. **指数K线量修正**：上证指数日量字段实测原始单位是**万股**（非个股的「手」）——量 540324900 vs 额 10084亿，额/量=186630，按万股→均价 18.66 元与沪市大盘吻合；用户确认参照软件显示约 5.4 亿手。修复 `decodeKline` 指数分支 `×10000`（万股→股），上证指数日线量 = 540,324,900 手 = 5.40 亿手 ✓
10. **指数分时量修正**：实测指数 0x0FC5 逐笔响应的 volume 字段实为**「成交额/100（百元）」而非成交量**（全日记合计 10,083,823,040 ×100 = 10,083.82亿 = 当日成交额）。getIntraday 指数分支：按分钟累计**成交额**，再用**日K日均价**（量额比）把每分成交额换算成成交量 → 日总量 5.37 亿手（参照 5.4 亿，99.4% 吻合）。另：**指数分时不画均价线**（均价=每股均价与指数点位量纲不符），时间线图按 isIndexCode 跳过
11. **个股开盘价/均价线修正**：实测 0x0FC5 含 09:25 集合竞价记录 `(09:25 p=1350.06 v=235)`，但 getIntraday 过滤 `minsOfDay<09:30` 将其丢弃 → 开盘价错（第1点显示 09:30 收盘而非开盘价）、均价线不含竞价量。修复：竞价计入第 1 分钟（idx clamp ≥0）、第 1 分钟价格用 `a.open`（开盘价）。验证：P0 price=1350.06（=K线开盘）✓、cumVol 含竞价量、avg 为含竞价的 VWAP ✓；另：分时图首点均价 = 开盘价（价格线/均价线同起点）
12. **K线十字信息框固定顶部**：日/周/月线信息框原跟随鼠标纵坐标浮动 → 改为固定在图表顶部（`mainRect_.top()+4`），水平仍跟随鼠标，与分时图一致
13. **分时图实时自动刷新**：用户要求不切换股票/图表也能更新 → `QTimer` 每 10s 静默重拉 getIntraday（loadGen_ 守卫竞态、不闪"加载中"、保留十字线位置）；`isTradingTime()` 判定交易时段（工作日 09:25-15:05）非交易时段跳过；hideEvent 停表、showEvent 恢复
14. **分时图未来分钟直线**：getIntraday 对未来分钟全部 carry-forward → 盘中 10:00~15:00 画成水平直线。修复：只构建到最后一个有成交的分钟（`mins.rbegin()->first`），未来分钟不生成点，图到当前时刻为止
15. **分时量柱颜色**：原按「价格 vs 昨收」着色 → 全天在昨收下方时量柱全绿，与成熟软件不符。改为按**分钟自身涨跌**（对比前一分钟价格）着色，首分钟对比昨收 → 下跌分钟绿、反弹分钟红，红绿交替

### 量单位统一（本次确认）
- 个股：0x0FC5 逐笔=手、K线字段=手；指数：0x0FC5=成交额/100、K线字段=万股 → 数据层统一换算成**股**，显示层 `formatVolume()` 统一转**手**并自动缩放（亿手/万手/手）

### GUI 复验（用户实测）
- 拼音搜索、弹层不抢焦点、分时 6+6 对称轴：**复验通过**

### 测试
- 新增 `tests/test_foundation/test_pinyin.cpp`（7 用例：首字母/全拼/词覆盖/ü→v/ASCII/索引名）
- ctest **174/174** 通过，构建零警告

### 关键决策
- 拼音用**首字母 + 全拼双通道**：首字母（gzmt）为默认习惯，全拼（guzhoumaotai）作补充
- 多音字仅覆盖**股票名称高频且无歧义**的词（银行/重庆/西藏/厦门）；重工/重科等 `重=zhòng` 与 `重庆` 冲突，暂不全局覆盖
- 拼音表 934KB 由脚本生成后**提交生成物**（仓库内置，离线可用），源数据 `scripts/pinyin_src.txt` + 生成脚本一并留存保证可重生成

### 待办
- [ ] GUI 复验：拼音搜索、弹层不抢焦点、分时 6+6 对称轴
- [ ] 多音字覆盖词表按需扩充（如 重 的上下文处理）

## 2026-08-04 — P9 TDX 通达信数据源（协议层 + Provider + 接口重构）

### 背景
用户要求参考 [injoyai/tdx](https://github.com/injoyai/tdx)（Go 通达信协议客户端）把数据源改为**直连通达信主站**（TCP :7709），完全替换腾讯数据源。已确认 C++ 直连二进制协议、覆盖全部 UI 消费（日/周/月K线+分时+实时盘口+股票列表）。

### 已完成（提交 c4d48a8，+2005 行）
- [x] **协议层** `src/data/tdx/`：TdxSocket（WinSock2 同步 TCP + TdxTransport 虚接口供测试注入）、tdx_protocol（帧编解码：请求 0x0C 12B 头 / 响应 0xB1CB7400 16B 头 + zlib 解压、变长整数、量解码 getVolume2、命令构造、tdxMarket/klineCategory）、tdx_models（K线差分价格/报价分单位/分时/除权除息/股票列表解码）
- [x] **TdxProvider**：单连接 mutex 串行 + 断线重连 + 8 服务器 failover（已更新为可连通 IP）；getBars 日/周/月**前复权**（gbbq 缓存 + 仿射变换 P_adj=(P-c)/m）；batchQuote 分块 60；getStockList 分页 1000；subscribeQuote pollThread + EventBus
- [x] **接口重构**：IDataProvider 加 batchQuote/getIntraday/refreshQuotes/providerName 纯虚；12 个 UI 类 `TencentProvider*`→`IDataProvider*`；main_window `makeDataProvider()`；provider_factory 读 config `data.provider`（默认 tdx）；AKShare 补桩；cn_encoding 抽取 GBK 工具；vcpkg 加 zlib
- [x] **实连验证**（tools_tdx_live）：登录 ✓、日/周/月K线前复权 ✓（茅台 2026-08-04 收 1328.36 与报价一致）、报价 ✓（量 3745000/额 50 亿）、列表解析 ✓（每页 1000）

### Step 9c 集成问题修复（用户实测反馈，本日）
用户启动软件实测发现 4 个问题，全部定位根因并修复：
1. **涨幅/跌幅榜 + 市场宽度错误**：根因 `decodeQuote` 只读前 11 个字段（到 amount）就跳下一条 → 多代码批量请求（MarketPanel 精选池 129 只分 3 块）记录错位，6 只只解析对 1 只。修复：按 pytdx 完整字段序列消费整条记录（s_vol/b_vol/rev2-3/**五档×20**/rev4(2B)/rev5-8/rev9+active2(4B)）。实连验证 6 只（茅台/平安/招行/平安银行/宁德/五粮液）价格全对。fixture `quote_6codes.bin` + `DecodeQuoteBatchFromFixture` 回归锁定
2. **指数K线错乱**：根因 指数 K线记录比个股**多 4 字节（涨跌家数）**，decodeKline 不跳 → 第二根起错位（日期 1899 垃圾）。修复：`decodeKline(payload, cat, isIndex)` + `isIndexCode`（SH `000xxx` / SZ `399xxx`），fetchBarsRaw 传入。实连验证上证指数 5 根（2026-07-29~08-04 开~3823/收~3828 合理）。fixture `kline_000001_day.bin` + `DecodeKlineIndexFromFixture`/`IsIndexCode` 回归
3. **搜索框每输入一个字符卡住（需切屏恢复）**：根因 `Qt::Popup` 弹层显示时**抢占键盘焦点** → 下个字符输入被弹层吞掉（QListWidget type-ahead 导航，表现为"往下选择"）；切屏（弹层关闭）才恢复。修复：弹层加 `WA_ShowWithoutActivating`（第一版）+ **popup 上装事件过滤器，导航键本地处理、可打印字符转发给 edit 继续输入**（第二版，`QCoreApplication::sendEvent(edit_, key)`）。GUI 需人工复验

### Step 9c 第二轮修复（用户复测反馈，本日）
1. **搜索框仍卡**（输入"000001"第二个0会往下选择）：WA_ShowWithoutActivating 不足以阻止 Qt::Popup 抢键盘 → 在 popup 装事件过滤器转发可打印字符给 edit
2. **涨幅/跌幅榜股票数量少**：原用精选 129 只。改为**全 A 股池**（getStockList 过滤可交易前缀 SH 600/601/603/605/688、SZ 000/001/002/003/300/301，~5000 只），异步加载完成后立即刷新；刷新间隔 10s→30s（全池拉取较重）
3. **换手率无数据**：实测 TDX 0x053E 报价记录**不含换手率**（解码尾部字段 rev4=2006/rev5-8=0/rev9=-3/active2=4197 无候选）；需流通股本（另一命令逐股贵）→ 换手率列显示 "—" 而非误导的 0.00%
4. **服务器报价批上限 = 80 只**（实测 200 只请求返回 80）→ quoteChunk_ 60→80
- 全量构建零警告；ctest **165/165**；tdx_live 回归正常

### Step 9c 第三轮修复（用户复测，跌幅榜 -100% 股）
- **症状**：跌幅榜出现一批 -100% 股票
- **根因**（`tools_tdx_market_probe` 全 A 股池 5209 只实测）：TDX 对**停牌股返回 price=0.00**（6 只：SZ000838/002214/300246/300311/300333/300862），`change=(0-preClose)/preClose` 误算成 -100%
- **修复**：MarketPanel 排名与市场宽度**排除 `lastPrice<=0 || preClose<=0`**（停牌/无行情不参与排序与涨跌家数统计）
- 新增 `tools_tdx_market_probe` 市场异常探测工具；ctest 165/165

### Step 10 单测 + 收尾（本日）
- **分时 0x051D 判定为服务器非标准变体，降级保留**：穷举 2/3/4 字段 × 偏移 × 累积/绝对全失败；pytdx `get_minute_time_data` 发相同请求收相同 1268B 响应，官方解析器同样产出垃圾（0.01→2623）——该服务器响应头嵌入 market+code（`[4]=01 [5:11]=600519`），标准解析器全部失配。quote 前导块已识别（price/last/open/high/low 差分全命中基准）。fixture `minute_600519.bin` + 分析脚本保留 `tests/fixtures/tdx/`，findings 写入 docs/tdx-protocol.md
- **transportFactory_ 注入**：doConnect 改用工厂创建传输（默认 TdxSocket），FakeTdxTransport 全链路可测
- **线程可中断**：pollLoop/heartbeatLoop 固定 sleep → 条件变量 wait_for，disconnect 立即唤醒（修复生产 5s/30s 关闭延迟 + 测试挂死）
- **真 bug 修复**：
  1. `loadStockList` `uint16_t start` + `maxStocks=100000`（> uint16 上限）→ `start<maxStocks` 恒真 + `start+=1000` 溢出回绕 → **无限循环**。改 uint32 + `start<=65535` 防回绕
  2. `decodeCodeList` 名称字段固定 8B，短名含 null 填充 → 未修剪导致名称尾部空字符。加尾部 null 修剪
- **单测**：`test_tdx_protocol.cpp`（编码/请求构造/响应压缩与未压缩/变长往返/时间日与分钟/量/Count/CodeList 市场传入+GBK+normalize/K线合成差分/报价合成 + fixture 驱动）与 `test_tdx_provider.cpp`（FakeTdxTransport：getBars 快乐路径/断线重连/batchQuote 分块 61→2 次/getStockList 分页 2500→3 页/qfqAdjust 10送10 前复权 ÷2/connect failover/订阅退订去重+poll 请求体观察）共 **26 用例**
- 全量构建零警告；ctest **162/162**（136 + 26）；tdx_live 回归正常（分时降级属预期）

### Step 9a 集成排查：getStockList 列表 bug（本日修复）
- **症状**：`tools_tdx_live` 显示 SH 列表 = 0 只 → 搜索栏/索引依赖 getStockList 会全空
- **根因**：`decodeCodeList` 把记录**首字节**（代码首字符，如 '9'=0x39）当作市场 → `marketFromByte(57)=Unknown` → 全部被 `isValid()` 过滤。实测记录内**不含市场字段**（市场由请求隐含），布局 `[0:6]代码 | [6:8]=0x0064 | [8:16]名称GBK(8B) | [16:29]其他`
- **修复**：`decodeCodeList(payload, Market)` 增加市场参数由调用方传入；`loadStockList` 传请求市场；列表上限 20000 硬截断改为按 Count 真实总数（安全上限 100000）
- **验证**：tdx_diag 实连对比 count mkt=1→27642 / mkt=0→23906；code mkt=1→"999999 上证指数"、mkt=0→"395001 主板A股"；tdx_live SH 列表 **27642 只 + 找到茅台: 贵州茅台**
- 新增 `docs/tdx-protocol.md` 协议笔记（帧格式/命令/变长/量解码/K线/报价/列表布局/复权/服务器列表，全部实连抓包校准）

### Step 8 构建验证（本日收尾）
- 脚本替换后首次编译，修复 2 处遗漏：
  1. `main_window.cpp` `std::makeDataProvider()` — 脚本保留 `std::` 前缀，改为 `makeDataProvider()`（namespace st 自由函数）
  2. `tools_chart_render.cpp` 用旧签名构造 `KLineChart(TencentProvider*)`/`TimelineChart(TencentProvider*)` — 改 `makeDataProvider()` + `provider.get()`
- 状态栏/About 数据源文案 `"腾讯行情"` → `provider_->providerName()` 动态显示
- `cmake --build --preset with-qt` 零错误零警告；`ctest` 136/136 全绿（含 test_tencent_provider 回归基线）

### 已知降级（Step 10 待办）
- **分时 0x051D 格式待校准**：实测数据起点 [13]（非参考实现 [6]），差分累积前 2 条对后续错位 → `getIntraday` 返回 nullopt（分时图显示"无数据"，不显示错误数据）。备选：0x0FC5 分时成交明细聚合
- 股票列表全量 28 页（27642 只）拉取较慢，需优化

### 测试
- 136/136（无新增单测，协议层靠 tools_tdx_live 实连验证；test_tdx_protocol/test_tdx_provider 留 Step 10）

## 2026-08-03 — P7 优化崩溃修复 + 回测性能优化

### 背景
用户测试参数优化面板时应用崩溃（WER: `ucrtbased.dll` + `0x80000003` = Debug CRT/STL 断言断点）。

### 根因（两个叠加问题）
1. **BacktestEngine::getPortfolio 用函数级 static Portfolio**：GridSearch 并行跑多个 BacktestEngine 时，多线程同时写同一个 static → 数据竞争/堆损坏 → 策略遍历 positions 时撞上对方释放 → STL 断言崩溃。回测面板只跑单引擎所以从未触发。
2. **BacktestEngine 每 bar 整段拷贝历史**（`BarSeries series(hist)` O(n²)）：网格搜索 90 组合 × 6 股从 38s（单线程）到 123s（8 线程 Debug 堆锁竞争）——优化"假死"。

### 修复
- [x] `cachedPortfolio_` 实例成员替代函数级 static（每引擎独立，线程隔离）
- [x] **BarSeries 重构**：内部改 `shared_ptr<vector<Bar>>` 存储 + 新增 `append()`；backtest/paper_trade 热循环从"每 bar 整段拷贝"变 O(1) 追加
- [x] backtest_engine：删除死代码 `filteredBarsByCode`/`currentIndexByCode`，改 `map<StockCode, BarSeries> seriesByCode` 增量构建
- [x] paper_trade：`history_` 改 `map<string, BarSeries>` + `seedHistory` 按值移动
- [x] 优化面板：Debug 下 `parallelLanes=2`（实测 Debug CRT 堆全局锁使 8 线程 = 单线程 2.3 倍耗时），Release 用 maxThreadCount

### 性能对比（6 股 × 90 组合）
| lanes | 修复前 | 修复后 |
|---|---|---|
| 1 | 38s | 6.0s |
| 2 | - | 5.6s |
| 8 | 123s | 14.0s |

### 排查过程
- WER 拿到崩溃签名（ucrtbased.dll + 0x80000003 = Debug 断言断点）
- 无头复现工具发现单线程通过、并行卡死 → 定位 BarSeries O(n²) 拷贝
- gtest 全崩但独立 exe 通过 → 二分测试文件定位为**陈旧对象 ABI 不匹配**（bar.h 变更后部分测试对象未重编），强制重编后 135 全绿
- 新增回归测试 `ConcurrentEnginesPortfolioSafe`（4 线程并发引擎 + 高频 getPortfolio）
- 测试 135 → 136

## 2026-08-03 — P7 UI 量化面板完成

### 背景
P6 完成（117 tests）后搭建量化面板体系。探索确认：StockScreener(11因子)/PaperTradeEngine/BacktestEngine 已就绪，`optimizer/`、`analyzer/` 目录为空，`screener_panel` 是空桩。发现 **Performance 的 winRate/profitFactor/totalTrades/winningTrades/totalPnl 字段从未被填充**（性能展示和优化目标函数都依赖它）。

### 已完成
- [x] **Engine 修复与新增**
  - 修复 `PerformanceCalculator::computeTradeStats`（FIFO 逐股配对，买成本=amount+totalFee、卖回款=amount-totalFee）→ BacktestEngine::run 填充 winRate/profitFactor/totalTrades/winningTrades/totalPnl
  - `GridSearchOptimizer`（engine/optimizer/）：参数笛卡尔积 + std::async 并行回测（共享只读 DataCache 不重复 IO）+ 5 目标函数（总收益/夏普/最大回撤/卡玛/盈亏比）排序
  - `StrategyComparator`（engine/analyzer/）：多策略同数据范围同时回测，按总收益降序
  - `StressTest`（engine/analyzer/）：5 预设极端窗口（2015股灾/2016熔断/2018熊市/2020疫情/2024微盘股）+ 全期基线对比
  - `MonteCarlo`（engine/analyzer/）：日收益有放回重采样 N 次 → P5/P50/P95 + 亏损概率
  - 修复 `PaperTradeEngine`：onQuote 现在累积 per-code 历史并注入 ctx.history（此前趋势策略因 `!ctx.history` 永不交易）；新增 `seedHistory` 播种历史；修复 `placeOrder` 忽略 amount 导致 `buyByAmount` 不成交
- [x] **UI 面板**（一个"量化"QDockWidget 内嵌 5 标签页，tabify 到右侧回测 dock）
  - **选股面板**：11 因子勾选 + 精选129池 + topN/回看/截止 → StockScreener → 结果表（排名/代码/名称/总分/因子明细），双击开图
  - **模拟交易面板**：单股票 + 策略参数 + 资金/滑点 → QTimer 3s 轮询 batchQuote 驱动 PaperTradeEngine → 现金/市值/总资产/总盈亏/持仓 + 成交表 + 日志
  - **参数优化面板**：两参数 from/to/step + 目标函数 → GridSearch → 结果表按目标排序，单击行应用参数到回测面板
  - **策略对比面板**：6 策略预设同时回测 + 多序列净值叠加（EquityCurveWidget 扩展）+ 蒙特卡洛置信区间
  - **压力测试面板**：5 压力场景 + 基线对比 + 当前窗口净值曲线/指标（最大回撤红色高亮）
- [x] **UI 基础**：EquityCurveWidget 多序列（setSeries + setData 兼容）、ScreenResultModel/GridSearchTableModel/ComparisonTableModel
- [x] **MainWindow**：quantDock(5 tab) + 选股双击开图 + 优化点行应用到回测

### 测试
- **Total: 135 tests, 0 failed**（+18）
  - test_performance +4: computeTradeStats 单赢/赢亏/空/FIFO部分
  - test_backtest_engine +1: TradeStatsFilled（买入卖出→交易统计填充）
  - test_grid_search +6: 组合生成/排序/目标映射/缓存复用/并行确定性
  - test_analyzer +6: StressTest窗口+基线/Comparator降序/MonteCarlo恒定+分布+空
  - test_paper_trade +1: TrendStrategyTradesWithSeededHistory（播种历史+金叉买入）

### 排查记录
- **PaperTradeEngine 趋势策略永不交易**：onQuote 未设置 ctx.history，MACross/Turtle 首行 `!ctx.history` return → 累积历史 + seedHistory
- **buyByAmount 不成交**：placeOrder lambda 忽略 amount 参数 → 按 lastPrice_ 换算股数
- **test_engine.exe 0xc0000135**（DLL 找不到）：构建 shell 缺 Qt bin PATH（build.bat 有 `set PATH=D:\Qt\...\bin`）
- **C4099 预置警告**：backtest_panel.h `struct StockCode;` 前向声明与定义 `class StockCode` 冲突 → 改 class
- **C4996 预置警告**：main_window.cpp QMenu::addAction 弃用重载 → 换 text,shortcut,obj,slot 顺序

## 2026-08-03 — P6 UI 图表核心完成

### 背景
P5 完成（103 tests）后搭建完整图表体系。实测锁定腾讯三个数据接口格式（日/周/月 fqkline、分钟 mkline、分时 minute/query），发现 getBars 周/月是坏的（返回日线）。

### 已完成
- [x] **Data 层**
  - indicators 自研（foundation/utils，纯函数可单测）：sma/ema/macd/boll/rsi（Wilder）
  - `parseMinuteTime` 解析 "yyyyMMddHHmm"
  - K线通用化：`fetchKlineBars`/`parseFqKline`（日/周/月参数化 + qfq{day|week|month} 键），修复周/月返回日线 bug
  - 分钟 K 线：`fetchMinuteBars`/`parseMinuteKline`（m5/m15/m30/m60，域 ifzq）
  - 分时：`getIntraday`/`parseIntraday`（IntradayData{date,preClose,points}，量手→股）
  - `Quote::turnover` 换手率（时间戳锚定 t+8）
  - getBars 分发: 日/周/月/季/年 + 分钟周期；start==epoch 拉最近 640 根
- [x] **KLineChart**（QPainter 自绘核心）
  - 布局: 主图55% + 量15% + MACD15% + RSI15% + 时间轴
  - 蜡烛红涨绿跌 + 量柱 + MA5/10/20/60 + BOLL + MACD(DIF/DEA/柱) + RSI(6/12/24) 副图
  - 十字光标 + OHLC 浮框（时间/高低开收/涨跌幅/量/MA）、滚轮缩放（20-800 锚定光标）、左键拖拽平移
  - 异步加载 loadGen_ 竞态守卫；周期切换（分时/日/周/月）
- [x] **TimelineChart 分时图**：240 分钟 X 轴（09:30-11:30/13:00-15:00 午休虚线）、昨收线、价格线+均价线（cumAmt/cumVol）、量柱红涨绿跌
- [x] **CentralChartWidget**：分时↔K线 QStackedWidget + 周期栏；MainWindow 中央区 welcome↔图表，搜索/指数/榜单双击→开图
- [x] **BacktestPanel**：选股(精选129多选)/策略/参数/日期/资金 → IO池拉数→Worker池回测 → 绩效指标网格(12项红涨绿跌)+净值曲线+成交明细表
- [x] **StrategyPanel**：策略模板库(双均线/海龟)+参数编辑 → 应用到回测
- [x] **MarketPanel**：涨幅榜/跌幅榜/市场宽度（batchQuote 实时 129 只，QTimer 10s 刷新，双击开图）

### 测试
- **Total: 117 tests, 0 failed**（+14: indicators 9 + tencent 解析 5）
  - test_foundation +9: SMA/EMA/MACD/RSI(全涨100全跌0/Wilder经典)/BOLL/parseMinuteTime
  - test_data +5: parseFqKline 周/月键、parseMinuteKline 时间列序手→股、parseIntraday 量额换算+qt昨收、turnover

### 实测验证
- `kline_test`：日线640/周线640(跨12年)/月线299(跨25年)/5分钟320/60分钟320/分时267点，周月聚合正确
- `chart_render`：离屏渲染 K线图+分时图为 PNG + 像素分析（红蜡烛/绿蜡烛/MA黄线/均价橙线/价格蓝线全部命中）
- 应用运行 10s 无崩溃，市场面板实时刷新正常

### 排查记录
- **Bar 无 preClose**：涨跌幅改用前一根收盘计算
- **ema 前导 NaN**（MACD dea=ema(dif)）：定位首个连续有限窗口作种子
- **parseIntraday date 嵌套**：date 在 stock["data"]["date"] 非 stock["date"]
- **QPointF 花括号窄化**：MSVC /permissive- 拒绝 int→qreal
- **QToolButton::setFlat 移除**（Qt6）→ setAutoRaise
- **周/月线修复验证**：周线跨12年、月线跨25年（非日线）

### 下一步 → P7 UI 量化面板
选股面板、模拟交易面板、参数优化面板、策略对比面板、压力测试面板

## 2026-08-03 — P5 UI 框架完成

### 背景
P4 完成后（87 tests 零失败）搭建 Qt UI 框架。探索确认 UI 层 13 个类全为空桩，且发现 **4 个必须前置修复的底层缺陷**：`LogManager::logMessage` 从未 emit、st_ui AUTOMOC 陈旧（12 类无 moc）、腾讯行情返回 GBK 编码、`toPinyinInitials` 对中文是占位符。

### 已完成
- [x] **Data 层增强（P5 前置）**
  - `fetch()` 改 thread_local QNetworkAccessManager → 任意线程安全调用（IO 池加载股票列表不卡 10s）
  - `parseQuoteRecord` 完整解析行情（实测字段索引 + GBK→UTF-8 转码 + 名称规范化去全角/空格）
  - **时间戳锚定解析**：股票/指数字段布局不同（股票时间@30，指数@32），用 14 位时间戳字段定位后相对取值
  - `batchQuote` / `parseQuotes` / `parseQuoteName` / `toTencentCode` 公共接口
  - `curated_stocks.h` 内置精选股票池 **129 只**（沪66+深63，代码/名称/拼音硬编码），腾讯实时名称覆盖（离线回退）
    - 已用 live 数据校验：0 错误代码；更新 601211 国泰海通（2025 国泰君安+海通合并）
  - **QuotePoller** 实时轮询：主线程异步 QNAM + QTimer（5s）+ `pollInFlight_` 防堆积 + 注入式测试
  - `subscribeQuote`/`unsubscribeQuote`/`refreshQuotes` 真实实现（原为空）
- [x] **UI 类名重命名**（9 类统一 PascalCase）+ 全 .cpp 加 `#include "moc_X.cpp"`（moc 1→26 个）
- [x] **主题系统**：ThemeManager + dark/light.qss（.qrc 内嵌）+ ConfigManager 持久化 `ui.theme`
- [x] **日志面板**：LogManager `log()` 补 emit `logMessage`（fmt::format + 主线程 Queued 投递）；LogPanel 级别过滤/自动滚动/清空/有界缓冲
- [x] **顶部指数条**：上证/深证/创业板/科创50 实时行情，红涨绿跌，可点击
- [x] **全局搜索栏**：代码/名称/拼音首字母搜索，自定义 QListWidget 弹层 + 防抖 + 键盘导航，IO 池异步加载 129 只
- [x] **快捷键系统**：ShortcutManager + ConfigManager 持久化映射（Ctrl+Space/Ctrl+L/F5/Ctrl+,）+ 偏好设置对话框
- [x] **MainWindow 组装**：QDockWidget 布局（左市场/右策略+回测/底日志）+ 菜单 + 工具栏 + 状态栏 + QSettings 窗口/布局持久化 + 主题切换

### 测试
- **Total: 103 tests, 0 failed**（+16 新测试）
  - test_data +13: TencentQuoteTest 8（字段解析/GBK/规范化/批量/指数市场前缀）、CuratedStocksTest 2、QuotePollerTest 3
  - test_core +3: LogManagerTest（logMessage 信号投递）

### 实测验证
- `curated_check`：129 只精选股票池对比腾讯实时名称，0 错误代码
- `quote_test`：QuotePoller 真实异步链路，4 大指数全部正确（上证 3832.26 +0.72% / 深成 13578.9 +2.21% / 创业 3343.96 +3.06% / 科创 1635.96 +2.99%）
- 应用启动 8s 无崩溃；`%APPDATA%\StockTerminal\config\default.json` 生成 `ui.theme`/`ui.shortcuts.*`；ini 持久化窗口几何+dock 布局

### 排查记录
- **QToolButton::setFlat 移除**（Qt6）→ 改用 setAutoRaise(true)
- **`class QWidget;` 误放 namespace st 内** → 遮蔽全局 ::QWidget，导致基类解析错乱 → 移到全局作用域
- **指数/股票字段布局不同**：绝对索引硬编码解析指数会错 → 时间戳锚定相对取值
- **指数市场识别错误**：parseQuoteBatch 用代码自动检测市场，sh000001 被误判 SZ → 从 `v_sh000001` 前缀解析市场
- **start() 立即刷新只拉到 1 只**：订阅逐个调用 → singleShot(0) 延迟到本轮订阅完成

### 下一步 → P6 UI 图表核心
K线图（9 层渲染系统）+ 分时图 + 回测面板 + 策略面板 + 市场全景面板

## 2026-08-02 — P4 验证闭环 + 腾讯数据源完成

### 背景
P4 目标用真实 A 股数据验证回测闭环。发现**东财数据源（AKShare）在此机器网络不稳定**（DNS 返回不可达 IPv6，VPN 干扰）。经实测腾讯行情接口稳定（连续 3 次 200）。

### 已完成
- [x] **TencentProvider** — 腾讯数据源（主用，稳定）
  - 日K线（前复权）：web.ifzq.gtimg.cn 接口
  - 实时行情/批量行情：qt.gtimg.cn
  - 独立 QNetworkAccessManager + NoProxy（不影响全局 VPN）
  - 自动重试（3次）+ 超时 10s
  - 腾讯 K线格式解析：`[日期,开,收,高,低,量]`（与东财字段顺序不同）
- [x] **AKShareProvider 保留**（东财，备选）
- [x] **策略模板库**（engine/strategy/templates/）
  - ma_cross_strategy: 双均线（MA5/20 金叉死叉）
  - turtle_strategy: 海龟（唐奇安通道突破）
- [x] **端到端回测验证**（真实茅台数据 484 根 2023-2024）

### 端到端验证结果
| 策略 | 总收益 | 最大回撤 | 夏普 | 交易数 |
|------|--------|---------|------|--------|
| 双均线 MA5/20 | -15.8% | 33.9% | -0.31 | 34 |
| 海龟策略 | **+45.1%** | 15.3% | 0.88 | 57 |

回测引擎验证通过：数据拉取 → 缓存 → 策略驱动 → 撮合 → 绩效计算全链路正确。

### 排查记录
- **东财数据源不稳定**：DNS 返回 IPv6 不可达，curl/httplib 时通时不通 → 改腾讯数据源
- **腾讯 K线价格为字符串**：nlohmann 解析需转 double
- **BacktestEngine ctx.history 未设置**：策略无法计算指标 → 修复注入历史序列
- **前视偏差**：history 需为"到当前为止"的序列，不能含未来 bar → 累计注入
- **BarSeries::lookback(0) 越界**：MA 策略 sma 用 lookback(0)，修复为 lookback(1..n)
- **trade.time 为 1970**：下单时间未设置 → currentTime_ 追踪

### 下一步 → P5 UI 框架
主窗口 QDockWidget 布局 + 全局搜索 + 顶部指数条 + 日志面板

## 2026-08-02 — P3 Engine 扩展引擎完成

### 已完成
- [x] **TA-Lib 接入** — vcpkg 安装 talib 0.7.1（一次通过）
  - CMake 链接 `talib::talib`，头文件 `ta-lib/ta_libc.h`
- [x] **StockScreener 选股引擎**
  - IFactor 因子抽象接口（name/category/calculate/toScore）
  - 核心因子集 11 个：ROC/RSI/MACD/波动率/ATR/最大回撤/均线排列/ADX/量比/换手/OBV
  - ConditionFilter 条件筛选
  - Ranker 加权排名（总分归一化）
  - StockScreener 主类：加载日线 → 算因子 → 筛选 → 排名
- [x] **MarketEngine 基础版**
  - MarketScanner 涨幅榜/跌幅榜/换手率
  - MarketBreadth 市场宽度（涨跌家数/ADL/新高新低）
  - MarketEngine 聚合主类
- [x] **Fundamental 预留**
  - FinancialReport / CompanyProfile 数据结构
  - IFundamentalProvider 抽象接口（独立于行情）

### 测试
- **Total: 87 tests, 0 failed**（+19 新测试）
  - FactorTest: 6 tests（指标正确性/数据不足/分数映射）
  - ScreenerTest: 6 tests（排名/筛选/选股流程）
  - MarketScannerTest: 4 tests（涨跌幅/排序/TopN）
  - MarketBreadthTest: 2 tests

### 排查记录
- **BarSeries::lookback bug**：`lookback(1)` 返回当前 bar 而非前一根。修复为 `bars_[size - n - 1]`（n=1 前一根）。

### 下一步 → P4 验证闭环
双均线+海龟策略，回测跑通 + 模拟交易验证

## 2026-08-02 — P2 Engine 核心引擎完成

### 已完成
- [x] **Strategy 基类** — IStrategy 完整接口
  - 生命周期: initialize/onStart/onStop
  - 核心入口: onBar, 可选 onTrade
  - 下单 API: buy/sell/buyByAmount/sellAll (引擎注入交易回调)
  - 查询 API: portfolio/currentCode
- [x] **FeeCalculator** — 全参数可配置费用计算
  - 佣金(含最低)、印花税、过户费、经手费、证管费、自定义规则
  - 内置模板: A股/低佣金/ETF/港股
  - FeeBreakdown 费用明细
- [x] **OrderMatcher** — 订单撮合器
  - 市价单/限价单, 资金/持仓限制
  - 部分成交/全部成交/未触发
- [x] **Performance** — 绩效统计器
  - 核心: 总收益/年化/最大回撤/夏普/胜率
  - 进阶: 卡玛/波动率/索提诺/Alpha/Beta
- [x] **BacktestEngine** — 事件驱动回测引擎
  - 全局时间轴对齐 → 逐Bar驱动策略 → 下一Bar开盘价撮合
  - 内部 Account 管理资金/持仓/费用
  - 净值曲线快照 → 绩效计算
- [x] **PaperTradeEngine** — 模拟交易引擎
  - 实时行情驱动, 即时成交+滑点
  - 与回测共用 IStrategy 接口

### 测试
- **Total: 68 tests, 0 failed**（+18 新测试）
  - FeeCalculatorTest: 7 tests
  - PerformanceTest: 6 tests
  - BacktestEngineTest: 2 tests (买入持有上涨+无数据报错)
  - PaperTradeEngineTest: 3 tests

### 排查记录
- **空指针崩溃**：StrategyContext.portfolio 未设置导致策略解引用空指针（SEH）
- **getCurrentCode 静态空码**：下单 code 为空导致撮合不匹配，改由 currentCode_ 成员追踪
- **手续费断言**：测试预期未含全部费用项，修正为完整费用

### 下一步 → P3 Engine 扩展引擎
StockScreener + MarketEngine + Fundamental

## 2026-08-02 — 安全设计：敏感凭证保护

### 背景
软件将开源公开 + 纯本地单机分享给他人使用，需保护数据源 token、券商凭证、AI key、数据库口令。

### 已完成
- [x] **CredentialStore** — Windows DPAPI 加密凭证存储
  - CryptProtectData/CryptUnprotectData 加密整个 secret JSON → Base64 → 落盘
  - 绑定当前用户，跨用户/磁盘窃取无法解密
  - 非 Windows 预留明文占位（跨平台）
- [x] **AppPaths** — 用户目录隔离
  - %APPDATA%\StockTerminal\{config,data,secrets,logs}
  - 程序目录保持只读，多用户凭证隔离
- [x] **docs/security.md** — 威胁模型、零遥测声明、token 轮换实践
- [x] ConfigManager 保持非敏感配置，敏感字段走 CredentialStore

### 测试
- **Total: 50 tests, 0 failed**（+11 新测试）
  - CredentialStoreTest: 7 tests（含密文检查）
  - AppPathsTest: 4 tests

### 关键决策
- **DPAPI 而非主密码**：零打扰、绑定用户、无需自存密钥；对木马场景靠 token 轮换补足
- **Base64 存储**：DPAPI 输出二进制，JSON 要求 UTF-8，先 Base64 再存

### 下一步 → P2 Engine 核心引擎
Strategy 基类 + BacktestEngine + PaperTradeEngine + FeeCalculator + Performance

## 2026-08-02 — P1 Data 层完成

### 已完成
- [x] Foundation 补充 `StockInfo` 类型（股票基本信息）
- [x] `IDataProvider` 抽象接口完善（connect/getStockInfo/getStockList/getBars/订阅行情）
- [x] **DataRepository** — SQLite 存储 (Qt6::Sql)
  - stocks 表：股票信息
  - daily_bars 表：日线 K 线（OHLCV + turnover）
  - minute_bars 表：分钟 K 线
  - data_sync_log 表：同步状态
- [x] **AKShareProvider** — 东财 HTTP API 接入 (cpp-httplib)
  - 股票列表、日线 K 线（JSON 解析）
- [x] **DataCache** — 内存缓存（code+period 分组，线程安全）
- [x] **StockSearchIndex** — 股票搜索索引（代码/名称/拼音首字母）
- [x] Foundation 补充 `bar.cpp`（BarSeries 实现）

### 测试结果
- **Total: 39 tests, 0 failed**（+14 新测试）
  - DataRepositoryTest: 4 tests（schema/股票保存加载/K线保存加载/同步日志）
  - DataCacheTest: 4 tests
  - StockSearchIndexTest: 6 tests

### 排查记录
- **Qt SQL 崩溃根因**：test_data 的 main 缺少 `QCoreApplication`，导致 SQL 驱动插件加载崩溃（SEH 0xc0000005）。加入 QCoreApplication 后修复。
- **StockCode 解析 bug**：loadStockInfos 用完整 fullCode（SH600519）构造 code，改用 StockCode(fullCode) 解析构造函数修复。

### 下一步 → P2 Engine 核心引擎
Strategy 基类 + BacktestEngine + PaperTradeEngine + FeeCalculator + Performance
- [x] 需求讨论：综合性股票交易工作站，30+功能
- [x] 架构设计：分层服务架构（7层），UI→Intelligence→Engine→Core→Data→Foundation
- [x] 技术栈确定：C++17 + Qt 6.5+ + CMake + Ninja + MSVC
- [x] 功能清单：P0-P10 分阶段实施计划
- [x] 项目文档结构搭建 (docs/) + CLAUDE.md

## 2026-08-02 — P0 项目骨架搭建完成

### 已完成
- [x] Foundation 层完整实现
  - [x] enums.h: Market, Direction, BarPeriod, OrderType, OrderStatus 等10+枚举
  - [x] types.h: Price, Volume, Amount, DateTime 等核心类型别名
  - [x] stock_code.h/cpp: 市场自动检测（SH600519/600519 两种格式）
  - [x] bar.h: OHLCV K线 + BarSeries（历史序列+技术指标辅助）
  - [x] tick.h: Tick、MarketDepth、Quote 实时行情结构
  - [x] order.h: Order/Trade/OrderUpdate 订单成交体系
  - [x] portfolio.h: Position/Portfolio 持仓管理
  - [x] utils/datetime.h/cpp: 日期解析、交易日计算、周末检测
  - [x] utils/string_utils.h/cpp: trim/split/toLower/startsWith/endsWith/拼音
  - [x] utils/json_utils.h/cpp: JSON 文件读写 + 嵌套路径读写
- [x] CMake 构建系统
  - [x] FetchContent: nlohmann-json, spdlog, cpp-httplib, GoogleTest
  - [x] Qt 可选开关 (BUILD_WITH_QT=OFF/ON)
  - [x] CMakePresets.json: default / with-qt / release
  - [x] build.bat: 一键构建+测试脚本
- [x] MSVC 编译环境验证通过（MSVC 14.51 + Ninja + Windows SDK 10）
- [x] 其他层桩文件 (stubs) 创建
- [x] 示例策略: ma_cross / momentum / grid_trading

### 测试结果
- [x] Foundation 层单元测试：**18 tests passed, 0 failed**
  - StockCodeTest: 8 tests
  - DateTimeTest: 3 tests
  - StringUtilsTest: 5 tests
  - JsonUtilsTest: 2 tests

## 2026-08-02 — P0 完成：Core 层实现

### 已完成
- [x] Qt 6.11.1 安装 + 集成（MSVC 2022 64-bit @ D:\Qt\6.11.1\msvc2022_64）
- [x] CMake 集成 Qt：AUTOMOC、BUILD_WITH_QT 开关
- [x] Core 层完整实现：
  - [x] **EventBus** — Qt 信号槽 + lambda 双模式，线程安全，跨模块通信
  - [x] **ThreadPool** — QThreadPool 封装，worker pool (CPU) + IO pool 分离
  - [x] **ConfigManager** — JSON 配置管理，支持嵌套路径 "a.b.c"
  - [x] **LogManager** — spdlog 封装，console + file dual sinks
  - [x] **NotificationService** — 统一通知通道，EventBus 集成，历史记录
- [x] Core 层单元测试：7 tests passed
- [x] Foundation 层单元测试：18 tests passed

### 测试汇总
- **Total: 25 tests, 0 failed**

### 代码统计 (估算)
| 层 | 文件数 | 代码行数 |
|----|--------|---------|
| Foundation | 10 | ~500 |
| Core | 10 | ~600 |
| Data | 8 (stubs) | ~100 |
| Engine | 12 (stubs) | ~150 |
| UI | 12 (stubs) | ~150 |
| Tests | 10 | ~250 |

### 关键决策
- **vcpkg + 系统 Qt**：vcpkg 管理 C++ 库（nlohmann-json/spdlog/cpp-httplib/gtest），Qt 6.11.1 单独安装由 CMAKE_PREFIX_PATH 指定
- **C++17 替代 C++20**：`operator<=>` 不可用，改用手动比较运算符
- **AUTOMOC 修复**：静态库中 Q_OBJECT 需 `#include "moc_xxx.cpp"` 显式触发
- **Qt DLL PATH**：ctest 需 Qt bin 在 PATH 中

### 下一步 → P1 Data 层
DataProvider 抽象接口、AKShare HTTP 接入、SQLite 存储实现

---

## 模板 — 每日记录格式

```
## YYYY-MM-DD — 简短描述

### 已完成
- [x] 事项1
- [x] 事项2

### 待办
- [ ] 待办1
- [ ] 待办2

### 关键决策
- 决策1：原因
- 决策2：原因

### 遇到的问题
- 问题1：解决方案
```
