# 变更记录

## 2026-08-16 — P10 第三十三轮：模拟交易搜索选股 + 多股票同时模拟
- 引擎多股票化：addStrategy(code, strategy) 按股票绑定独立策略实例；挂单/最近价按 code 隔离（B 报价不会成交 A 挂单）；单股票旧用法完全兼容
- 面板：股票池换全市场搜索多选（StockPoolPicker）；启动逐只播种+独立策略；轮询批量报价逐只驱动；状态区新增股票数
- 测试 +3 → 476 全绿（Debug+Release）

## 2026-08-16 — 修复轮⑩：优化建议两阶段进度映射错乱
- 根因：网格阶段 50~100% + 压力测试 90~100% → 网格完成时进度条已满/ETA 0s，压力测试阶段跳回 90% 且 ETA 不更新 → 误判卡死
- 修复：网格 0~90% + 压力测试 90~100% 单调过渡，stress 回调更新 ETA；修复压力测试窗口子进度 idx 时序倒退 bug
- 473 全绿（Debug+Release）

## 2026-08-16 — P10 第三十二轮：全市场回测内存优化 + 进度修复
- BacktestEngine 新增 keepEquitySnapshots：网格搜索/回测/对比/压力不存每日完整 Portfolio 快照（净值曲线内部累积，零拷贝 netValue）
- timeline 改存 const Bar* 指针：消除每组合 440 万 Bar 值拷贝（~420MB）
- 优化/建议 Release 并行 lane 上限 4（原全部核数）；进度统计改为所有在跑组合加权平均 + 单调保护（不倒退/不停滞）
- 优化/建议运行前显示「N 组合 × M 只」预估提示
- 实测：全市场 5213 只单次回测 76s / 峰值 1.6GB（数据固有 830MB）；473 全绿（Debug+Release）

## 2026-08-16 — 修复轮⑨：量化工作台打开崩溃（0xC0000374）
- 根因：TDX `ensureConnected` 连接失败/慢时每个 executeCommand 都 spawn detached doConnect 线程 → 打开量化工作台 8+ 组件并发拉列表触发线程风暴 + transport_ 竞争 → 堆损坏
- 修复：连接失败重试加 2s 冷却（启用预留的 lastConnectAttempt_）；StockPoolPicker/StockSearchBar 加载前等待连接就绪（≤15s）
- 验证：崩溃配置 20 轮复现全过 + GUI 快速打开 5/5 存活；473 全绿（Debug+Release）

## 2026-08-15 — P10 第三十一轮：Release 构建 + 进度细化（全市场卡住修复）
- 新增 `release-qt` preset（MSVC Release + Qt，独立目录 build/release-qt，复用 vcpkg 已装库）+ `run-release.bat` 一键启动
- 实测基准（300 只 × 12 组合 MACross 优化）：Debug 454.8s vs Release 54.2s，**快 8.4 倍**，结果一致（43.67）
- 进度细化：GridSearchOptimizer 组合内按回测日期细分上报（全市场 ~900 点/组合），优化/建议/回测/对比/压力 5 面板 IO 阶段节流（每 2%）+ 进度条旁新增「已用 X · 预计剩余 Y」实时估算（共享 `ui/utils/progress_eta.h`）
- 修复：GridSearchOptimizer worker 丢失 results[i] 赋值（release 测试暴露）
- 测试 473 全绿（Debug + Release 双验证）；构建零警告

## 2026-08-15 — P10 第三十轮：量化面板全市场股票池
- 新增共享控件 `ui/widgets/stock_pool_picker`：异步加载全 A 股（TDX 实连 5213 只）+ 搜索过滤（代码/名称/拼音）+ 勾选多选 + 全选/清空 + 已选计数，加载完成默认全选
- 6 面板替换内置精选池（130 只）→ 全市场：参数优化/优化建议/选股/回测/策略对比/压力测试
- 名称映射全面切到全市场列表（结果表/成交表股票名不再限于精选池）；选股「输出前N」上限 129 → 1000
- 模拟交易（单股票引擎 + 下拉单选）本轮未动，下轮做引擎多股票改造 + 搜索多选
- 测试 473 全绿；构建零警告

## 2026-08-15 — P10 第二十九轮：因子库扩充（11 → 24）
- 纯技术因子新增 13 个：动量 CCI(14)/威廉%R/乖离率/连涨天数；波动 布林带位置/20日均振幅；质量 MA金叉/52周价格位置/MA20斜率；量价 MFI(14)/量价配合
- 估值类首落地：市盈率TTM（低估值高分）/ 总市值（小市值偏好）——`FactorContext.quote` + `StockScreener.setQuoteFundamentals` + `IDataProvider.batchQuoteFundamentals`（腾讯/AKShare 批量，MultiProvider 主备回退）
- 选股面板：IO 阶段批量拉基本面快照注入引擎；因子勾选区自动出现 24 个新因子（中文名映射齐全）；估值因子无数据时降级中性 50 分不阻塞
- 测试 456 → 473 全绿；构建零警告

## 2026-08-15 — P10 第二十八轮：量化工作台菜单栏顶层
- 删除「量化」子菜单，「量化工作台」提升为菜单栏顶层项（与市场/资金数据并列，点击直接打开）
- 测试 456 全绿；构建零警告

## 2026-08-15 — 修复轮⑧：表单行内控件 stretch 分配（间距 6px 实测）
- 根因：行内 QLabel 与 spin 均被 QHBoxLayout 均分拉伸（「从」label 拉宽至 104px 文本后留白 76px）→ 视觉间隔远
- 修复：spin/日期框 stretch 1 独占剩余，行内标签保持文本宽——标签与控件 6px 紧贴，控件不收缩（离屏渲染几何验证 + form_render 工具）
- 测试 456 全绿；构建零警告

## 2026-08-15 — 修复轮⑦：表单标签紧贴行式布局
- 4 面板配置表单改单行式（label + 控件 stretch 1）：标签与控件间距 6px，控件保持拉伸宽度不收缩（QFormLayout 标签列结构导致的间距问题根治）
- 测试 456 全绿；构建零警告

## 2026-08-15 — 修复轮⑥：表单控件宽度恢复
- 移除 FieldsStayAtSizeHint（控件恢复默认拉伸宽度不收缩），保留标签左对齐 + 紧凑间距
- 测试 456 全绿；构建零警告

