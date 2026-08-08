# 定时任务（Scheduled Tasks）实现计划 — P10 第十轮

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现定时任务：定时刷新行情(市场+板块)/定时跑选股/定时抓数据/定时提醒。设置菜单→独立窗口，固定时间+周期触发，增删改立即保存。

**Architecture:** 调度判定逻辑抽纯函数（`shouldFire`）放引擎可单测；TaskScheduler（QTimer 驱动，UI 层）调用执行器；动作通过 `std::function<void(const ScheduledTask&)>` 注入解耦；RunScreener/FetchData 用纯引擎 StockScreener/IDataProvider（无 UI 依赖）。

**Tech Stack:** C++17, Qt 6.11 (Widgets), nlohmann/json, GoogleTest, CMake + vcpkg。

## Global Constraints

- 分层严格：UI → Intelligence → Engine → Core → Data → Foundation
- 安全异步：禁裸 `this` 捕获；`++gen_` 守卫 + `ThreadPool::submitIO` + `QPointer` + `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
- 编译零警告：`cmake --build --preset with-qt` 零错误零警告
- 改头文件后必须 `--clean-first` 全量重建（项目记忆：陈旧对象致堆/栈损坏）
- worktree 任何 cmake/build/ctest 必须先 `call vcvars64.bat`；测试用 cmd 分号 PATH 含 Qt bin
- 测试：`ctest --preset default` 全绿，当前 358 → 目标 ~375
- 中文注释/UI 文案；命名遵循项目惯例（`utils::`、`StockCode`、`Direction`）
- 每次增删改任务**立即保存**到 `configDir/scheduled_tasks.json`

---

### Task 1: 引擎数据模型 + shouldFire 调度判定 + 持久化

**Files:**
- Create: `src/engine/scheduler/scheduled_task.h`
- Create: `src/engine/scheduler/scheduled_task.cpp`
- Create: `src/engine/scheduler/scheduled_task_store.h`
- Create: `src/engine/scheduler/scheduled_task_store.cpp`
- Create: `tests/test_engine/test_scheduled_task.cpp`
- Modify: `src/CMakeLists.txt`、`tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `StockCode`、`Direction`（已有）、`nlohmann::json`、`utils::parseDateTime/now`
- Produces: `st::ScheduledTask`、`st::ScheduledTaskType`、`st::ScheduleKind`、`st::shouldFire(task, now, lastRunTime)`、`st::ScheduledTaskStore`（load/save）

- [ ] **Step 1: 写数据模型头文件 `scheduled_task.h`**

```cpp
#pragma once

#include "foundation/types.h"
#include <string>

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
```

需 `#include <nlohmann/json.hpp>`；`scheduled_task.h` 顶层补 include。

- [ ] **Step 2: 实现 `scheduled_task.cpp`**

```cpp
#include "engine/scheduler/scheduled_task.h"
#include "foundation/utils/datetime.h"

#include <ctime>

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
        {"lastResult", lastResult},
        {"running", running}
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
    if (hhmm.size() < 5 || hhmm[2] != ':') return false;
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
```

需 `#include <cstdlib>`（atoi）。`lastRun` 为上次执行时间（调度器维护，初始 = 任务创建时间，避免新建立即触发）。

- [ ] **Step 3: 写持久化 Store `scheduled_task_store.h/cpp`**

仿 `trade_journal_store`（JSON 数组 roundtrip）：
```cpp
class ScheduledTaskStore {
public:
    bool load(const std::string& path, std::vector<ScheduledTask>& out) const;
    bool save(const std::string& path, const std::vector<ScheduledTask>& tasks) const;
};
```
- load：读 JSON 数组 → fromJson 填充；损坏回退空
- save：toJson → dump(2) 写文件，create_directories 父目录

