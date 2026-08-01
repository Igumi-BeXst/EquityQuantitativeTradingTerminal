# C++ 编码规范

## 命名约定

| 元素 | 规范 | 示例 |
|------|------|------|
| 命名空间 | lowercase | `namespace st {}` |
| 类/结构体 | PascalCase | `StockCode`, `FeeCalculator` |
| 接口类 | PascalCase + I 前缀 | `IDataProvider`, `IStrategy` |
| 函数/方法 | camelCase | `loadBars()`, `toDateString()` |
| 成员变量 | camelCase + 后缀 `_` | `market_`, `avgCost_` |
| 常量 | kPascalCase | `kInvalidPrice` |
| 枚举值 | PascalCase | `Market::SH`, `BarPeriod::Daily` |
| 头文件 | snake_case | `stock_code.h`, `fee_calculator.h` |
| 源文件 | snake_case | `stock_code.cpp` |

## 头文件

```cpp
#pragma once  // 统一使用，不用 include guards

#include <标准库>    // 标准库
#include <第三方库>   // 第三方库
#include "项目头文件"  // 项目头文件（相对路径，从 src/ 起）
```

- 头文件自包含：每个 .h 单独 `#include` 应能编译
- 头文件不放 using namespace

## 类设计

- 接口用纯虚类 (`class IDataProvider`)
- 数据用 struct (公开成员): `Bar`, `Order`, `Position`
- 行为用 class (私有成员): `BacktestEngine`, `ConfigManager`
- Rule of Five: 明确需要则显式定义，不需要则 `= default`
- 优先值语义，需要多态才用指针

## 错误处理

- Foundation/Data/Core 层: 返回值（`std::optional`, `bool`, 错误码）
- Engine 层: 返回值 + EventBus 错误事件
- UI 层: Qt 信号 + 日志
- 不使用异常（保持对 Qt 的一致性）

## 注释

- 公共接口用 `///` doxygen 风格简要说明
- 复杂逻辑用 `//` 行注释解释意图
- 不注释显而易见的代码

## 格式化

- 4 空格缩进，不用 Tab
- 行宽上限 120 字符
- `{` 不另起一行（K&R 风格）
- 文件末尾空行