## 2026-08-15 — P10 第二十七轮：表单紧凑化（标签间距）
- 优化/建议/回测/策略对比面板：QFormLayout 标签左对齐 + 字段保持自身宽度（控件紧贴标签）
- 股票池/策略预设列表保最小宽 280px；测试 456 全绿；构建零警告

## 2026-08-15 — P10 第二十六轮：压力测试数据红绿着色
- 收益/年化/夏普/收益差：正红负绿；最大回撤：>20% 警示红 / ≤20% 可控绿；胜率 50%、盈亏比 1.0、净值 1.0 分界着色
- 测试 456 全绿；构建零警告

## 2026-08-15 — P10 第二十五轮：量化工作台表格均分 + 居中（全量，舆情除外）
- 7 个结果表列宽均分（header Stretch）+ 5 个模型单元格居中（TextAlignmentRole，共享模型覆盖优化/建议、回测/模拟）
- 舆情情绪例外：标题列弹性占满左对齐显示全，评分/情绪列自适应 + 居中
- 股票池列表保持左对齐；测试 456 全绿；构建零警告

## 2026-08-15 — P10 第二十四轮：优化建议 tab 网格结果表均分 + 居中
- 仅优化建议面板：结果表列宽均分（header Stretch）+ 单元格居中（view 级 CenterDelegate，不动共享模型）
- 测试 456 全绿；构建零警告

## 2026-08-15 — P10 第二十一轮：策略模板共享目录（全策略面板自动同步）
- 新增 `ui/strategy_catalog.h` 单一数据源：StrategySpec（类别/说明/参数键名/默认值/范围）+ all/byId/makeParams
- 5 面板去硬编码：策略/优化/优化建议/压力/模拟下拉遍历目录自动同步 6 策略；参数标签/范围/默认值按 spec 切换；模拟交易 makeStrategy 统一走 GridSearchOptimizer
- 策略对比预设 6 → 10（新增动量/突破/均值回归/RSI）；优化/建议搜索范围=默认值~+20 步长 2
- 测试 456 全绿；构建零警告

## 2026-08-15 — 修复轮④：策略向导参数说明常显 + 回测成交表名称列
- 策略面板: 参数说明由悬停 tooltip 改为 SpinBox 下方常显灰色小字（不再依赖悬停触发）
- 成交表: 新增「名称」列（时间/代码/名称/方向/...，精选池名称映射，非池内显示 "--"）
- 测试 456 全绿；构建零警告

## 2026-08-15 — P10 第二十轮：策略模板增强 + 向导（AI 量化工作流第 4 轮，收尾）
- 引擎: 4 个新策略——Momentum（N 日收益阈值+均线离场）/ Breakout（收盘价突破）/ MeanReversion（超跌回归）/ Rsi（超卖买卖）；共享 strategy_helpers；makeStrategy 注册 6 id
- 向导: StrategyPanel 模板按类别分组（趋势/动量/突破/均值回归/反转）+ 参数说明悬停 tooltip + 应用回测；BacktestPanel 下拉 6 策略 + 参数标签/默认值按 id 切换
- 测试: Engine 174 → 190（+16 StrategyTemplateTest 手动 ctx 驱动），总计 440 → 456 全绿；构建零警告
- 已知限制: 每策略暴露 2 主要参数（其余固定默认）；优化/对比/压力/模拟面板下拉 v2 扩展；向导无 Advisor 建议值

## 2026-08-15 — 修复轮③：形态识别结果表按最新时间倒序
- PatternTableModel::setRows 反转行序（detect 输出升序 → 表内最新在前）；纯 UI 模型改动，440/440 全绿

## 2026-08-15 — 修复轮②：AI 因子勾选未生效（全选/单选情绪 AI 分相同）
- 根因: `AiScreenerConfig.useSentiment` 硬编码 true + 形态勾选状态从未传入引擎 → 勾选组合不影响结果
- 修复: 配置增 `usePattern`（对齐设计文档）+ UI 真实传递两个勾选状态；`usePattern=false` → 形态分项缺失折减
- 测试: Intelligence 73 → 75（+2：PatternDisabled / PatternToggleChangesScore），总计 438 → 440 全绿；构建零警告

## 2026-08-15 — 修复轮：选股日期区间 bug（P7 遗留）+ 热力图轴标签 + 结果上下文
- **选股全 -- 根因**：`addTradingDays(负天数)` 原实现直接返回原日期 → 选股/回看 start=end=当天 → TDX 严格区间返回空 → 因子全 nullopt（默认 50 分）+ AI 分 0；修复支持负天数（向前数非周末日，与向后对称）+ 2 例单测
- 热力图: x 轴刻度移到图表底部（不再压表格）；y 轴名竖排防截断
- 参数优化: 结果区新增上下文信息行（股票池名称/目标函数/日期区间），明确结果对应关系
- 测试: Foundation 56 → 58，总计 436 → 438 全绿；构建零警告

## 2026-08-15 — P10 第十九轮：AI 选股工作流（AI 量化工作流第 3 轮）
- 引擎: `ai_screener`（intelligence/screener，纯 C++17）——`runAiScreener` 复用 `composeSignal` 分项逻辑（形态 detectAt(3)+RSI/MACD/动量+情绪）→ AI 综合分 0~100 + 三可选分项 + 一句话结论；权重可配、情绪缺失降级、按综合分降序
- 面板: 选股配置加「AI 因子: 形态/情绪」勾选（情绪仅拉池前 30 只，限量防网络阻塞）；结果表新增「AI 分」列（红涨绿跌）并可按 AI 分排序；取消勾选 → 与旧版完全一致
- 模型: ScreenResultModel 可选 AI 列（空 aiScores = 不显示），列布局 排名/代码/名称/总分/AI分/因子明细
- 测试: Intelligence 65 → 73（+8 AiScreenerTest），总计 428 → 436 全绿；构建零警告
- 已知限制: AI 分不融合现有 11 因子（总分列独立保留）；情绪限量前 30 只；情绪权重 UI 未暴露（v2）