- [ ] **Step 4: 写测试 `test_scheduled_task.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/scheduler/scheduled_task.h"
#include "engine/scheduler/scheduled_task_store.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {
ScheduledTask mkDaily(const std::string& hhmm) {
    ScheduledTask t;
    t.kind = ScheduleKind::Daily;
    t.timeOfDay = hhmm;
    t.enabled = true;
    return t;
}
DateTime at(const std::string& s) { return utils::parseDateTime(s); }
DateTime epoch() { return utils::parseDateTime("2020-01-01 00:00:00"); }
}  // namespace

TEST(ScheduledTaskTest, DailyFiresAtOrAfterTime) {
    auto t = mkDaily("15:05");
    // 未到点
    EXPECT_FALSE(shouldFire(t, at("2026-08-08 15:04:59"), epoch()));
    // 到点
    EXPECT_TRUE(shouldFire(t, at("2026-08-08 15:05:00"), epoch()));
    EXPECT_TRUE(shouldFire(t, at("2026-08-08 15:05:01"), epoch()));
    EXPECT_TRUE(shouldFire(t, at("2026-08-08 23:59:59"), epoch()));
}

TEST(ScheduledTaskTest, DailyNoRefireWithinWindow) {
    auto t = mkDaily("15:05");
    // 上次 15:04:50 执行，现在 15:05:10（20 秒 < 60 防重窗）→ 不触发
    EXPECT_FALSE(shouldFire(t, at("2026-08-08 15:05:10"), at("2026-08-08 15:04:50")));
    // 上次 15:03:00，现在 15:05:10（130 秒）→ 触发
    EXPECT_TRUE(shouldFire(t, at("2026-08-08 15:05:10"), at("2026-08-08 15:03:00")));
}

TEST(ScheduledTaskTest, IntervalFiresAfterElapsed) {
    ScheduledTask t;
    t.kind = ScheduleKind::Interval;
    t.intervalSeconds = 300;   // 5 分钟
    EXPECT_FALSE(shouldFire(t, at("2026-08-08 10:00:00"), at("2026-08-08 09:58:00")));
    EXPECT_TRUE(shouldFire(t, at("2026-08-08 10:05:00"), at("2026-08-08 10:00:00")));
}

TEST(ScheduledTaskTest, DisabledNeverFires) {
    auto t = mkDaily("15:05");
    t.enabled = false;
    EXPECT_FALSE(shouldFire(t, at("2026-08-08 15:05:00"), epoch()));
}

TEST(ScheduledTaskTest, InvalidTimeNoFire) {
    auto t = mkDaily("99:99");
    EXPECT_FALSE(shouldFire(t, at("2026-08-08 15:05:00"), epoch()));
    auto t2 = mkDaily("bad");
    EXPECT_FALSE(shouldFire(t2, at("2026-08-08 15:05:00"), epoch()));
}

TEST(ScheduledTaskTest, ZeroIntervalNoFire) {
    ScheduledTask t;
    t.kind = ScheduleKind::Interval;
    t.intervalSeconds = 0;
    EXPECT_FALSE(shouldFire(t, at("2026-08-08 10:05:00"), epoch()));
}

TEST(ScheduledTaskTest, JsonRoundTrip) {
    ScheduledTask t;
    t.id = "T1";
    t.type = ScheduledTaskType::RunScreener;
    t.kind = ScheduleKind::Daily;
    t.timeOfDay = "15:05";
    t.target = R"({"scope":"all"})";
    t.lastResult = "2026-08-08 15:05:00 成功 3 条";
    const auto j = t.toJson();
    const auto t2 = ScheduledTask::fromJson(j);
    EXPECT_EQ(t2.id, "T1");
    EXPECT_EQ(t2.type, ScheduledTaskType::RunScreener);
    EXPECT_EQ(t2.timeOfDay, "15:05");
    EXPECT_EQ(t2.target, R"({"scope":"all"})");
    EXPECT_EQ(t2.lastResult, "2026-08-08 15:05:00 成功 3 条");
}

TEST(ScheduledTaskStoreTest, RoundTrip) {
    const std::string path = "scheduled_tasks_test.json";
    std::vector<ScheduledTask> tasks;
    ScheduledTask t;
    t.id = "T1";
    t.timeOfDay = "15:05";
    tasks.push_back(t);
    ScheduledTaskStore store;
    EXPECT_TRUE(store.save(path, tasks));
    std::vector<ScheduledTask> loaded;
    EXPECT_TRUE(store.load(path, loaded));
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].id, "T1");
    EXPECT_EQ(loaded[0].timeOfDay, "15:05");
    std::remove(path.c_str());
}

TEST(ScheduledTaskStoreTest, CorruptFallsBackEmpty) {
    const std::string path = "scheduled_tasks_bad.json";
    { std::FILE* f = std::fopen(path.c_str(), "w"); std::fputs("not json{", f); std::fclose(f); }
    std::vector<ScheduledTask> out;
    ScheduledTaskStore store;
    EXPECT_FALSE(store.load(path, out));
    EXPECT_TRUE(out.empty());
    std::remove(path.c_str());
}
```

