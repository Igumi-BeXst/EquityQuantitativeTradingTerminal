#include "core/app_paths.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace st {

namespace {

/// 取标准位置；为空时回退到 homePath（无桌面/受限环境）
std::string writableBase(QStandardPaths::StandardLocation location) {
    auto base = QStandardPaths::writableLocation(location);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    return base.toStdString();
}

/// 应用目录名（与 main.cpp 的 setApplicationName 一致；未设置时 Qt 用 exe 名）
std::string appFolderName() {
    const auto name = QCoreApplication::applicationName();
    return name.isEmpty() ? std::string("StockTerminal") : name.toStdString();
}

/// exe 所在目录（便携模式兜底用）
std::string exeDirPath() {
    const auto dir = QCoreApplication::applicationDirPath();
    return dir.isEmpty() ? std::string() : dir.toStdString();
}

/// 在指定环境变量目录下拼出应用根目录。
///
/// 不用 QStandardPaths::AppLocalDataLocation 的原因是：它在低完整性进程
/// （受限令牌 / 沙箱 / 部分受管环境）下会解析到 AppData\LocalLow，而不是
/// 标准的 AppData\Local。LocalLow 是给低权限进程用的边角目录，会被清理工具
/// 当成可丢弃数据，不适合放行情缓存。%LOCALAPPDATA% 是稳定的系统事实。
bool baseFromEnv(const char* envName, std::string& out) {
    const QByteArray value = qgetenv(envName);
    if (value.isEmpty()) return false;
    out = (std::filesystem::path(value.toStdString()) / appFolderName()).string();
    return true;
}

/// 真正验证目录可用：建得出来 + 写得进去。
///
/// 只检查 ACL 不够 —— 受限令牌、安全软件规则、策略拦截都可能让
/// create_directories 返回 Access denied，此时必须换位置而不是让启动失败。
bool dirWritable(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec) || ec) return false;

    const std::filesystem::path probe = std::filesystem::path(dir) / ".__write_probe";
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << 'x';
        if (!f) return false;
    }
    std::error_code rec;
    std::filesystem::remove(probe, rec);
    return true;
}

} // namespace

std::string AppPaths::appRoot() {
    // 依次尝试，取第一个真正可写的位置：
    //   1. Local     —— 理想：缓存/日志不参与域漫游
    //   2. Roaming   —— 兼容旧版本数据
    //   3. exe 同目录 —— 便携/受限环境兜底（AppData 被策略或安全软件拦死时）
    //   4. TEMP      —— 最后手段（内容可能被系统清理）
    // 全部不可写才返回空，调用方据此降级（不写盘但仍可拉数据）。
    static const std::string resolved = [] {
        std::string local;
        if (baseFromEnv("LOCALAPPDATA", local) && dirWritable(local)) return local;

        std::string roaming;
        if (baseFromEnv("APPDATA", roaming) && dirWritable(roaming)) return roaming;

        const std::string exeDir = exeDirPath();
        if (!exeDir.empty()) {
            const std::string portableDir =
                (std::filesystem::path(exeDir) / "appdata").string();
            if (dirWritable(portableDir)) return portableDir;
        }

        std::string temp;
        if (baseFromEnv("TEMP", temp) && dirWritable(temp)) return temp;
        return std::string();
    }();
    return resolved;
}

std::string AppPaths::configRoot() {
    // Roaming: 用户配置与凭证需要跟随账户备份/漫游。
    // 若 Roaming 不可写，退回与缓存相同的根，至少保证配置能保存。
    static const std::string resolved = [] {
        std::string roaming;
        if (baseFromEnv("APPDATA", roaming) && dirWritable(roaming)) return roaming;
        return appRoot();
    }();
    return resolved;
}

std::string AppPaths::ensureDir(const std::string& root, const std::string& sub) {
    if (root.empty()) return {}; // 根不可用：不要退化成相对路径落到工作目录
    std::error_code ec;
    // 注意：全部使用带 error_code 的重载。不带 ec 的 exists()/create_directories()
    // 失败时会抛 std::filesystem_error，GUI 程序里没人接就 std::terminate -> abort()。
    std::filesystem::path dir = std::filesystem::path(root) / sub;
    if (!std::filesystem::exists(dir, ec) || ec) {
        std::error_code mkEc;
        std::filesystem::create_directories(dir, mkEc);
    }
    return dir.string();
}

