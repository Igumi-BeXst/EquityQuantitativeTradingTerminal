# StockTerminal

> A comprehensive stock trading workstation for the A-share (China) market.

[简体中文](./README_zh-CN.md)

StockTerminal is an all-in-one desktop application that combines live market watching, professional K-line / intraday charting, multi-factor stock screening, event-driven backtesting, parameter optimization, simulated trading, and AI-assisted signal analysis.

Built with modern C++ and Qt, it uses a layered service architecture and connects to free data sources with automatic failover, making it suitable for both learning quantitative trading and daily A-share research.

---

## ✨ Features

### 📈 Market Data & Watching
- Direct connection to TDX (通达信) market data, with Tencent and AKShare as fallback sources
- `MultiProvider` automatic failover between data sources
- Local SQLite cache for daily/minute bars and fundamental data
- Full A-share stock pool (5,000+ stocks) with search by code, name, full pinyin, or pinyin initials
- Market overview tabs: gainers, losers, industry sectors, concept sectors
- Dragon-tiger list (龙虎榜) and margin trading (融资融券) data
- Watchlist, custom indices, and multi-window chart comparison

### 📊 Charts & Technical Analysis
- Professional K-line chart with a layered rendering system
- Intraday (分时) chart and multiple period switching
- Index overlay / relative strength comparison
- Drawing tools, range statistics, chip distribution, and transaction distribution
- Trade markers on K-line / intraday charts, holding-cost lines for paper/real positions
- Hover tooltips, crosshair, and interactive selection

### 🧪 Quantitative Research
- Event-driven backtest engine aligned with JoinQuant (聚宽) conventions:
  - Previous-day close signal → today open execution
  - T+1 trading rule
  - A-share 100-share board lots
  - Real-price mode with 1-tick (0.01) slippage option
- Built-in strategy templates: MA cross, momentum, breakout, mean reversion, RSI, etc.
- Multi-factor stock screener with 20+ built-in factors (technical, valuation, sentiment)
- Grid-search parameter optimization with heatmap view and result export
- Strategy comparison, stress testing, Monte Carlo simulation, and strategy advisor
- Performance metrics: annualized return, max drawdown, Sharpe, Sortino, Calmar, Alpha, Beta, VaR, win rate, volatility
- Benchmark comparison against CSI 300 (沪深300)

### 🤖 AI & Intelligence
- Composite AI signal: candlestick pattern + sentiment + technical indicators
- AI-powered stock screening with configurable factor weights
- Strategy advisor with overfitting / risk warnings and refined parameter suggestions

### 🛠 Simulated Trading & Tools
- Simulated paper trading with A-share T+1 rules
- Account state persistence across stop / restart
- Trade journal with configurable log-size control and archival
- Scheduled tasks for quote refresh, screening, data fetching, and reminders
- Full CSV export (UTF-8 BOM) for multiple panels
- Quant workbench with backtest / optimization / advisor / screener / comparison / stress-test / paper-trade tabs

---

## 🏗 Architecture

```
UI → Intelligence → Engine → Core → Data → Foundation
```

Dependencies flow strictly from top to bottom, and layers are coupled through pure virtual interfaces.

- **Foundation** — basic types, data structures, utilities
- **Data** — data fetching, storage, caching
- **Core** — event bus, thread pool, configuration, logging
- **Engine** — backtest, paper trading, screening, optimization, analysis
- **Intelligence** — AI signals, pattern recognition, sentiment analysis, advisor
- **UI** — Qt desktop interface

The Qt main thread never blocks; long-running tasks run in a `ThreadPool` and return results through the `EventBus`.

---

## 🧰 Tech Stack

| Category | Choice |
|----------|--------|
| Language | C++17 |
| UI | Qt 6.5+ (Widgets, Sql, Network) |
| Build | CMake 3.21+ / Ninja / vcpkg |
| Storage | SQLite (via Qt6::Sql) |
| Logging | spdlog |
| JSON | nlohmann/json |
| HTTP | cpp-httplib |
| Technical Analysis | TA-Lib |
| Testing | GoogleTest |

---

## 📁 Project Layout

```
StockTerminal/
├── CMakeLists.txt          # Root CMake build
├── CMakePresets.json       # CMake presets
├── vcpkg.json              # vcpkg manifest
├── config/                 # Configuration files
├── docs/                   # Project documentation
├── examples/               # Example strategies
├── src/
│   ├── foundation/         # Base types / utilities
│   ├── data/               # Data access layer
│   ├── core/               # Core services
│   ├── engine/             # Business engines
│   ├── intelligence/       # AI / intelligence layer
│   └── ui/                 # Qt UI
├── tests/                  # Unit tests
└── scripts/                # Build / helper scripts
```

---

## 🔨 Build

### Prerequisites

- Windows 10/11, Visual Studio 2022 (MSVC)
- CMake 3.21+
- Ninja
- vcpkg (manifest mode)
- Qt 6.5+ (the current presets use Qt 6.11.1 MSVC 2022)

### Quick Start (Windows)

```bat
:: Configure + build + test (headless, no Qt UI)
build.bat

:: Or manually:
cmake --preset default
cmake --build --preset default
ctest --preset default
```

### Build with Qt UI

```bat
cmake --preset with-qt
cmake --build --preset with-qt
```

If Qt is installed in a different location, update `CMAKE_PREFIX_PATH` in `CMakePresets.json`.

### Release Build

```bat
cmake --preset release-qt
cmake --build --preset release-qt

:: Launch the release build
run-release.bat
```

### Run Tests

```bat
ctest --preset default
```

The project currently contains **484 unit tests** covering Foundation, Core, Data, Engine, and Intelligence layers.

---

## 📚 Documentation

More details are available in [`docs/`](./docs):

- [Architecture](./docs/architecture.md)
- [Requirements](./docs/requirements.md)
- [Tech Stack](./docs/tech-stack.md)
- [Coding Standards](./docs/coding-standards.md)
- [CMake Guide](./docs/cmake-guide.md)
- [Database Schema](./docs/database-schema.md)
- [Testing Guide](./docs/testing-guide.md)
- [Changelog](./docs/changelog.md)
- [Development Log](./docs/DEVLOG.md)

---

## ⚠️ Disclaimer

This project is for **educational and research purposes only**. It is not financial advice, and nothing in this software should be considered a recommendation to buy or sell any security. Past performance does not guarantee future results. Use at your own risk.
