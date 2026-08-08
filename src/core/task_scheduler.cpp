#include "core/task_scheduler.h"
#include "foundation/utils/datetime.h"
#include <QTimer>
#include <algorithm>
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

TaskScheduler::TaskScheduler(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setInterval(10000);   // 10 秒 tick
    connect(timer_, &QTimer::timeout, this, &TaskScheduler::onTick);
}

TaskScheduler::~TaskScheduler() { stop(); }

void TaskScheduler::setTasks(std::vector<ScheduledTask> tasks) {
    tasks_ = std::move(tasks);
    // 初始化 lastRun_：新建任务不立即触发（Daily 任务建在 15:00 会在 15:05 首次触发前 5 分钟窗口内）
    for (const auto& t : tasks_) {
        if (!lastRun_.count(t.id)) lastRun_[t.id] = utils::now();
    }
}

std::string TaskScheduler::addTask(const ScheduledTask& t) {
    tasks_.push_back(t);
    lastRun_[t.id] = utils::now();
    if (onTasksChanged_) onTasksChanged_();
    return t.id;
}

bool TaskScheduler::updateTask(const std::string& id, const ScheduledTask& t) {
    for (auto& it : tasks_) {
        if (it.id == id) {
            it = t;
            lastRun_[id] = utils::now();   // 重置防重计时（改动后重新计时）
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