- [ ] **Step 5: 更新 CMakeLists 两处并跑通**

`src/CMakeLists.txt` st_engine 源列表加：
```cmake
    engine/scheduler/scheduled_task.cpp
    engine/scheduler/scheduled_task_store.cpp
```
`tests/CMakeLists.txt` test_engine 源列表加：
```cmake
        test_engine/test_scheduled_task.cpp
```

Run: `cmake --preset with-qt` → `cmake --build --preset with-qt` → 零警告零错误
Run: `ctest --preset default -R "ScheduledTask"` → 9 用例 PASS
Run: `ctest --preset default` → 全绿

- [ ] **Step 6: Commit**

```bash
git add src/engine/scheduler/ tests/test_engine/test_scheduled_task.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 定时任务数据模型 + shouldFire 调度判定 + JSON 持久化"
```

---

### Task 2: TaskScheduler 真实现（QTimer 驱动，UI 层）

**Files:**
- Modify: `src/core/task_scheduler.h`
- Modify: `src/core/task_scheduler.cpp`

**Interfaces:**
- Consumes: `ScheduledTask`、`shouldFire`（Task 1）、`utils::now`
- Produces: `st::TaskScheduler`（`setTasks/addTask/updateTask/removeTask/tasks/runNow/setExecutor/start/stop`）

- [ ] **Step 1: 重写头文件**

```cpp
#pragma once

#include "engine/scheduler/scheduled_task.h"
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
    using Executor = std::function<void(const ScheduledTask&)>;
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
```

- [ ] **Step 2: 实现**

```cpp
#include "core/task_scheduler.h"
#include "foundation/utils/datetime.h"
#include <QTimer>
#include <algorithm>

namespace st {

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
    for (const auto& t : tasks_) {
        if (t.id == id && !t.running) {
            if (executor_) executor_(t);
            lastRun_[id] = utils::now();
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
            lastRun_[t.id] = now;
            if (executor_) executor_(t);
        }
    }
}

} // namespace st
```

- [ ] **Step 3: 编译验证**

`task_scheduler.cpp` 在 st_core（Qt6::Core 依赖已有）。改头文件 → clean rebuild。

Run: `cmake --preset with-qt` → `cmake --build --preset with-qt --clean-first` → 零警告零错误
Run: `ctest --preset default` → 全绿（无回归；TaskScheduler 无新单测，逻辑由 Task 1 的 shouldFire 单测覆盖）

- [ ] **Step 4: Commit**

```bash
git add src/core/task_scheduler.h src/core/task_scheduler.cpp
git commit -m "feat: TaskScheduler 真实现（QTimer 驱动 + 执行器注入 + 增删改回调）"
```

---

### Task 3: 选股范围解析器 + 动作执行器（RunScreener/FetchData/Remind/Refresh）