## 2026-08-15 — P10 第十八轮：参数优化热力图（AI 量化工作流第 2 轮）
- 引擎: `grid_heatmap`（engine/optimizer，纯 C++17）——`HeatmapMatrix` + `buildHeatmap`（升序去重坐标轴/缺格 NaN/同格 last wins/无效参数 nullopt）
- 组件: `GridHeatmapWidget`（ui/widgets 自绘）——优红劣绿灰中渐变（MaxDrawdown 方向自适应反转）、缺格深色、最优组合白框、悬停浮框（参数+目标值）、双击格应用参数、右侧优→劣图例
- 面板: 优化面板结果区改「结果表/热力图」双 tab；单参数/空结果自动禁用热力图
- 测试: Engine 166 → 174（+8 GridHeatmapTest），总计 420 → 428 全绿；构建零警告
- 已知限制: 仅 2 参数出图（单参数 v2 做 1D 折线）；热力图与结果表无选中联动；颜色按当次结果归一化

## 2026-08-14 — P10 第十七轮：AI 综合信号面板（AI 量化工作流第 1 轮）
- 引擎: `composite_signal`（intelligence/signal，纯 C++17）——`composeSignal` 融合形态+情绪+技术 → 评级/得分/置信度/分项/中文摘要；默认权重 形态0.4/情绪0.3/技术0.3 可覆盖；缺失分项按权重折减置信度（分层修正：设计文档 engine/analyzer → intelligence/signal，避免 engine 依赖 intelligence）
- 面板: `AiSignalPanel` 主窗口右侧 Dock（与筹码 tabify）——评级大字+得分/置信度/日期+一句话结论+分项分数条（自绘 -1~+1 红正绿负）+ 历史信号表（会话内 50 条双击开图）；绑定搜索/市场/量化/资金/日志 5 处开图路径 + 视图菜单开关
- 数据: 日K（TDX）+ 东财新闻（仅个股，指数跳过情绪分项）；安全异步（IO→Worker→QPointer+gen 守卫）
- 测试: Intelligence 52 → 65（+13 CompositeSignalTest），总计 407 → 420 全绿；构建零警告
- 已知限制: 情绪依赖东财接口（失败降级缺失）；历史信号仅本会话；评级权重 UI 未暴露（v2）

## 2026-08-12 — P10 第十六轮：K线区间统计（全套指标 + 弹窗表格）
- 区间统计引擎: `RangeStats` + `computeRangeStats`（engine/analyzer/range_statistics，纯 C++17 无 Qt 依赖）——闭区间 [from,to] 统计，无效 bar 跳过、基准=首个有效 open、量额比均价、除零守卫，全无效区间返回 nullopt
- 图表交互: KLineChart DrawMode 加 Range（控制条「区间统计」按钮）——拖拽选区高亮（半透明块+两端虚线+首末日期）、松开发 `rangeSelected(bars,from,to)`；弹窗关闭高亮保留；切股/切周期/清除标注/退出模式清选区；坐标锚定 bar 索引随平移缩放稳定
- 弹窗: RangeStatsDialog 模态表格（指标/数值 2 列 10 行，涨跌幅/振幅红涨绿跌，量额 手/万/亿 格式化）；CentralChartWidget 接 rangeSelected → 弹窗（标题「股票名（周期）」），独立图表窗口共用自动生效
- 测试: Engine 158 → 166（+8 RangeStatisticsTest），总计 → 407 全绿

## 2026-08-12 — P10 第十五轮：自选股列表 + 板块成分股下钻 + 市场收编视图菜单
- 自选股: WatchlistStore（foundation 层 JSON 持久化，纯 C++17 可单测）+ WatchlistModel（3 列 QAbstractTableModel 行情表 涨跌幅红涨绿跌）+ WatchlistPanel（左侧主 Dock + 10s 交互优先级刷新 + 双击开图 / 右键移除）
- 图表同步: 周期栏「加入自选」checkable 按钮 —— 已在自选显示「已在自选」；经 currentCodeChanged / watchlistChanged 双重同步，切换股时按钮状态随自选列表自动更新
- 板块成分股下钻: SectorConstituentsDialog —— 板块行右键「查看成分股」→ 模态弹窗 4 列（代码/名称/最新价/涨跌幅%）降序 + 双击开图
- 市场收编: 市场 Dock 收编到「视图→市场」菜单（默认隐藏 toggleViewAction）；自定义指数 Dock 在自选股下方竖排
- 测试: Foundation 52 → 55（+3 WatchlistStore），总计 392 → 395 全绿（纯 UI 装配 / foundation 存储层，无引擎/数据层变更）

## 2026-08-11 — P10 第十四轮：市场窗口合并板块窗口（4 平级 tab + 统一错峰刷新 + 板块双击开图）
- 功能: MarketPanel 改为 4-tab（涨幅榜/跌幅榜/行业板块/概念板块）+ 市场宽度固定底栏；板块窗口独立 Dock 移除，自定义指数 Dock 独立竖排（在市场 Dock 下方）
- 数据: 行业/概念板块表用 TDX 板块指数全量源（880xxx）+ 虚拟化 QTableView + SectorListModel/SectorListPage 组件（固定类型列）
- UI: 板块表模板同步涨幅榜——移除硬编码黑底灰字、跟随主题、涨跌幅红涨绿跌；双击板块行→中央图表打开对应板块 K 线（880xxx）
- 刷新: 统一 30s 错峰——市场池 t=0、行业 +1s、概念 +2s（QTimer::singleShot）；定时任务 RefreshQuotes 简化
- 测试 392 全绿（纯 UI 重构/装配，无引擎/数据层变更，无新增单测）
- 已知限制: 市场宽度 tab 固定底栏非平级；双击板块 K 线不联动右侧盘口/关键数据（绑定主窗口中央图表）

## 2026-08-11 — 板块窗口改用通达信板块指数全量源（去掉领涨股列 + 切换缓存）
- 修复: 板块窗口原用东财 clist（被 IP 封锁）→ 新浪兜底（行业仅 49 个）→ 数据不全
- 数据: SectorPanel 改用 `IDataProvider::getSectorIndices()`（TDX 板块指数，实测行业 132 / 概念 438 全量）+ `batchQuote`（涨跌幅/成交额，132/132 有效报价）——与叠加对比对话框同源同过滤
- UI: 表格 4 列 → 3 列（板块/涨跌幅/成交额），移除领涨股列（TDX 简单报价不提供）；涨跌幅降序全量展示（可滚动）
- **切换优化 + 卡死修复**:
  - 每类型结果缓存 + 每类型 seq 去陈旧 → 重复切换即显示缓存不卡顿
  - **交互优先级**（TdxProvider InteractiveGuard + yieldToInteractive）：批量报价在 chunk 间让位于交互请求；板块面板用 `batchQuoteInteractive` 抢占市场批量刷新（实测批量运行中交互批量 **635ms** 完成）
  - **绘制卡死修复**：QTableWidget 438 行大表在 app 完整环境（Win11 样式）下滚动触发 Qt 绘制死循环（gdb 定位主线程卡在 Qt6Gui/Widgets 绘制路径）→ 板块榜单改用 **QTableView + SectorListModel**（虚拟化渲染，只画可见行），卡死消除
