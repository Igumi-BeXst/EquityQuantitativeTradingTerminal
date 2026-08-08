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
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << arr.dump(2);
        return static_cast<bool>(ofs);
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace st
