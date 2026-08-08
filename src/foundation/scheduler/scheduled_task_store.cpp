#include "foundation/scheduler/scheduled_task_store.h"

#include <filesystem>
#include <fstream>

namespace st {

bool ScheduledTaskStore::load(const std::string& path, std::vector<ScheduledTask>& out) const {
    out.clear();
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    try {
        auto j = nlohmann::json::parse(ifs);
        if (!j.is_array()) return false;
        for (const auto& item : j) {
            out.push_back(ScheduledTask::fromJson(item));
        }
        return true;
    } catch (const std::exception&) {
        out.clear();
        return false;
    }
}

bool ScheduledTaskStore::save(const std::string& path, const std::vector<ScheduledTask>& tasks) const {
    try {
        std::filesystem::path p(path);
        if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : tasks) {
            arr.push_back(t.toJson());
        }
        // 原子写入：先写临时文件，成功后再 rename 覆盖正式文件。
        // 崩溃/断电时磁盘上是完整的旧文件或临时文件，不会留下截断的正式文件。
        const auto tmpPath = p.string() + ".tmp";
        {
            std::ofstream ofs(tmpPath, std::ios::trunc);
            if (!ofs.is_open()) return false;
            ofs << arr.dump(2);
            if (!ofs) return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmpPath, p, ec);
        if (ec) {
            std::filesystem::remove(tmpPath, ec);   // 尽力清理临时文件
            return false;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace st
