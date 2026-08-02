# 开发日志 (Development Log)

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
