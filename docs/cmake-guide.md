# CMake 构建指南

## 前置条件
1. CMake 3.21+
2. vcpkg (设置 VCPKG_ROOT 环境变量)
3. MSVC 2022 或 GCC 11+

## Quick Start
```bash
git clone <repo>
cd StockTerminal
vcpkg install   # 首次
cmake --preset default
cmake --build --preset default
ctest --preset default
./build/src/StockTerminal
```

## CMake Presets
- `default`: Debug 构建
- `release`: Release 构建