- 实测: TDX 板块指数 652 个全量抓取 + 报价有效，远超新浪 49 个

## 2026-08-11 — P10 第十三轮：全量 CSV 导出
- 功能: 6 面板加「导出」按钮（选股结果/市场全景/板块行情/资金数据×2/交易日志）→ CSV 文件（UTF-8 BOM，Excel 中文正常）
- 工具: `csv::tableToCsv`（foundation 纯函数，BOM + joinRow 转义）+ `ui/utils/table_csv_export`（QAbstractItemView 通用导出：QTableView/QTableWidget 统一）
- 回测: 保留原有导出（含绩效指标+净值曲线，多于表格，不统一）
- 测试 387 → 392：Foundation 47 → 52（CsvExportTest 5 例）
- 已知限制: 只做 CSV 不做 xlsx；市场宽度 tab 导跌幅榜；空表按钮静默；回测导出无 BOM

## 2026-08-11 — P10 第十二轮：多窗口开图对比
- 功能: 中央图表「新窗口」按钮 + 右键菜单「在新窗口打开」→ 独立图表窗口（ChartWindow：完整周期栏/叠加对比/交易标记，多实例可并存、可递归开窗）
- 图表: CentralChartWidget 加 `standalone` 模式（独立窗口去筹码分布按钮）+ `currentName()` + `openNewWindow` 信号
- 装配: MainWindow `openNewChartWindow`（QPointer 容器 + WA_DeleteOnClose + 级联偏移 + 递归连接）+ `refreshTradeMarks` 遍历所有窗口分发（onChange 覆盖式统一由 MainWindow 刷新）
- 环境: vcpkg 目录恢复（重新克隆 + VCPKG_MANIFEST_INSTALL=OFF 复用已装库）
- 测试 387 全绿（纯 UI 装配，无新增单测）
- 已知限制: 新窗口不联动盘口/关键数据；自定义指数不从新窗口开；chartWindows_ 只增不减

## 2026-08-10 — P10 第十一轮：K线持仓标注 + 交易标记
- 功能: 图表标注模拟+实盘交易数据——K线（日/周/月）+分时图买卖点箭头（红▲买/绿▼卖）+ 持仓成本线（模拟青/实盘橙虚线）
- 数据: 引擎层 `collectTradeMarks`/`deriveHoldings`（按 JournalType 各自独立 FIFO 推导持仓）+ `TradeJournalEngine::setOnChange` 变更回调（纯 C++17 可单测）
- 交互: 悬停 K 线浮框追加交易行；切股清空、切周期保留重定位；日志变更（模拟成交落库/手动增删）自动刷新图表标注
- 成本线: 模拟=AutoTrade 青色、实盘=ManualNote 橙色（方案 B：实盘也推导，依赖录入完整性）
- 装配: CentralChartWidget 缓存转发 + MainWindow 7 处加载点接线 + setOnChange QueuedConnection marshal
- 测试 376 → 387：Engine 147 → 158（TradeMarkTest 11 例）
- 已知限制: 分时悬停浮框 v1 不做；点击箭头跳转详情 v1 不做；实盘成本线依赖录入完整性

## 2026-08-08 — P10 第十轮：定时任务（刷新行情 / 跑选股 / 抓数据 / 提醒）
- 功能: 设置菜单 → 独立「定时任务」窗口（TaskWindow）——任务列表 CRUD（新建/编辑/删除/立即执行）+ 类型/触发/启用/上次结果展示
- 动作: 定时刷新行情（市场+板块）/ 跑选股（ScopeResolver 范围解析）/ 抓数据（范围内全部股票日线）/ 提醒（NotificationService）
- 触发: 固定时间（Daily "HH:MM"）+ 周期（Interval 每 N 秒）；TaskScheduler QTimer 10s tick，Daily 同日去重 + 60s 防重窗 + running 防重入
- 数据: ScheduledTask 模型（foundation）+ ScheduledTaskStore JSON 持久化（configDir/scheduled_tasks.json，增删改立即保存）
- 引擎: ScopeResolver 范围解析（全部 A 股 / 板块指数 / 上次手动选股——last v1 退化全部 A 股）
- 装配: MainWindow 动作执行器（RefreshQuotes 调市场/板块刷新、RunScreener/FetchData IO 池异步 + QPointer 守卫 + lastResult 回填）
- 测试 358 → 372：Foundation 38 → 47（ScheduledTaskTest 7 + ScheduledTaskStoreTest 2）；Engine 142 → 147（ScopeResolverTest 5）
- 已知限制: scope=last 为 v2 待接线；板块成分股 v1 用板块指数自身；无交易日历感知；执行历史只存 lastResult

## 2026-08-08 — P10 第九轮：交易日志（模拟vs实盘对比 + 费率设置）
- 功能: 交易记录完整 CRUD（新建/编辑/删除/清空）+ 文字筛选（代码/名称/策略/注解）+ 类型徽标（自动交易蓝/手动录入红/策略信号绿）
- 对比回顾: 模拟vs实盘双序列收益曲线 + 逐笔精确配对（同代码同方向同数量，标价差及百分比）+ 月度收益汇总 + 按代码已实现盈亏 + 按策略胜率/盈亏/交易数
- 费率设置: 独立 JournalFeeDialog——佣金费率/最低佣金/印花税率/过户费率可编辑 + 保存到 configDir/journal_config.json（影响后续新建/编辑自动算费用，已存条目不重算）
- 装配: MainWindow 顶部「日志」菜单 → 独立 JournalWindow；PaperTradePanel 模拟成交自动落库（QPointer + QueuedConnection 安全异步）
- 引擎: TradeJournalEngine 内存存储 + TradeJournalStore JSON 持久化 + computeStats 统计；线程安全（mutex）+ 指纹去重
- 测试 328 → 358：Engine 112 → 142（净增 30，其中交易日志新增 28：TradeJournalTest 5 + TradeJournalStatsTest 12 + TradeJournalPairTest 6 + TradeJournalStoreTest 5；其他层不变）
- 已知限制: 已存条目不因费率变更重算；StockSearchBar 暂无下拉建议；对比回顾 v1 不做模拟/实盘时间窗口滑动

