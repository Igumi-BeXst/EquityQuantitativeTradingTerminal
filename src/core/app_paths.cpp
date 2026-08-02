#include "core/app_paths.h"
#include <QStandardPaths>
#include <QDir>
#include <filesystem>
#include <system_error>

namespace st {

std::string AppPaths::appRoot() {
    // Windows: %APPDATA%/StockTerminal
    auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        // 回退: 当前用户目录
        base = QDir::homePath();
    }
    return base.toStdString();
}

std::string AppPaths::ensureDir(const std::string& root, const std::string& sub) {
    std::filesystem::path dir = std::filesystem::path(root) / sub;
    std::error_code ec;
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir.string();
}

std::string AppPaths::configDir() {
    return ensureDir(appRoot(), "config");
}

std::string AppPaths::dataDir() {
    return ensureDir(appRoot(), "data");
}

std::string AppPaths::secretFilePath() {
    return (std::filesystem::path(ensureDir(appRoot(), "secrets")) / "secrets.json").string();
}

std::string AppPaths::logDir() {
    return ensureDir(appRoot(), "logs");
}

bool AppPaths::ensureDirectories() {
    auto root = appRoot();
    if (root.empty()) return false;
    bool ok = true;
    ok &= !configDir().empty();
    ok &= !dataDir().empty();
    ok &= !secretFilePath().empty();
    ok &= !logDir().empty();
    return ok;
}

} // namespace st
