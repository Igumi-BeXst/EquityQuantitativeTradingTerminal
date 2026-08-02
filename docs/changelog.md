# 变更记录

## v0.1.0 (2026-08-01)
- 项目初始化
- 需求分析与架构设计
- P0: Foundation + Core 层架子

## P5 (2026-08-03) — UI 框架
- Data 层增强: thread_local 线程安全 fetch、腾讯行情完整解析（GBK→UTF-8 + 时间戳锚定）、批量行情接口
- 内置精选股票池 129 只（真实名称校验，离线回退）
- QuotePoller 实时行情轮询（异步 + EventBus 发布），subscribeQuote 真实实现
- UI: 主窗口 QDockWidget 布局、全局搜索、顶部指数条、日志面板、快捷键、暗/亮主题
- 修复 LogManager logMessage 信号、AUTOMOC 陈旧、指数市场识别、指数/股票字段布局差异
- 测试 87 → 103