## 2026-08-08 — 移除北向资金（2024 披露调整致数据不可得）
- 背景: 自 2024-05-13 起交易所停止披露沪深股通（北向）盘中/盘后实时净买入与成交额（保留仅每日盘后成交总额+十大成交活跃股，但数据中心接口不可用）
- 尝试: 北向快照/分钟（kamt）、历史成交额（kamt.kline 单位不可解释/陈旧）、十大成交股（报表配置不存在）——全部不可用
- 移除: 资金窗口「北向资金」tab + EastMoneyFundsProvider 北向快照/分钟解析 + 相关单测；保留龙虎榜 + 融资融券
- 测试 330 → 328

## 2026-08-08 — 资金数据：龙虎榜交易日下拉（只列有数据的交易日）
- UI: 龙虎榜日期选择从 QDateEdit 改为**交易日下拉框**——用 TDX 上证指数日线提取近 200 天真实交易日历（含节假日休市，周末/不开市日期不可选），最新在前、默认最新交易日
- 修复: 改头文件（funds_window.h/main_window.h）后增量构建残留陈旧对象 → Run-Time Check Failure #2 栈损坏（"stack around variable window corrupted"）——clean rebuild 后消失（项目记忆已知模式）
- 测试 330 全绿

## 2026-08-08 — P10 第八轮：资金数据（龙虎榜 / 北向资金 / 融资融券）
- 数据源可行性实测: 东财 datacenter-web（数据中心）+ kamt（北向）接口未被封锁（封锁的是 clist/K线/分时）——龙虎榜按日期、两融按股票、北向快照/分钟全可用
- 数据: EastMoneyFundsProvider——龙虎榜（RPT_DAILYBILLBOARD_DETAILSNEW 按日期）、北向快照/分钟（kamt.get / kamt.rtmin）、两融个股（RPTA_WEB_RZRQ_GGMX 按股票）+ 市场总览（RPTA_RZRQ_LSHJ）；URL 构建 + 纯静态解析（可单测）+ thread_local QNAM 同步 fetch
- UI: 顶部新增「资金」菜单 → 「资金数据」独立窗口（FundsWindow，仿量化工作台）；3 tab——龙虎榜（日期选择+榜单表+双击开图）/ 北向资金（当日快照+分钟折线自绘）/ 融资融券（沪深市场总览+个股两融明细表，经龙虎榜双击联动）
- 实连验证: 周五龙虎榜 68 条、沪深两融 26398 亿、茅台两融 120 条明细、北向分钟 241 点
- 测试 323 → 330（资金解析器 7 例：龙虎榜/北向快照/北向分钟/两融个股/两融总览/URL）

## 2026-08-08 — 量化工作台关闭卡顿修复 + 选股面板 use-after-free 加固
- 关闭卡顿根因: QuantWindow::closeEvent 对**共享线程池** `waitForDone()`（无超时）——线程池全应用共享（市场面板/图表/自定义指数都在用），关闭时等所有无关任务跑完（全市场批量报价可达几十秒）
- 修复: 移除 closeEvent 的 waitForDone（面板异步任务均为 QPointer 守卫 + shared_ptr cache，面板销毁后回调被 Qt 自动丢弃；provider 归 MainWindow 所有，~MainWindow 仍 waitForDone 后排空再释放——安全）。关闭耗时实测 1460ms → 6ms（在飞 3s 长任务下）
- 加固: ScreenerPanel 选股 Worker 任务从「裸 this 进度回调 + 裸 cache_.get()」改为「QPointer 守卫 + shared_ptr cache 拷贝 + 判空」——消除面板销毁时选股在跑的 use-after-free（真机场景选股中关窗会崩）
- 排查工具: quant_repro 量化窗口开/关耗时回归工具（保留）
- 测试 323 全绿

## 2026-08-08 — 视图菜单移除筹码分布 + 量化工作台崩溃修复（陈旧对象）
- UI: 视图菜单去掉「筹码分布」（仍可通过图表周期栏「筹码分布」按钮开关）
- 崩溃修复: 打开量化工作台即崩（Debug 断言 `_CrtIsValidHeapPointer`、事件日志 0xc0000374 堆损坏）——根因是大规模多轮开发改了大量头文件后增量构建残留陈旧对象 → ABI 错位 → 堆损坏；`--clean-first` 全量重建后消失
- 测试 323 全绿

## 2026-08-08 — 自定义指数分时修复：时区导致价格线画到屏幕外
- 根因: computeIndexIntraday 用 `secs % 86400` 取的是 **UTC** 当天分钟数（北京 09:30 = UTC 01:30 → 90 分钟），再 `baseDay + m分钟` 当本地时间用 → 时区偏 8 小时 → 所有分时点 x 坐标为负（首 x=-1464）→ 整条价格线画在主图左侧屏幕外，用户看到"分时没线"
- 修复: 改用 `localtime_s` 取**本地时区**当天分钟（与分时图 minutesFromOpen 的 localtime 一致）
- 验证: 离屏渲染 首x=-1464→64、末x=-417→825，严格蓝像素 77→3180；实连 600667+002185 分时 240 点
- 测试 323 全绿

## 2026-08-08 — 自定义指数修复：基准日=创建当天 + 分时权重重归一化
- 修复: 基准日默认从「成分股最早共同数据日」改为「创建当天」——指数当天=基点(1000)，历史回溯到此值；原来用最早共同数据日会累计出几千上万的虚高点位（用户反馈"价格那么高"）
- 修复: 基准日落在周末/节假日时，除数自动回退到最近交易日（≤基准日的最后一根 bar），避免 0 除返回空
- 修复: 分时计算中，分时数据缺失的成分股被跳过时权重重新归一化到可用成分股（避免指数迟钝或为空）
- 验证: 实连 茅台+平安 等权组合，基准日=创建当天 → 指数 1000，历史 ~1000-1100 区间，分时 240 点
- 测试 322 → 323（基准日非交易日回退 +1）

