# CLAUDE.md — StockTerminal 项目工作指引

## 项目概述

StockTerminal 是一款综合性股票交易工作站软件。
C++17/20 + Qt 6.5+ + vcpkg + CMake，分层服务架构。

## 关键文件路径

| 用途 | 路径 |
|------|------|
| 设计文档 | [docs/architecture.md](docs/architecture.md) |
| 需求规格 | [docs/requirements.md](docs/requirements.md) |
| 技术栈说明 | [docs/tech-stack.md](docs/tech-stack.md) |
| C++ 编码规范 | [docs/coding-standards.md](docs/coding-standards.md) |
| CMake 构建指南 | [docs/cmake-guide.md](docs/cmake-guide.md) |
| Qt UI 开发规范 | [docs/qt-ui-standards.md](docs/qt-ui-standards.md) |
| 数据库设计 | [docs/database-schema.md](docs/database-schema.md) |
| 模块接口规范 | [docs/api-design.md](docs/api-design.md) |
| 测试策略 | [docs/testing-guide.md](docs/testing-guide.md) |
| Git 工作流 | [docs/git-workflow.md](docs/git-workflow.md) |
| 安全设计 | [docs/security.md](docs/security.md) |
| 开发日志 | [docs/DEVLOG.md](docs/DEVLOG.md) |
| 变更记录 | [docs/changelog.md](docs/changelog.md) |
| 问题排查 | [docs/troubleshooting.md](docs/troubleshooting.md) |

## 架构

```
UI → Intelligence → Engine → Core → Data → Foundation
```

分层服务架构，严格自上而下依赖。层间通过纯虚接口耦合。

关键设计原则：
- Qt 主线程永不阻塞，耗时操作抛到 ThreadPool
- 所有数据源通过 IDataProvider 抽象接口接入
- StrategyEngine 被 BacktestEngine 和 PaperTradeEngine 共用

## 工作原则

1. **小步迭代** — 阶段拆分为小任务，每个任务：实现 → 编译 → 测试 → 再推进
2. **编译零警告** — 每次修改后 `cmake --build --preset default` 零错误零警告
3. **测试先行** — Foundation/Core/Engine 层写单元测试
4. **更新 DEVLOG** — 每完成一个阶段更新 [docs/DEVLOG.md](docs/DEVLOG.md)
5. **先读规范** — 实现前先阅读相关 docs/ 规范文档

## 构建命令

```bash
# 构建和测试（双击 build.bat 或在终端运行）
build.bat

# 或手动：
cmake --preset default
cmake --build --preset default
ctest --preset default

# 有 Qt 后：
cmake --preset with-qt && cmake --build --preset with-qt
```

## 技术栈

| 类别 | 选择 |
|------|------|
| 语言 | C++17 |
| UI | Qt 6.11.1 (MSVC 2022) |
| 构建 | CMake 3.21+ + Ninja + vcpkg |
| 包管理 | vcpkg (manifest mode) |
| 数据 | SQLite (Qt6::Sql) |
| 日志 | spdlog |
| JSON | nlohmann/json |
| HTTP | cpp-httplib |
| 测试 | GoogleTest |

## 当前阶段

**P9 ✅ → P10 第二轮 ✅ → P10 第三轮 ✅ → P10 第四轮 ✅ → P10 第五轮 ✅ → P10 第六轮 ✅ → P10 第七轮 ✅ → P10 第八轮 ✅（资金数据：龙虎榜 + 融资融券，顶部「资金」菜单独立窗口；北向资金因 2024 披露调整移除）→ P10 第九轮 ✅（交易日志：模拟vs实盘对比，含精确配对/收益曲线/费率设置）→ P10 第十轮 ✅（定时任务：刷新行情/跑选股/抓数据/提醒，设置菜单独立窗口）→ P10 第十一轮 ✅（交易标记：K线+分时买卖箭头 + 模拟/实盘持仓成本线 + 悬停浮框）→ P10 第十二轮 ✅（多窗口开图对比：独立图表窗口 + 新窗口按钮/右键 + 递归开窗）→ P10 第十三轮 ✅（全量 CSV 导出：6 面板 + UTF-8 BOM）→ P10 第十四轮 ✅（市场窗口合并板块：4 平级 tab + 统一错峰刷新 + 板块双击开图）→ P10 第十五轮 ✅（自选股列表 + 板块成分股下钻 + 市场收编视图菜单）→ P10 第十六轮 ✅（区间统计：K线拖拽选区 + 全套指标弹窗 + 选区高亮保留）→ P10 第十七轮 ✅（AI 综合信号：形态+情绪+技术融合评级 + 主窗口右侧 Dock + 历史信号记录）→ P10 第十八轮 ✅（参数优化热力图：结果表/热力图双视图 + 悬停浮框 + 最优格高亮 + 双击应用参数）→ P10 第十九轮 ✅（AI 选股工作流：AI 因子勾选 + 结果表 AI 分列 + 按 AI 分排序 + 情绪限量拉取）→ P10 第二十轮 ✅（策略模板+向导：6 策略模板类别分组 + 参数说明悬停 + 应用回测一键跳转）→ P10 第二十九轮 ✅（因子库扩充 11→24：技术因子 13 个新 + 估值类首落地 PE/市值，批量基本面快照注入选股引擎）**

Foundation: ✅ (56) | Core: ✅ (21) | Data: ✅ (112) | Engine: ✅ (207) | Intelligence: ✅ (73) | 总计: ✅ 473 tests | Qt: ✅ 6.11.1 | TA-Lib: ✅ 0.7.1 | 数据源: ✅ 通达信 TDX (直连 :7709) + 腾讯/AKShare 备源 + MultiProvider

| 阶段 | 内容 |
|------|------|
| P0 | 类型系统、EventBus、ThreadPool、日志、配置 |
| P1 | DataProvider抽象、AKShare接入、SQLite存储 |
| P2 | Strategy基类、BacktestEngine、PaperTradeEngine |
| P3 | StockScreener、MarketEngine、Fundamental |
| P4 | 示例策略 + 端到端验证 |
| P5 | UI 框架 |
| P6 | UI 图表核心 |
| P7 | UI 量化面板 |
| P8 | UI 数据面板 |
| P9 | Intelligence 层 |
| P10 | 扩展增强 |

## 目录结构

```
d:\StockTerminal/
├── CMakeLists.txt          # 根 CMake
├── vcpkg.json              # vcpkg manifest
├── CMakePresets.json       # CMake presets (default/release)
├── CLAUDE.md               # 本文件
├── config/                 # 配置文件
├── docs/                   # 项目文档
├── src/                    # 源代码
│   ├── foundation/         # 基础类型/工具
│   ├── data/               # 数据访问层
│   ├── core/               # 核心服务
│   ├── engine/             # 业务引擎
│   ├── intelligence/       # AI 智能层
│   └── ui/                 # Qt UI
├── tests/                  # 单元测试
└── examples/               # 示例策略
```
