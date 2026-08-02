# 变更记录

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