## 2026-08-08 — P10 第七轮：自定义指数
- 引擎: custom_index 数据模型（IndexConstituent/CustomIndex）+ 计算——价格加权+基点重定基日线指数（基准日=首个共同数据日，缺日 carry-forward，上市晚于基准日成分股剔除）→ 聚合成周/月线；分时/实时从指数昨收做加权涨跌幅外推（避免复权价与实时裸价衔接断裂）；权重归一化（全 0=等权）
- 引擎: CustomIndexStore JSON 持久化（configDir/custom_indexes.json，nlohmann 读写）
- UI: 左区「自定义指数」dock（与板块 tab 并列，视图菜单可开关）——指数列表 + 实时点位/涨跌幅（订阅成分股 QuoteReceived 驱动）+ 新建/编辑/删除/打开图表
- UI: CustomIndexEditorDialog 编辑器——名称/基点/成分股表（StockSearchBar 搜索加股，权重默认等权可改，均分按钮，权重合计）
- 图表: KLineChart/TimelineChart 加外部数据模式（setExternalReloader + loadBars/loadIntraday 直接载入）；CentralChartWidget::loadCustomIndex 编排（伪代码 US+CIxxx 占位，切日/周/月/分时都重算喂数据；切回普通股票退出该模式）
- 实连验证: custom_index_live 工具——茅台+平安+招行等权组合 2018→2026 日线 2086 根，基准=1000，昨收 3049.54，分时 240 点，实时外推 -0.36%（与成分股涨跌幅加权一致）
- 测试 300 → 322（引擎 22：指数公式/周月聚合/分时外推/实时外推/权重归一化 17 + store 读写 5）

## 2026-08-07 — 移除日志面板
- UI: 主窗口「日志」Dock 移除（含 Ctrl+L 聚焦快捷键、偏好设置快捷键列表、默认配置键）——界面不再显示 [info] 日志流
- 日志能力保留: LogManager 仍写文件日志（AppPaths::logDir()/stock_terminal.log）；log_panel.{h,cpp} 源文件保留在工程（未实例化），需要时可恢复
- 测试 300 全绿

## 2026-08-07 — 启动不再弹控制台窗口
- 构建: StockTerminal 由 console 子系统改为 GUI 子系统（add_executable 加 WIN32）——run.bat 启动不再出现额外 cmd 窗口（spdlog stdout sink 打到的那类 info）
- 日志不受影响: 仍走 UI 日志面板 + 文件日志；非 Qt 的 CLI 版 StockTerminal 保持 console 子系统不变
- 说明: 关闭主窗口后进程仍在销毁 ~MainWindow（waitForDone 等在途 TDX 任务 + join 心跳/轮询线程），原控制台窗口因此滞后关闭；改 GUI 子系统后此窗口不再出现

## 2026-08-07 — 日/周/月叠加改为 K 线蜡烛显示
- 引擎: OverlayRow 扩展完整 OHLC（open/high/low/close），alignOverlay 按日期存叠加标完整 OHLC
- 图表: 日/周/月叠加从「归一化收盘折线」改为「缩放叠加蜡烛」——缩放因子使叠加标锚点收盘对齐 base 锚点收盘，OHLC 等比缩放；品红空心蜡烛（半透明填充）区别于 base 红涨绿跌
- 图表: computeVisibleRange 纳入叠加蜡烛缩放后高低价，保证蜡烛完整可见
- 分时叠加仍为价格线（分时无蜡烛概念）
- 验证: 叠加煤炭 K 线蜡烛渲染 3111 品红像素；300 测试全绿

## 2026-08-07 — 分时叠线不渲染修复
- 修复: TimelineChart 叠加数据异步到达后未重算量程/锚点（overlayAnchor_ 只在 setData 里算，此时叠加数据还没到）→ 分时叠线画不出来（active=1 但 0 像素）
- 修复: fetchOverlayData 回调里补 computeRanges()，叠加数据到达即重算锚点再重绘
- 验证: 分时板块叠加渲染 0 → 1792 品红像素；K线叠加 1662 像素
- 测试 300 全绿

## 2026-08-07 — 板块叠加可用性修复：列表定向快抓 + 叠线改品红
- 修复: getSectorIndices 定向抓取 SH 列表开头（板块指数块）→ 板块列表从 ~30s 降到 ~0.25s；失败不缓存空（可重试）
- UI: 叠加对话框板块 tab 加「加载中…/暂无数据」占位，不再白屏空列表
- UI: 叠线颜色 蓝色 #40c4ff → 品红 #e040fb（蓝色与 MA20 青/分时价格蓝几乎同色难辨；品红为图上独有色，更醒目）
- 测试 300 全绿

## 2026-08-07 — 板块/概念叠加改用通达信板块指数（脱离东财封锁）
- 背景: 东财 clist/push2his 均被 IP 封锁，板块叠加数据源不可用
- 数据: IDataProvider::getSectorIndices()（TdxProvider 实现）——从 TDX SH 列表过滤通达信板块指数 652 个（880xxx，含行业 8803xx-8804xx 与概念 8805xx+），带缓存
- 数据: getBars 对 880xxx 用 isIndex 解码（板块指数记录格式同指数，多 4 字节）；实连验证 煤炭/证券/5G概念/DeepSeek K线 640 根、分时 240 点正常
- UI: 叠加对话框 行业/概念 tab 改用 TDX 板块指数（行业 8803xx-8804xx、概念 8805xx+，过滤大盘 880001-099/地域 8802xx）；每 tab 保留名称/代码搜索
- UI: fetchOverlayData 对 880xxx 板块代码走 TDX getBars/getIntraday（不受东财封锁影响）；非 880 代码仍走东财（BK/suggest）
- 测试 298 → 300（isSectorIndexCode + isTdxSectorCode）

## 2026-08-07 — 板块/概念叠加修复：新浪降级列表的代码解析为东财 BK
- 修复: 东财 clist 被封锁 → 板块列表降级新浪，返回 new_xxx 代码，东财 K线/分时接口不认 → 板块叠加拉不到数据
- 数据: EastMoneySectorProvider 新增 `resolveSectorCode`（东财 suggest API type=14 按名称查 BK 代码，失败剥"行业/板块"后缀重试）；fetchSectorKline/fetchSectorTrends 对非 BK 代码自动解析
- 实连验证: 银行→BK0475、玻璃行业→BK0546、食品饮料→BK0438 解析成功
- 测试 295 → 298（suggest 解析 3 例）
- 已知限制: 东财 push2his K线接口当前亦被 IP 封锁（外部 CDN），板块叠加需等封锁解除；指数/个股叠加走 TDX 不受影响