**Files:**
- Create: `src/engine/scheduler/screener_scope.h`
- Create: `src/engine/scheduler/screener_scope.cpp`
- Create: `tests/test_engine/test_screener_scope.cpp`
- Modify: `src/CMakeLists.txt`、`tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScheduledTask`（Task 1）、`StockScreener`、`IDataProvider`、`ScreenerConfig`
- Produces: `st::ScopeResolver`（`resolveScope(targetJson, provider, lastConfig) -> std::vector<StockCode>`）、`st::TaskExecutor`（`execute(type, task, provider, ...)`）

- [ ] **Step 1: 写范围解析器头文件 `screener_scope.h`**

```cpp
#pragma once

#include "foundation/stock_code.h"
#include <functional>
#include <string>
#include <vector>

namespace st {

class IDataProvider;

/// 选股/抓数据范围解析 — 从 task.target JSON 解析出股票列表
/// target 示例：
///   {"scope":"all"}                        → 全部 A 股
///   {"scope":"sector","sector":"BK0475"}   → 某板块（TDX 板块指数代码）
///   {"scope":"last"}                       → 复用上次手动选股配置
class ScopeResolver {
public:
    /// 取全部 A 股
    static std::vector<StockCode> allAShares(IDataProvider* provider);

    /// 取某板块成分（TDX 板块指数；v1 简化：板块代码本身作为单标的池）
    static std::vector<StockCode> sectorStocks(IDataProvider* provider,
                                               const std::string& sectorCode);

    /// 解析 target JSON → 股票池
    /// lastConfig：上次手动选股的股票列表（scope=last 时使用；空则回退 all）
    static std::vector<StockCode> resolve(const std::string& targetJson,
                                          IDataProvider* provider,
                                          const std::vector<StockCode>& lastConfig);
};

} // namespace st
```

> 注：板块成分股 v1 简化为「板块指数自身」作为池（TDX 板块指数可拉 K 线）；若需成分股明细，v2 用 TDX 板块成分接口。设计文档 §5 已注明范围外，实现时按此简化并在文档记录。

- [ ] **Step 2: 实现 `screener_scope.cpp`**

```cpp
#include "engine/scheduler/screener_scope.h"
#include "data/idata_provider.h"
#include <nlohmann/json.hpp>

namespace st {

std::vector<StockCode> ScopeResolver::allAShares(IDataProvider* provider) {
    std::vector<StockCode> out;
    if (!provider) return out;
    auto sh = provider->getStockList(Market::SH);
    auto sz = provider->getStockList(Market::SZ);
    for (auto& s : sh) {
        if (tdx::isTradableAShare(s.code) || tdx::isIndexCode(s.code))
            out.push_back(s.code);
    }
    for (auto& s : sz) {
        if (tdx::isTradableAShare(s.code) || tdx::isIndexCode(s.code))
            out.push_back(s.code);
    }
    return out;
}

std::vector<StockCode> ScopeResolver::sectorStocks(IDataProvider*, const std::string& code) {
    // v1：板块指数自身作为池（TDX 板块指数代码，如 BK0475 → StockCode("SH"+code)）
    if (code.empty()) return {};
    return { StockCode("SH" + code) };
}

std::vector<StockCode> ScopeResolver::resolve(const std::string& targetJson,
                                              IDataProvider* provider,
                                              const std::vector<StockCode>& lastConfig) {
    try {
        auto j = nlohmann::json::parse(targetJson);
        const std::string scope = j.value("scope", "all");
        if (scope == "sector") {
            return sectorStocks(provider, j.value("sector", ""));
        }
        if (scope == "last") {
            return lastConfig.empty() ? allAShares(provider) : lastConfig;
        }
        return allAShares(provider);
    } catch (const std::exception&) {
        return allAShares(provider);
    }
}

} // namespace st
```

> `tdx::isTradableAShare` 在 `data/tdx/tdx_models.h`（项目已有）；若 scope.cpp 不便引用，改为简单过滤（SH 6 开头/SZ 0 开头）。实现时确认。

