#include "core/task_scheduler.h"
#include "foundation/utils/datetime.h"
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace st {

/// 当天 23:59:59（本地时间）转为 DateTime，用于 Daily 任务同日去重
/// Daily 任务触发后 lastRun_ 置为当天结束时刻，shouldFire 的 60s 反抖到第二天才满足
static DateTime endOfToday() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);
    tm.tm_hour = 23;
    tm.tm_min  = 59;
    tm.tm_sec  = 59;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

/// Daily 任务的本地当前时刻是否已过目标时间（今天不会再触发）
/// 用于 setTasks 初始化：已错过的 Daily 任务设 lastRun=endOfToday 避免启动后意外触发
static bool dailyTimePassed(const ScheduledTask& t, DateTime now) {
    const std::string& hhmm = t.timeOfDay;
    if (hhmm.size() != 5 || hhmm[2] != ':') return false;   // 非法时间按未错过处理
    const int targetSec = std::atoi(hhmm.substr(0, 2).c_str()) * 3600
                        + std::atoi(hhmm.substr(3, 2).c_str()) * 60;
    std::tm tm{};
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    localtime_s(&tm, &tt);
    const int daySec = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return daySec > targetSec;   // 严格大于：恰好到点不算错过（还会触发）
}

TaskScheduler::TaskScheduler(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setInterval(10000);   // 10 秒 tick
    connect(timer_, &QTimer::timeout, this, &TaskScheduler::onTick);
}

TaskScheduler::~TaskScheduler() { stop(); }

void TaskScheduler::setTasks(std::vector<ScheduledTask> tasks) {
    tasks_ = std::move(tasks);
    // 初始化 lastRun_：新建任务不立即触发
    for (const auto& t : tasks_) {
        if (lastRun_.count(t.id)) continue;
        const auto now = utils::now();
        if (t.kind == ScheduleKind::Daily && dailyTimePassed(t, now)) {
            // Daily 任务今天已过目标时间（启动在目标点之后）：设 lastRun 为当天结束，
            // 避免启动后 60 秒反抖动窗口一过就意外触发本应明天才执行的任务。
            lastRun_[t.id] = endOfToday();
        } else {
            lastRun_[t.id] = now;
        }
    }
}

std::string TaskScheduler::addTask(const ScheduledTask& t) {
    tasks_.push_back(t);
    lastRun_[t.id] = utils::now();
    if (onTasksChanged_) onTasksChanged_();
    return t.id;
}

bool TaskScheduler::updateTask(const std::string& id, const ScheduledTask& t) {
    if (t.id != id) return false;   // 不允许修改任务 ID（否则 lastRun_ 索引错乱）
    for (auto& it : tasks_) {
        if (it.id == id) {
            // I4: 仅调度相关字段变更才重置 lastRun_，否则编辑内容/enabled 等
            //     会重置计时导致错过当日触发点。
            const bool scheduleChanged =
                it.kind != t.kind ||
                it.timeOfDay != t.timeOfDay ||
                it.intervalSeconds != t.intervalSeconds;
            it = t;
            if (scheduleChanged) lastRun_[id] = utils::now();
            if (onTasksChanged_) onTasksChanged_();
            return true;
        }
    }
    return false;
}

bool TaskScheduler::removeTask(const std::string& id) {
    auto it = std::find_if(tasks_.begin(), tasks_.end(),
                           [&](const ScheduledTask& t) { return t.id == id; });
    if (it == tasks_.end()) return false;
    tasks_.erase(it);
    lastRun_.erase(id);
    if (onTasksChanged_) onTasksChanged_();
    return true;
}

void TaskScheduler::runNow(const std::string& id) {
    for (auto& t : tasks_) {
        if (t.id == id && !t.running) {
            // I1: Daily 任务触发后 lastRun_ 置为当天结束，防同日重复触发
            if (t.kind == ScheduleKind::Daily) {
                lastRun_[id] = endOfToday();
            } else {
                lastRun_[id] = utils::now();
            }
            t.running = true;
            if (executor_) executor_(t);
            t.running = false;
            return;
        }
    }
}

DateTime TaskScheduler::lastRunFor(const std::string& id) const {
    auto it = lastRun_.find(id);
    return it == lastRun_.end() ? utils::now() : it->second;
}

void TaskScheduler::start() { if (!started_) { started_ = true; timer_->start(); } }
void TaskScheduler::stop() { if (started_) { started_ = false; timer_->stop(); } }

void TaskScheduler::onTick() {
    const auto now = utils::now();
    for (auto& t : tasks_) {
        if (!t.enabled || t.running) continue;
        if (shouldFire(t, now, lastRunFor(t.id))) {
            // I1: Daily 任务触发后 lastRun_ 置为当天结束，防同日重复触发
            // shouldFire 的 60s 仅反抖动，本层保证 Daily 任务当天首次触发后不再触发
            if (t.kind == ScheduleKind::Daily) {
                lastRun_[t.id] = endOfToday();
            } else {
                lastRun_[t.id] = now;
            }
            if (executor_) {
                t.running = true;
                executor_(t);
                t.running = false;
            }
        }
    }
}

} // namespace st