## 2026-08-07 — 行情/K线加载变慢修复（IO 池扩容 + 批量报价让出）
- 修复: 市场面板每 30s 对全 A 股 ~5000 只批量报价（~87 次串行 TDX 往返，实测 ~68s），IO 池仅 2 线程被长时间占用 → 期间选股/加载 K 线排队等待
- 修复: ThreadPool::ioPool 2 → 6 线程；TdxProvider::batchQuote 每 chunk 间让出 5ms（连接为每命令加锁，交互请求可插入）
- 实测: 批量报价期间 getBars 由「等完整批次 ~68s」降至「~4-6s 完成（K线+除权 2~3 命令）」；TDX 服务器延迟正常时更快
- 根因说明: 本轮叠加功能不涉及加载路径；变慢主因是 TDX 服务器当前延迟偏高且不稳定（单请求 0.4~1.6s，约为正常 16 倍）+ 全市场刷新放大器

## 2026-08-07 — P10 第六轮：指数/板块/概念叠加对比
- 图表: 中央周期栏「叠加对比」按钮 → 对话框选叠加目标（指数/个股搜索 + 行业/概念板块列表，三来源）
- 图表: 分时/日/周/月均可叠加，**按视图隔离**（分时↔K线各自独立，互不影响）；归一化叠线起点与可见区左缘对齐，随平移/缩放重锚定
- 图表: K线图新增「相对强弱」副图（个股/叠加标比值，锚点=100 + 参考虚线），分时仅叠价格线
- 生命周期: 切股清叠加（作废在途请求）；K线图内切周期保留叠加并重取
- 引擎: overlay_analysis（OverlayTarget + 按日期/按分钟对齐，纯函数可单测）
- 数据: EastMoneySectorProvider 扩展板块历史K线（kline/get）+ 板块分时（trends2），3-host 回退
- 测试 274 → 295（叠加对齐 12 + 板块K线/分时解析 9）

## 2026-08-07 — 筹码分布默认收起
- UI: 筹码分布 Dock 默认隐藏（不常驻），视图→筹码分布 按需打开/关闭（toggleViewAction 勾选联动）

## 2026-08-07 — 板块模块简化：全量热力图 → Top10 榜单
- UI: SectorPanel 从 Squarified treemap 全量热力图改为 QTableWidget 涨跌幅前十榜单（板块/涨跌幅/领涨股/成交额，红涨绿跌）
- treemap 引擎代码与测试保留（可复用）

## 2026-08-07 — 板块行情新浪兜底（东财 clist 被 IP 封锁）
- 修复: 东财 `/api/qt/clist/get` 对本 IP 被 CDN 拦截（curl 实测 HTTP 000 空回复）→ 板块数据一直拉不到
- 数据: EastMoneySectorProvider 东财优先（重试 1 次/主机）+ 连续 2 次全败后进程内隔离，降级新浪板块行情（行业/概念，GBK 转码解析）；重启自动复测东财
- UI: 悬停详情涨跌平家数条件显示（新浪源无该字段）
- 测试 270 → 274（新浪解析 4 例）；验证行业 49 / 概念 175 条真实数据，首刷 ~1s

## 2026-08-07 — 恢复板块热力图面板
- 修复: 定位堆损坏时临时注释掉的「板块」Dock 未恢复 → 左区板块热力图重新启用（行业/概念 Treemap）

## P10 第五轮 (2026-08-07) — 数据导出 CSV + K线截图 + 画线工具
- 工具: foundation/utils/csv（escape/joinRow/klineToCsv，fixed 精度避免科学计数）
- 图表: KLineChart 画线工具（水平线/趋势线/清除，锚定 bar+价格随缩放稳定）+ K线数据导出 CSV
- 主窗口: 文件→截图当前图表（grab 存 dataDir/screenshots/ PNG）
- 回测: 「导出结果」→ 绩效/净值曲线/成交明细 CSV
- 测试 266 → 270（csv 4 例）

## P10 第四轮 (2026-08-06) — 板块指数 + 概念热力图
- 数据: EastMoneySectorProvider 东财板块行情（行业 m:90+t:2 / 概念 m:90+t:3，自动分页，主机回退防限流）
- 引擎: Treemap Squarified 矩形树布局（面积∝成交额）
- UI: 左区「板块」面板 — 行业/概念 treemap 热力图（红涨绿跌强度按幅度）、悬停详情、30s 自动刷新、限流空态提示
- 工具: sector_calib 实连校准（行业 496 / 概念 504，top 钨+6.85% 等真实数据）
- 测试 255 → 266（板块解析 5 + treemap 6）

## P10 第三轮 (2026-08-06) — 筹码分布 + 成交分布 + 区间统计
- 引擎: ChipDistribution 筹码分布（三角分布 + 换手率衰减模型，200 桶，派生平均成本/获利盘/90%成本区间/集中度）
- 引擎: TransactionDistribution 当日成交分布（分时/逐笔 → 价格直方图）、RangeStats 区间统计（涨跌幅/振幅/换手/均价/量额）
- UI: 「筹码分布」右区 Dock（筹码云自绘 + 当日成交分布 + 区间统计网格 + 全部/250/120/60 日预设），随个股切换联动
- 数据: 流通股本（东财）做换手率衰减；缺失降级纯量模式
- 工具: chip_calib 实连校准（茅台 平均成本 1347/获利盘 29.8%/换手 83.75% 与行情吻合）
- 测试 241 → 254（+13：筹码 6 + 成交分布 3 + 区间统计 4）

