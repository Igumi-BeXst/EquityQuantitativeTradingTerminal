#pragma once

#include "foundation/scheduler/scheduled_task.h"
#include <QObject>
#include <functional>
#include <map>
#include <vector>

class QTimer;

namespace st {

/// 定时任务调度器 — QTimer 驱动（UI 层）
/// 每 10 秒 tick 检查任务，到点通过执行器回调执行动作
class TaskScheduler : public QObject {
    Q_OBJECT
public:
    explicit TaskScheduler(QObject* parent = nullptr);
    ~TaskScheduler() override;

    /// 任务执行器 — 由 UI 层注入（解耦调度与动作）
    /// 非 const 引用：调度器在调用前后切换 running 防重入
    using Executor = std::function<void(ScheduledTask&)>;
    void setExecutor(Executor exec) { executor_ = std::move(exec); }

    /// 载入任务（启动时）
    void setTasks(std::vector<ScheduledTask> tasks);
    const std::vector<ScheduledTask>& tasks() const { return tasks_; }

    /// CRUD（内部：增删改后调 onTasksChanged_ 通知 UI 持久化）
    std::string addTask(const ScheduledTask& t);
    bool updateTask(const std::string& id, const ScheduledTask& t);
    bool removeTask(const std::string& id);

    /// 立即执行某个任务
    void runNow(const std::string& id);

    /// 启动/停止 tick
    void start();
    void stop();

    /// 上次执行时间表（id → DateTime，防重入/防重复触发）
    DateTime lastRunFor(const std::string& id) const;

    /// 数据变化回调（UI 层绑定→保存 JSON）
    void setOnTasksChanged(std::function<void()> cb) { onTasksChanged_ = std::move(cb); }

private:
    void onTick();

    QTimer* timer_ = nullptr;
    std::vector<ScheduledTask> tasks_;
    std::map<std::string, DateTime> lastRun_;   // id → 上次触发时间
    Executor executor_;
    std::function<void()> onTasksChanged_;
    bool started_ = false;
};

} // namespace st
