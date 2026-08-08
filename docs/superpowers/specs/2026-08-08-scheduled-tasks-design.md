# 定时任务（Scheduled Tasks）设计 — P10 第十轮

日期：2026-08-08
状态：待审阅

## 1. 背景与目标

用户用 StockTerminal 做量化测试 + 手动实盘。定时任务让重复操作自动化：
定时刷新行情 / 定时跑选股 / 定时抓数据 / 定时提醒。

核心调度器 `TaskScheduler`（[task_scheduler.h](src/core/task_scheduler.h)）现为**空壳**（scheduleAt/scheduleRecurring 空实现），需真实现。

## 2. 位置

**「设置(&S)」菜单**下新增「定时任务(&T)…」→ 独立窗口（仿 JournalWindow）。
（不新建「任务」菜单，收敛到设置菜单；设置菜单现有「偏好设置」）

## 3. 数据模型（可单测）

```cpp
enum class ScheduledTaskType { RefreshQuotes, RunScreener, FetchData, Remind };
enum class ScheduleKind { Daily, Interval };   // 每天固定时间 / 每 N 秒·分·时

struct ScheduledTask {
    std::string  id;
    ScheduledTaskType type = ScheduledTaskType::Remind;
    ScheduleKind kind = ScheduleKind::Daily;
    std::string  timeOfDay;         // Daily: "HH:MM"（如 "15:05"）
    int         intervalSeconds = 0; // Interval: 周期（秒），保存时换算 分钟/小时 显示
    std::string  target;            // 任务参数（见 §5 各类型）
    bool        enabled = true;
    std::string  lastResult;        // 上次执行结果："YYYY-MM-DD HH:MM:SS 成功/失败 摘要"
    bool        running = false;    // 当前执行中（防重入）
};
```

## 4. 调度器 TaskScheduler（重写空壳）

- 位于 **UI 层**（动作要调 UI 面板：MarketPanel 刷新）；QTimer 驱动（项目已有模式）
- `QTimer` 每 **10 秒** tick：检查全部任务——
  - Daily：当前时间 ≥ timeOfDay 且距上次执行 > 某阈值（如 1 分钟防重复）→ 触发
  - Interval：距上次触发 ≥ intervalSeconds → 触发
- 触发时：`running_=true` → 执行动作 → 更新 lastResult → `running_=false`
- 提供 `addTask/updateTask/removeTask/tasks()/runNow(id)`（立即执行）
- 动作通过 `std::function<void(const ScheduledTask&)>` 执行器注入（解耦调度与动作）

## 5. 四个动作

| 类型 | target 参数 | 执行 | 依赖 |
|------|------------|------|------|
| RefreshQuotes | 空 | `marketPanel_->refresh()` + `sectorPanel_->refresh()`（市场面板+板块热力图都刷） | 主窗口 marketPanel_/sectorPanel_（面板在则刷） |
| RunScreener | 选股范围 JSON | `StockScreener::run(pool)` 存结果 | 纯引擎，无 UI 依赖 |
| FetchData | 选股范围 JSON（同 RunScreener） | 对范围内全部股票 `provider->getBars` 存 DataRepository | 数据层 |
| Remind | 提醒文本 | `NotificationService::notify()`（应用内通知） | 核心层 |

### RunScreener 选股范围（A+B 双方案）

`target` 存 JSON 描述范围：
```json
{ "scope": "all" }                      // 全部 A 股（getStockList SH+SZ）
{ "scope": "sector", "sector": "BKxxxx" } // 某板块（getSectorIndices 选）
{ "scope": "last" }                      // 复用上次手动选股的配置
```
- 任务对话框里选股范围用下拉：全部A股 / 板块列表（下拉）/ 上次手动选股
- "last" 实现：ScreenerPanel 手动跑选股时把当前配置（symbols/start/end）存一份，任务执行时读取

## 6. 持久化

- **每次增删改立即保存**（addTask/updateTask/removeTask 内部调 store.save）
- `configDir/scheduled_tasks.json`，仿交易日志（TradeJournalStore 模式）
- 启动时 load 进调度器

## 7. UI — 设置菜单 → 定时任务窗口

**TaskWindow : QMainWindow**（仿 JournalWindow）：
- 任务列表 QTableWidget：类型/时间(或周期)/目标摘要/启用/上次结果
- 工具栏：新建/编辑/删除/立即执行/启用开关
- 新建/编辑对话框按类型显示不同参数：
  - RefreshQuotes：仅时间
  - RunScreener：时间 + 选股范围（全部/板块/上次）
  - FetchData：时间 + 选股范围（全部/板块/上次，对范围内全部股票抓日线）
  - Remind：时间 + 提醒内容
- 时间输入：Daily 用 QTimeEdit，Interval 用 QSpinBox(分钟)

## 8. 测试计划

`tests/test_engine/test_scheduled_task.cpp`（当前 358 → 目标 ~375）：
| 组 | 用例 |
|----|------|
| 模型 | 字段默认值、Daily/Interval 参数校验 |
| 调度判定 | Daily 到点触发 / 未到不触发 / 触发后 1 分钟防重 / Interval 到间隔触发 / 禁用不触发 |
| 持久化 | roundtrip / 损坏文件回退 / 增删改立即保存 |
| 动作映射 | target JSON 解析（all/sector/last） |

## 9. 风险与对策

| 风险 | 对策 |
|------|------|
| 调度器放 UI 层 vs 引擎测试 | 调度判定逻辑抽纯函数（`shouldFire(task, now, lastRun)`）可单测；QTimer 壳在 UI |
| RefreshQuotes 依赖面板 | 面板不存在时记录「面板未打开」跳过 |
| RunScreener 无 UI 但慢 | 用 ThreadPool::submitIO 执行，不阻塞调度 tick |
| "last" 选股配置 | ScreenerPanel 手动跑时存配置；未存过则回退 all |
| 任务崩溃丢状态 | 增删改立即保存 + lastResult 也保存 |

## 10. 范围外（v1 不做）

- 交易日历感知（工作日/节假日跳过）——v1 每天执行，v2 加交易日过滤
- 任务执行历史（多轮结果）——v1 只存 lastResult
- 券商实盘交易（无 API）