std::string AppPaths::joinDir(const std::string& root, const std::string& sub) {
    return (std::filesystem::path(root) / sub).string();
}

std::string AppPaths::configDir() {
    return ensureDir(configRoot(), "config");
}

std::string AppPaths::dataDir() {
    return ensureDir(appRoot(), "data");
}

std::string AppPaths::secretFilePath() {
    return (std::filesystem::path(ensureDir(configRoot(), "secrets")) / "secrets.json").string();
}

std::string AppPaths::logDir() {
    return ensureDir(appRoot(), "logs");
}

std::string AppPaths::legacyDataDir() {
    // 旧版位置固定是 %APPDATA%\StockTerminal\data，**只读**访问，不创建、不校验可写性。
    // 不能复用 configRoot()：当 Roaming 不可写而回退到别处时，旧缓存仍留在 Roaming，
    // 这里必须还能找到它（读取不需要写权限）。
    const QByteArray roaming = qgetenv("APPDATA");
    if (roaming.isEmpty()) return {};
    return (std::filesystem::path(roaming.toStdString()) / appFolderName() / "data").string();
}

std::string AppPaths::documentsDir() {
    return ensureDir(writableBase(QStandardPaths::DocumentsLocation), "StockTerminal");
}

std::string AppPaths::screenshotsDir() {
    return ensureDir(documentsDir(), "screenshots");
}

bool AppPaths::ensureDirectories() {
    try {
        if (appRoot().empty() || configRoot().empty()) return false;

        // 必须真实校验落地结果：create_directories 失败时（无写权限、盘满、策略拦截）
        // 不能静默当成成功，否则上层会在不存在的路径上启动缓存/日志。
        bool ok = true;
        const auto requireDir = [&ok](const std::string& path) {
            std::error_code ec;
            if (path.empty() || !std::filesystem::is_directory(path, ec)) ok = false;
        };

        requireDir(configDir());
        requireDir(dataDir());
        requireDir(logDir());
        const std::string secret = secretFilePath();
        requireDir(secret.empty() ? std::string()
                                  : std::filesystem::path(secret).parent_path().string());

        // 文档目录在无桌面环境下可能不可用，失败不阻断启动
        documentsDir();
        return ok;
    } catch (const std::exception&) {
        return false;
    }
}

void AppPaths::migrateLegacyData() {
    namespace fs = std::filesystem;
    std::error_code ec;

    try {
        // 只有 Roaming 真正可写时才搬迁。若 Roaming 不可写（AppData 被策略/安全软件
        // 拦截），旧数据只是只读来源，由 readLegacyRawDaily 直接读取即可 ——
        // 强行复制几百 MB 到另一个卷既慢又可能在启动期卡死。
        std::string roaming;
        if (!baseFromEnv("APPDATA", roaming) || !dirWritable(roaming)) return;

        const fs::path legacy = legacyDataDir();
        if (legacy.empty() || !fs::exists(legacy, ec) || ec) return;

        const fs::path target = dataDir();
        if (target.empty()) return;
        if (fs::equivalent(legacy, target, ec)) return; // 已指向同一处

        // 先把条目快照出来。边遍历 directory_iterator 边 rename/remove 会破坏
        // 迭代器状态（MSVC Debug 下直接触发断言 abort），必须先收集再操作。
        std::vector<fs::path> entries;
        {
            std::error_code iterEc;
            for (const auto& entry : fs::directory_iterator(legacy, iterEc)) {
                if (iterEc) break;
                entries.push_back(entry.path());
            }
        }

        for (const auto& src : entries) {
            const fs::path dest = target / src.filename();
            std::error_code inner;
            if (fs::exists(dest, inner)) continue; // 新位置已有同名项，保留新的

            // 同卷 rename 是原子且瞬间完成的（Roaming 与 Local 通常同卷），
            // 这是首选路径，不存在"搬一半"的中间状态。
            fs::rename(src, dest, inner);
            if (inner) {
                // 跨卷或占用：退化为递归复制。只有复制完全成功才删除旧数据；
                // 任何失败都保留原件，宁可占空间也不丢用户缓存。
                std::error_code cec;
                fs::copy(src, dest,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, cec);
                if (!cec) {
                    std::error_code rec;
                    fs::remove_all(src, rec);
                }
            }
        }
    } catch (const std::exception&) {
        // 迁移是尽力而为：任何异常都不能让启动流程崩溃，旧数据保持原样
    }
}

} // namespace st
