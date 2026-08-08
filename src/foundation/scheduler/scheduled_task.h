#pragma once

#include "foundation/types.h"
#include <string>
#include <nlohmann/json.hpp>

namespace st {

/// 任务类型
enum class ScheduledTaskType : uint8_t {
    RefreshQuotes = 0,  // 刷新行情（市场+板块）
    RunScreener  = 1,   // 跑选股
    FetchData    = 2,   // 抓数据（范围内全部股票日线）
    Remind       = 3,   // 提醒
};

/// 触发方式
enum class ScheduleKind : uint8_t {
    Daily    = 0,   // 每天固定时间 "HH:MM"
    Interval = 1,   // 每 N 秒
};

/// 定时任务
struct ScheduledTask {
    std::string  id;
    ScheduledTaskType type = ScheduledTaskType::Remind;
    ScheduleKind kind = ScheduleKind::Daily;
    std::string  timeOfDay;         // Daily: "HH:MM"
    int         intervalSeconds = 0; // Interval: 周期（秒）
    std::string  target;            // JSON：选股范围/股票列表/提醒文本
    bool        enabled = true;
    std::string  lastResult;        // 上次执行结果
    bool        running = false;    // 执行中（防重入）

    /// 序列化（nlohmann）
    nlohmann::json toJson() const;
    static ScheduledTask fromJson(const nlohmann::json& j);
};

/// 调度判定纯函数 — 可单测，不依赖 Qt
/// now/lastRun 为 DateTime（UTC 时间戳）；返回是否应触发
bool shouldFire(const ScheduledTask& task, DateTime now, DateTime lastRun);

} // namespace st