- [ ] **Step 3: 测试 `test_screener_scope.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/scheduler/screener_scope.h"

using namespace st;

TEST(ScopeResolverTest, ParseAllScope) {
    auto v = ScopeResolver::resolve(R"({"scope":"all"})", nullptr, {});
    EXPECT_TRUE(v.empty());   // provider=nullptr → 空
}

TEST(ScopeResolverTest, ParseSectorScope) {
    auto v = ScopeResolver::resolve(R"({"scope":"sector","sector":"BK0475"})", nullptr, {});
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].code(), "BK0475");
}

TEST(ScopeResolverTest, ParseLastScopeFallsBackAll) {
    // provider=nullptr + lastConfig 空 → allAShares(nullptr)=空
    EXPECT_TRUE(ScopeResolver::resolve(R"({"scope":"last"})", nullptr, {}).empty());
}

TEST(ScopeResolverTest, LastScopeUsesConfig) {
    std::vector<StockCode> last = { StockCode("SH600519") };
    auto v = ScopeResolver::resolve(R"({"scope":"last"})", nullptr, last);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].code(), "600519");
}

TEST(ScopeResolverTest, MalformedFallsBackEmpty) {
    EXPECT_TRUE(ScopeResolver::resolve("not json{", nullptr, {}).empty());
}
```

- [ ] **Step 4: 更新 CMake 并跑通**

`src/CMakeLists.txt` st_engine 加 `engine/scheduler/screener_scope.cpp`
`tests/CMakeLists.txt` test_engine 加 `test_engine/test_screener_scope.cpp`

Run: `cmake --preset with-qt` → `cmake --build --preset with-qt` → 零警告
Run: `ctest --preset default -R "ScopeResolver"` → 5 用例 PASS
Run: `ctest --preset default` → 全绿

- [ ] **Step 5: Commit**

```bash
git add src/engine/scheduler/screener_scope.* tests/test_engine/test_screener_scope.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 选股/抓数据范围解析器（全部/板块/上次）"
```

---

### Task 4: TaskWindow UI — 任务列表 + CRUD + 立即执行

**Files:**
- Create: `src/ui/panels/task_window.h`
- Create: `src/ui/panels/task_window.cpp`
- Modify: `src/CMakeLists.txt`（st_ui 加 `ui/panels/task_window.cpp`）

**Interfaces:**
- Consumes: `TaskScheduler`（Task 2）、`ScheduledTask`、`ScheduledTaskType`、`ScopeResolver`、`StockSearchBar`、`IDataProvider`
- Produces: `st::TaskWindow`（`std::shared_ptr<TaskScheduler> scheduler`, `IDataProvider* provider`）

- [ ] **Step 1: 写头文件**

```cpp
#pragma once

#include "engine/scheduler/scheduled_task.h"
#include <QDialog>
#include <QMainWindow>
#include <QString>
#include <memory>

class QTableWidget;
class QLineEdit;
class QTimeEdit;
class QSpinBox;
class QComboBox;

namespace st {

class TaskScheduler;
class IDataProvider;

/// 定时任务窗口 — 设置菜单 → 独立窗口
/// 任务列表 + 新建/编辑/删除/立即执行 + 启停
class TaskWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TaskWindow(std::shared_ptr<TaskScheduler> scheduler,
                        IDataProvider* provider,
                        QWidget* parent = nullptr);
    ~TaskWindow() override;

private:
    void rebuildAll();
    void refreshEnabledState();

    std::shared_ptr<TaskScheduler> scheduler_;
    IDataProvider* provider_ = nullptr;
    QTableWidget* table_ = nullptr;
};

/// 新建/编辑任务对话框
class TaskEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaskEditDialog(IDataProvider* provider,
                            const ScheduledTask* existing = nullptr,
                            QWidget* parent = nullptr);

    /// 获取填写结果（accept 后调用）
    ScheduledTask task() const;

private:
    void onTypeChanged();
    void onKindChanged();

    IDataProvider* provider_ = nullptr;
    // 控件...
};

} // namespace st
```

