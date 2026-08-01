# 技术栈

| 类别 | 选择 | 版本 |
|------|------|------|
| 语言 | C++17/20 | C++17 基准，C++20 可选特性 |
| UI 框架 | Qt 6.5+ | Widgets, Sql, Network |
| 构建系统 | CMake 3.21+ | |
| 包管理器 | vcpkg | manifest mode |
| 数据存储 | SQLite | 通过 Qt6::Sql |
| 测试框架 | GoogleTest | |
| JSON | nlohmann/json | header-only |
| 日志 | spdlog | 异步输出 |
| HTTP | cpp-httplib | header-only |
| Python 桥接 | pybind11 | P9 阶段引入 |

## 编译器要求

- Windows: MSVC 2022 (Visual Studio 2022)
- Linux: GCC 11+ / Clang 14+
- macOS: Apple Clang 14+

## vcpkg 依赖

```json
{
  "dependencies": [
    {"name": "qt6", "features": ["widgets", "sql", "network"]},
    "nlohmann-json",
    "spdlog",
    "cpp-httplib",
    "gtest"
  ]
}
```

## 目录约定

```
src/foundation/  → 目标 st_foundation
src/data/        → 目标 st_data
src/core/        → 目标 st_core
src/engine/      → 目标 st_engine
src/ui/          → 目标 st_ui
src/main.cpp     → 可执行文件 StockTerminal
tests/           → 对应每个目标
```
