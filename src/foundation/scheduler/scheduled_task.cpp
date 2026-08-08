#include "foundation/scheduler/scheduled_task.h"
#include "foundation/utils/datetime.h"

#include <ctime>
#include <cstdlib>

namespace st {

nlohmann::json ScheduledTask::toJson() const {
    return {
        {"id", id},
        {"type", static_cast<int>(type)},
        {"kind", static_cast<int>(kind)},
        {"timeOfDay", timeOfDay},
        {"intervalSeconds", intervalSeconds},
        {"target", target},
        {"enabled", enabled},
        {"lastResult", lastResult}
    };
}

ScheduledTask ScheduledTask::fromJson(const nlohmann::json& j) {
    ScheduledTask t;
    t.id            = j.value("id", std::string{});
    t.type          = static_cast<ScheduledTaskType>(j.value("type", 0));
    t.kind          = static_cast<ScheduleKind>(j.value("kind", 0));
    t.timeOfDay     = j.value("timeOfDay", std::string{});
    t.intervalSeconds = j.value("intervalSeconds", 0);
    t.target        = j.value("target", std::string{});
    t.enabled       = j.value("enabled", true);
    t.lastResult    = j.value("lastResult", std::string{});
    t.running       = j.value("running", false);
    return t;
}

bool shouldFire(const ScheduledTask& task, DateTime now, DateTime lastRun) {
    if (!task.enabled) return false;
    const auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                            now.time_since_epoch()).count();
    const auto lastSec = std::chrono::duration_cast<std::chrono::seconds>(
                             lastRun.time_since_epoch()).count();

    if (task.kind == ScheduleKind::Interval) {
        if (task.intervalSeconds <= 0) return false;
        return (nowSec - lastSec) >= task.intervalSeconds;
    }

    // Daily：解析 "HH:MM"，转当天秒数，比较是否到点且距上次 > 防重窗口
    const std::string& hhmm = task.timeOfDay;
    if (hhmm.size() != 5 || hhmm[2] != ':') return false;   // 严格 "HH:MM"，拒绝 "HH:MM:SS" 等
    const int hour = std::atoi(hhmm.substr(0, 2).c_str());
    const int min  = std::atoi(hhmm.substr(3, 2).c_str());
    if (hour < 0 || hour > 23 || min < 0 || min > 59) return false;

    // 本地时间当天秒数（localtime_s，与项目 timezone 惯例一致）
    std::tm tm{};
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    localtime_s(&tm, &tt);
    const int daySeconds = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    const int targetSeconds = hour * 3600 + min * 60;

    if (daySeconds < targetSeconds) return false;   // 未到点
    // 已到点：距上次执行需超过 60 秒（防重复触发同一天任务）
    return (nowSec - lastSec) >= 60;
}

} // namespace st
