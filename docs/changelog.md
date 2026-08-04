# 变更记录

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
- 测试 162 → 165
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
