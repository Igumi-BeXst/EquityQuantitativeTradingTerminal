#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace st {

// Core type aliases
using Price = double;
using Volume = int64_t;
using Amount = double;    // 成交额
using Ratio = double;     // 比例 0.0 ~ 1.0
using Percentage = double; // 百分比值

// DateTime: seconds since epoch (consistent with Unix timestamp)
using Timestamp = int64_t;
using DateTime = std::chrono::system_clock::time_point;

// String types
using String = std::string;

// ID types
using StrategyId = std::string;
using OrderId = std::string;
using TradeId = std::string;
using JournalId = std::string;

// Constants
constexpr Price kInvalidPrice = -1.0;
constexpr Volume kInvalidVolume = -1;
constexpr Timestamp kInvalidTimestamp = -1;

} // namespace st
