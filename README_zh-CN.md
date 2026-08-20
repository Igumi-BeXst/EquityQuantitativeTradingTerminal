# StockTerminal

> 面向 A 股市场的综合性股票交易工作站。

[English](./README.md)

StockTerminal 是一款集行情看盘、专业 K 线 / 分时图表、多因子选股、事件驱动回测、参数优化、模拟交易与 AI 辅助信号分析于一体的桌面软件。

项目使用现代 C++ 与 Qt 构建，采用分层服务架构，免费数据源自动容灾切换，适合量化交易学习与 A 股日常研究。

---

## ✨ 功能特性

### 📈 行情与看盘
- 通达信（TDX）直连行情，腾讯 / AKShare 作为备用数据源
- `MultiProvider` 多数据源自动容灾切换
- SQLite 本地缓存：日线 / 分钟线 / 基本面数据
- 全 A 股股票池（5000+ 只），支持代码、名称、全拼、拼音首字母搜索
- 市场面板：涨幅榜 / 跌幅榜 / 行业板块 / 概念板块
- 龙虎榜、融资融券数据
- 自选股、自定义指数、多窗口开图对比

### 📊 图表与技术分析
- 专业 K 线图，分层渲染系统
- 分时图与多周期切换
- 指数叠加 / 相对强弱对比
- 画线工具、区间统计、筹码分布、成交分布
- K 线 / 分时买卖箭头标记、模拟 / 实盘持仓成本线
- 悬停浮框、十字光标、交互式选区

### 🧪 量化研究
- 事件驱动回测引擎，与聚宽（JoinQuant）回测时序对齐：
  - 昨日收盘信号 → 今日开盘成交
  - A 股 T+1 规则
  - 100 股整手交易
  - 真实价模式 + 1 跳（0.01 元）滑点可选
- 内置策略模板：双均线、动量、突破、均值回归、RSI 等
- 多因子选股，内置 20+ 个因子（技术 / 估值 / 情绪）
- 网格搜索参数优化，支持热力图与结果导出
- 策略对比、压力测试、蒙特卡洛模拟、策略建议
- 绩效指标：年化收益、最大回撤、夏普、索提诺、卡玛、Alpha、Beta、VaR、胜率、波动率
- 沪深300 基准对比

### 🤖 AI 智能
- AI 综合信号：K 线形态 + 情绪 + 技术指标融合
- AI 选股，因子权重可配置
- 策略建议：过拟合 / 风险预警 + 参数优化建议

### 🛠 模拟交易与工具
- A 股 T+1 规则模拟交易
- 模拟账户停止 / 重启后状态持久化
- 交易日志体积控制与自动归档
- 定时任务：刷新行情 / 跑选股 / 抓数据 / 提醒
- 多面板 CSV 全量导出（UTF-8 BOM）
- 量化工作台：回测 / 参数优化 / 优化建议 / 选股 / 策略对比 / 压力测试 / 模拟交易

---

## 🏗 架构设计

```
UI → Intelligence → Engine → Core → Data → Foundation
```

依赖方向严格自上而下，层间通过纯虚接口耦合。

- **Foundation** — 基础类型、数据结构、工具类
- **Data** — 数据获取、存储、缓存
- **Core** — 事件总线、线程池、配置、日志
- **Engine** — 回测、模拟交易、选股、优化、分析
- **Intelligence** — AI 信号、形态识别、情绪分析、策略建议
- **UI** — Qt 桌面界面

Qt 主线程永不阻塞；耗时任务在 `ThreadPool` 中执行，通过 `EventBus` 回传结果。

---

## 🧰 技术栈

| 类别 | 选型 |
|------|------|
| 语言 | C++17 |
| UI | Qt 6.5+（Widgets, Sql, Network） |
| 构建 | CMake 3.21+ / Ninja / vcpkg |
| 存储 | SQLite（通过 Qt6::Sql） |
| 日志 | spdlog |
| JSON | nlohmann/json |
| HTTP | cpp-httplib |
| 技术分析 | TA-Lib |
| 测试 | GoogleTest |

---

## 📁 目录结构

```
StockTerminal/
├── CMakeLists.txt          # 根 CMake 构建
├── CMakePresets.json       # CMake presets
├── vcpkg.json              # vcpkg manifest
├── config/                 # 配置文件
├── docs/                   # 项目文档
├── examples/               # 示例策略
├── src/
│   ├── foundation/         # 基础类型 / 工具
│   ├── data/               # 数据访问层
│   ├── core/               # 核心服务
│   ├── engine/             # 业务引擎
│   ├── intelligence/       # AI 智能层
│   └── ui/                 # Qt UI
├── tests/                  # 单元测试
└── scripts/                # 构建 / 辅助脚本
```

---

## 🔨 构建指南

### 环境要求

- Windows 10/11，Visual Studio 2022（MSVC）
- CMake 3.21+
- Ninja
- vcpkg（manifest 模式）
- Qt 6.5+（当前 preset 使用 Qt 6.11.1 MSVC 2022）

### 快速开始（Windows）

```bat
:: 配置 + 构建 + 测试（无 Qt UI）
build.bat

:: 或手动执行：
cmake --preset default
cmake --build --preset default
ctest --preset default
```

### 带 Qt UI 构建

```bat
cmake --preset with-qt
cmake --build --preset with-qt
```

如果 Qt 安装在其他目录，请修改 `CMakePresets.json` 中的 `CMAKE_PREFIX_PATH`。

### Release 构建

```bat
cmake --preset release-qt
cmake --build --preset release-qt

:: 启动 Release 版本
run-release.bat
```

### 运行测试

```bat
ctest --preset default
```

当前项目包含 **484 个单元测试**，覆盖 Foundation、Core、Data、Engine、Intelligence 各层。

---

## 📚 文档

更多文档见 [`docs/`](./docs)：

- [架构设计](./docs/architecture.md)
- [需求规格](./docs/requirements.md)
- [技术栈](./docs/tech-stack.md)
- [编码规范](./docs/coding-standards.md)
- [CMake 构建指南](./docs/cmake-guide.md)
- [数据库设计](./docs/database-schema.md)
- [测试策略](./docs/testing-guide.md)
- [变更记录](./docs/changelog.md)
- [开发日志](./docs/DEVLOG.md)

---

## ⚠️ 免责声明

本项目仅供**学习与研究**使用，不构成任何投资建议。软件中的任何内容都不应被视为买入或卖出任何证券的推荐。历史收益不代表未来表现。使用风险自负。
