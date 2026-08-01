# 开发日志 (Development Log)

## 2026-08-01 — 项目初始化
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
- **放弃 vcpkg**：网络访问 GitHub 超时，改为 FetchContent + Qt 单独安装
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