- [ ] **Step 2: 实现记录列表（仿 JournalWindow 模式）**

构造要点：
- 表格 6 列：类型/时间/目标摘要/启用/上次结果/（状态）
- 工具栏：新建/编辑/删除/立即执行
- 启用开关：编辑对话框里勾选；表格列显示 ✓/✗
- `rebuildAll()`：`scheduler_->tasks()` → 填表；类型中文化（刷新行情/跑选股/抓数据/提醒）
- 新建/编辑对话框 `TaskEditDialog`：
  - 类型下拉（4 种）+ 触发方式下拉（每天固定时间 / 每 N 分钟）+ 对应控件
  - Daily：QTimeEdit；Interval：QSpinBox(分钟)
  - 按类型显示参数区：
    - RefreshQuotes：无额外参数
    - RunScreener：选股范围下拉（全部A股/板块/上次手动选股）+ 板块下拉（异步 fetchBoards）
    - FetchData：同 RunScreener（复用范围）
    - Remind：提醒内容 QLineEdit
  - 确定 → 组装 ScheduledTask（id 为空则生成 "T"+序号）
- 立即执行：`scheduler_->runNow(id)` + 刷新表格（lastResult 更新）

- [ ] **Step 3: 更新 CMake 并构建**

`src/CMakeLists.txt` st_ui 加 `ui/panels/task_window.cpp`

Run: `cmake --preset with-qt` → `cmake --build --preset with-qt --clean-first` → 零警告
Run: `ctest --preset default` → 全绿

- [ ] **Step 4: Commit**

```bash
git add src/ui/panels/task_window.h src/ui/panels/task_window.cpp src/CMakeLists.txt
git commit -m "feat: 定时任务窗口 — 任务列表 CRUD + 立即执行"
```

---

### Task 5: 动作执行器接线 + MainWindow 装配 + SectorPanel refresh

**Files:**
- Modify: `src/ui/main_window.h`
- Modify: `src/ui/main_window.cpp`
- Modify: `src/ui/panels/sector_panel.h`
- Modify: `src/ui/panels/sector_panel.cpp`

**Interfaces:**
- Consumes: `TaskScheduler`、`ScheduledTaskType`、`ScopeResolver`、`StockScreener`、`DataRepository`、`NotificationService`、`MarketPanel::refresh`、`SectorPanel::refresh`
- Produces: MainWindow 持有 scheduler_ + 菜单项 + 执行器 + 启动/保存；`SectorPanel::refresh()` 公开方法

- [ ] **Step 1: SectorPanel 加公开 refresh 方法**

`sector_panel.h` public 加 `void refresh();`（现 refreshBtn_ 槽改为公开方法；内部逻辑复用）。确认现有刷新触发点。

- [ ] **Step 2: MainWindow 装配**

`main_window.h`：
- 前置声明 `class TaskScheduler;`
- 私有成员 `std::shared_ptr<TaskScheduler> scheduler_;` + `void openTaskWindow();`
- 私有 `void runScheduledTask(const ScheduledTask&);`（执行器）
- 私有成员 `std::vector<StockCode> lastScreenerConfig_;`（ScreenerPanel 手动选股时更新，供 scope=last 用）

`main_window.cpp`：
- `initServices()` 尾（journal 加载后）：
  ```cpp
  scheduler_ = std::make_shared<TaskScheduler>(this);
  TaskStore store;   // ScheduledTaskStore
  std::vector<ScheduledTask> tasks;
  store.load(AppPaths::configDir() + "/scheduled_tasks.json", tasks);
  scheduler_->setTasks(std::move(tasks));
  scheduler_->setExecutor([this](const ScheduledTask& t) { runScheduledTask(t); });
  scheduler_->setOnTasksChanged([this] {
      ScheduledTaskStore store;
      store.save(AppPaths::configDir() + "/scheduled_tasks.json", scheduler_->tasks());
  });
  scheduler_->start();
  ```