## P9 (2026-08-04) — TDX 通达信数据源
- 数据源: 直连通达信主站（TCP :7709）完全替换腾讯，参考 injoyai/tdx 移植
- 协议层: WinSock2 TdxSocket + 帧编解码（0x0C 请求 / 0xB1CB7400 响应 + zlib）+ 变长整数/量解码
- TdxProvider: 单连接串行 + 断线重连 + 8 服务器 failover；日/周/月K线前复权（gbbq 仿射变换）；batchQuote 分块；getStockList 分页；subscribeQuote 轮询发布
- 接口: IDataProvider 加 batchQuote/getIntraday/refreshQuotes/providerName 纯虚
- 重构: 12 个 UI 类 TencentProvider*→IDataProvider*；main_window 用 makeDataProvider()（config data.provider 默认 tdx）；AKShare 补桩
- 实连验证: 登录/K线前复权/报价/列表均正确（茅台收 1328.36 与报价一致）
- 修复: getStockList=0（decodeCodeList 误读记录首字节为市场 → 改由调用方传入；列表上限按真实 Count）
- 新增: docs/tdx-protocol.md 协议笔记
- 单测: test_tdx_protocol + test_tdx_provider 26 用例（FakeTdxTransport 注入/复权/分块/分页/failover/重连）
- 修复: loadStockList uint16 start 溢出回绕无限循环（改 uint32 + 防回绕）
- 修复: decodeCodeList 名称 null 填充未修剪
- 修复: poll/heartbeat 线程固定 sleep → cv 可中断，disconnect 立即唤醒
- 分时 0x051D: 判定服务器非标准变体（pytdx 同失败），保持降级；fixture + findings 记录
- 测试 136 → 162
- 修复: decodeQuote 只读前 11 字段 → 多代码批量报价记录错位（涨幅/跌幅榜+市场宽度错误）；按 pytdx 完整字段序列消费整条记录（含五档）
- 修复: 指数 K线记录多 4 字节（涨跌家数）→ decodeKline 加 isIndex 跳过 + isIndexCode 判定（SH 000xxx/SZ 399xxx）
- 修复: 搜索框 Qt::Popup 抢键盘焦点导致每字符卡住 → WA_ShowWithoutActivating + popup 按键转发给 edit
- 市场: 涨幅/跌幅榜池 精选 129 → 全 A 股（~5000 只，过滤可交易前缀），刷新 30s，报价批上限 80
- 市场: 换手率 TDX 报价不含 → 显示 "—" 而非 0.00%
- 修复: 停牌股 TDX 返回 price=0 → 涨跌幅误算 -100%；市场面板排除 price<=0/preClose<=0
- 分时图: 0x051D 服务器非标准变体(死路) → 改用 0x0FC5 逐笔成交聚合（分页拉当日 → 1分钟 OHLC → 240轴），getIntraday 不再降级
- 搜索: 过滤非交易品种（999999/799999 回购等），仅可交易A股+指数；isTradableAShare 抽到 tdx_models 共享
- K线图: 价格轴标签颠倒修复（顶部=最高价）；主图下移避开标题条（MA 标签不重叠股票代码）；BOLL 面板内叠加 K 线蜡烛；图表左右留空 kMargin
- 分时图: 满 240 点数组（缺分钟价格 carry-forward）+ 丢弃盘后 15:00 后异常记录（防末分钟量膨胀）
- 分时图: MACD 用 preClose 扩展预热（~40 点）→ 从最左侧显示；图例标注 (12,26,9)；右轴 MACD 高低标注
- 分时图: 右轴价格标注 现价/涨跌幅/最高/最低
- K线: VOL 面板改"成交量"；十字光标量用万
- 量单位: K线/分时 成交量轴与图例均用 万（÷10000）
- 测试 165 → 167
- 已知降级: 分时 0x051D 格式待校准（getIntraday 返回 nullopt，Step 10）
- 状态栏/About 数据源文案动态显示 providerName()
- 测试 136 全绿（编译零警告）

## P7 修复 (2026-08-03) — 优化崩溃 + 回测性能
- 修复优化崩溃: BacktestEngine::getPortfolio 函数级 static → 实例成员（并行回测线程隔离）
- BarSeries 重构: shared_ptr 存储 + append()，回测热循环 O(n²) 整段拷贝 → O(1) 追加
- 优化面板 Debug 并行度调优（Debug CRT 堆锁 8 线程比单线程慢 → 限 2）
- 网格搜索性能: 6 股 90 组合 38s → 6s（单线程），并行 123s → 14s
- 回归测试: ConcurrentEnginesPortfolioSafe
- 测试 135 → 136

## P7 (2026-08-03) — UI 量化面板
- 引擎: 修复 Performance 交易统计（FIFO 配对 winRate/profitFactor/totalTrades/totalPnl）
- 引擎: GridSearchOptimizer 网格搜索（参数组合 + 并行回测 + 5 目标函数排序）
- 引擎: StrategyComparator 策略对比、StressTest 压力测试（5 预设极端窗口）、MonteCarlo 蒙特卡洛
- 引擎: 修复 PaperTradeEngine（onQuote 注入历史使趋势策略可交易 + seedHistory + buyByAmount 成交）
- UI: 选股面板（11 因子勾选 + 双击开图）
- UI: 模拟交易面板（单股票实时行情驱动 + 账户状态 + 成交表 + 日志）
- UI: 参数优化面板（范围搜索 + 点行应用到回测）
- UI: 策略对比面板（6 预设净值叠加 + 蒙特卡洛置信区间）
- UI: 压力测试面板（极端窗口回放 + 基线对比）
- UI: EquityCurveWidget 多序列、ScreenResultModel/GridSearchTableModel/ComparisonTableModel
- UI: "量化"dock 内嵌 5 标签页，tabify 到回测 dock
- 测试 117 → 135

## v0.1.0 (2026-08-01)
- 项目初始化
- 需求分析与架构设计
- P0: Foundation + Core 层架子

## P6 (2026-08-03) — UI 图表核心
- 自研指标库: SMA/EMA/MACD/BOLL/RSI（可单测）
- Data 层: 日/周/月/分钟 K线 + 分时数据接入（腾讯接口，实测锁定格式）
- KLineChart: QPainter 蜡烛图 + 量/MACD/RSI 副图 + MA/BOLL + 十字光标 + 缩放/平移 + 周期切换
- 分时图: 240 分钟轴 + 价格/均价线 + 昨收线
- 回测面板: 异步回测 + 12 项绩效 + 净值曲线 + 成交明细
- 策略面板: 策略模板库 + 参数编辑
- 市场全景: 涨幅/跌幅榜 + 市场宽度（实时 batchQuote）
- 修复: 周/月线返回日线 bug、Bar 无 preClose 涨跌幅计算
- 测试 103 → 117

## P5 (2026-08-03) — UI 框架
- Data 层增强: thread_local 线程安全 fetch、腾讯行情完整解析（GBK→UTF-8 + 时间戳锚定）、批量行情接口
- 内置精选股票池 129 只（真实名称校验，离线回退）
- QuotePoller 实时行情轮询（异步 + EventBus 发布），subscribeQuote 真实实现
- UI: 主窗口 QDockWidget 布局、全局搜索、顶部指数条、日志面板、快捷键、暗/亮主题
- 修复 LogManager logMessage 信号、AUTOMOC 陈旧、指数市场识别、指数/股票字段布局差异
- 测试 87 → 103
