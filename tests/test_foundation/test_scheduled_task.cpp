// 测试按 brief 字面值使用 std::fopen/fputs/fclose（C stdio）。
// MSVC /W4 下 C4996 视为弃用警告 → 定义此宏保持零警告，不改测试逻辑。
#define _CRT_SECURE_NO_WARNINGS

#include <gtest/gtest.h>
#include "foundation/scheduler/scheduled_task.h"
#include "foundation/scheduler/scheduled_task_store.h"
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
