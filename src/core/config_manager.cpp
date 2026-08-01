#include "core/config_manager.h"
#include "core/event_bus.h"

namespace st {

ConfigManager* ConfigManager::instance() {
    static ConfigManager mgr;
    return &mgr;
}

ConfigManager::ConfigManager(QObject* parent) : QObject(parent) {}

bool ConfigManager::load(const std::string& configPath) {
    configPath_ = configPath;
    auto loaded = utils::loadJsonFile(configPath);
    if (loaded) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ = std::move(*loaded);
        emit configChanged(QString::fromStdString(configPath));
        return true;
    }
    // Start with empty object if file doesn't exist
    data_ = utils::Json::object();
    return false;
}

bool ConfigManager::save() {
    if (configPath_.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return utils::saveJsonFile(configPath_, data_);
}

} // namespace st
