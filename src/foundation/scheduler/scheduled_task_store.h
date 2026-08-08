#pragma once

#include "foundation/scheduler/scheduled_task.h"
#include <string>
#include <vector>

namespace st {

/// 定时任务持久化 — JSON（数组 roundtrip）
/// 文件缺失/损坏 → 空任务列表；保存时自动创建父目录。
class ScheduledTaskStore {
public:
    /// 载入 JSON 数组 → 填充 out；损坏/缺失返回 false 且 out 为空
    bool load(const std::string& path, std::vector<ScheduledTask>& out) const;

    /// 全量保存为 JSON 数组（dump 缩进 2）；失败返回 false
    bool save(const std::string& path, const std::vector<ScheduledTask>& tasks) const;
};

} // namespace st
