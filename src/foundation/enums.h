#pragma once

#include <cstdint>

namespace st {

enum class Market : uint8_t {
    Unknown = 0,
    SH,       // 上海 (Shanghai)
    SZ,       // 深圳 (Shenzhen)
    BJ,       // 北京 (Beijing)
    HK,       // 香港 (Hong Kong)
    US,       // 美国
};

enum class Direction : uint8_t {
    Buy = 0,
    Sell = 1,
};

enum class BarPeriod : uint8_t {
    Tick = 0,      // 逐笔
    Minute1 = 1,   // 1分钟
    Minute5 = 5,   // 5分钟
    Minute15 = 15, // 15分钟
    Minute30 = 30, // 30分钟
    Minute60 = 60, // 60分钟
    Daily = 100,   // 日线
    Weekly = 101,  // 周线
    Monthly = 102, // 月线
    Quarterly = 103, // 季线
    Yearly = 104,  // 年线
};

enum class OrderType : uint8_t {
    Market = 0,    // 市价单
    Limit = 1,     // 限价单
    Stop = 2,      // 止损单
    StopLimit = 3, // 限价止损单
};

enum class OrderStatus : uint8_t {
    Pending = 0,    // 待成交
    Partial = 1,    // 部分成交
    Filled = 2,     // 全部成交
    Cancelled = 3,  // 已撤销
    Rejected = 4,   // 已拒绝
};

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
};

enum class JournalType : uint8_t {
    AutoTrade = 0,   // 自动成交记录
    ManualNote = 1,  // 手动备注
    Signal = 2,      // 策略信号
};

enum class StrategyState : uint8_t {
    Uninitialized = 0,
    Initialized = 1,
    Running = 2,
    Paused = 3,
    Stopped = 4,
    Error = 5,
};

enum class NotificationLevel : uint8_t {
    Info = 0,
    Warning = 1,
    Alert = 2,
    Critical = 3,
};

} // namespace st