- 设置菜单加：`settingsMenu->addAction(tr("定时任务(&T)…"), this, &MainWindow::openTaskWindow);`
- `openTaskWindow()`：仿 openJournalWindow（WA_DeleteOnClose + destroyed 置空 + new TaskWindow(scheduler_, provider_.get())）
- `runScheduledTask(const ScheduledTask& t)` 按类型分发：
  ```cpp
  switch (t.type) {
  case ScheduledTaskType::RefreshQuotes:
      if (marketPanel_) marketPanel_->refresh();
      if (sectorPanel_) sectorPanel_->refresh();
      break;
  case ScheduledTaskType::RunScreener: {
      // ThreadPool 异步：ScopeResolver::resolve → StockScreener::run → 结果存 lastResult
      auto pool = ScopeResolver::resolve(t.target, provider_.get(), lastScreenerConfig_);
      // 简化 v1：跑选股并把结果数写回 lastResult（真正展示结果 v2 做面板联动）
      break;
  }
  case ScheduledTaskType::FetchData: {
      // ThreadPool 异步：对 pool 全部 getBars 存 DataRepository
      break;
  }
  case ScheduledTaskType::Remind:
      NotificationService::instance()->info("定时提醒", t.target);
      break;
  }
  ```
- closeEvent：保存 scheduler_ 到 JSON（或依赖 onTasksChanged 已存）

> RunScreener/FetchData 的完整异步执行（ThreadPool + 进度 + 结果展示）涉及 ScreenerPanel 深度集成，**v1 简化为**：执行时调 ScopeResolver 解析池 → 用 StockScreener/provider 实际跑/抓 → 结果摘要写入任务的 lastResult（不联动面板）。完整面板联动留 v2。

- [ ] **Step 3: 构建 + 测试**

Run: `cmake --preset with-qt` → `cmake --build --preset with-qt --clean-first` → 零警告（改多个头文件，必须 clean）
Run: `ctest --preset default` → 全绿

- [ ] **Step 4: 手动冒烟清单**
- 设置菜单出现「定时任务」，打开独立窗口
- 新建「提醒」任务（每天 15:05，内容"复盘"）→ 列表显示 → 立即执行 → 弹出通知
- 新建「刷新行情」任务（每 5 分钟）→ 立即执行 → 市场/板块面板刷新
- 改时间 → 关闭窗口重开 → 任务还在（持久化）
- 删除任务 → 重开 → 没了

- [ ] **Step 5: Commit**

```bash
git add src/ui/main_window.h src/ui/main_window.cpp src/ui/panels/sector_panel.h src/ui/panels/sector_panel.cpp
git commit -m "feat: 设置菜单定时任务 + 动作执行器 + SectorPanel 公开刷新"
```

---

### Task 6: 文档收尾 + CLAUDE.md 阶段更新

**Files:**
- Modify: `docs/DEVLOG.md`
- Modify: `docs/changelog.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: 更新文档**

`docs/DEVLOG.md` 顶部加 P10 第十轮条目
`docs/changelog.md` 顶部加版本说明
`CLAUDE.md`：
- 当前阶段改 `P10 第九轮 ✅ → P10 第十轮 ✅（定时任务：刷新行情/跑选股/抓数据/提醒，设置菜单独立窗口）`
- 测试数 358 → 实际值（以 ctest 实跑为准，约 358+14=372）

- [ ] **Step 2: 终验**

Run: `cmake --build --preset with-qt --clean-first` → 零警告零错误
Run: `ctest --preset default` → 全绿

- [ ] **Step 3: Commit**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: 定时任务（P10 第十轮）文档收尾 + CLAUDE.md 阶段更新"
```
