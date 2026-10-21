#pragma once

#include <string>

namespace st {

/// 应用路径管理 — 用户目录隔离
///
/// 所有私有数据放在用户目录，程序目录保持只读。
/// 分享给他人使用时，每个用户的私有数据互不干扰。
///
/// ## 为什么分两个根目录
///
/// Windows 有两个语义不同的用户数据根目录：
///
///   %APPDATA%       (Roaming) — 会随域账户漫游同步到服务器
///   %LOCALAPPDATA%  (Local)   — 只留在本机，不参与漫游
///
/// 行情缓存和日志体积大且可重建，放进 Roaming 会让域用户每次登录/注销
/// 同步几百 MB（很多企业的漫游配置有 100MB 上限，直接同步失败）。
/// 因此按"能否重建"分开存放：
///
///   用户财产（不漫游，需备份）   -> %APPDATA%\StockTerminal\
///     ├── config\    (配置/自选股/交易日志/自定义指数/定时任务)
///     └── secrets\   (加密凭证, DPAPI)
///
///   可重建数据（本机缓存）       -> %LOCALAPPDATA%\StockTerminal\
///     ├── data\      (SQLite 行情缓存 / 旧 .stb 缓存)
///     └── logs\      (日志)
///
/// 用户主动产生的文件（截图/CSV 导出）不放 AppData，见 documentsDir()。
class AppPaths {
public:
    /// 本机数据根目录 (%LOCALAPPDATA%/StockTerminal) — 用于缓存/日志
    static std::string appRoot();

    /// 用户配置根目录 (%APPDATA%/StockTerminal) — 用于配置/凭证
    static std::string configRoot();

    /// 配置文件目录 (Roaming/config)
    static std::string configDir();

    /// 本地缓存数据目录 (Local/data) — SQLite 行情缓存等
    static std::string dataDir();

    /// 凭证文件路径 (Roaming/secrets/secrets.json)
    static std::string secretFilePath();

    /// 日志目录 (Local/logs)
    static std::string logDir();

    /// 旧版数据目录 (Roaming/data) — 只用于迁移历史缓存，新代码不要写这里
    static std::string legacyDataDir();

    /// 用户文档目录下的应用目录 (Documents/StockTerminal) — 截图/CSV 导出
    static std::string documentsDir();

    /// 截图目录 (Documents/StockTerminal/screenshots)
    static std::string screenshotsDir();

    /// 创建所有目录（幂等），失败返回 false
    static bool ensureDirectories();

    /// 一次性迁移：把旧版 Roaming/data 下的可重建缓存搬到 Local/data。
    /// 同卷 rename 瞬间完成；迁移失败不阻断启动（旧路径仍可只读访问）。
    static void migrateLegacyData();

private:
    /// 在根目录下拼接子目录并确保存在
    static std::string ensureDir(const std::string& root, const std::string& sub);

    /// 拼接子目录但不创建（迁移/只读场景用）
    static std::string joinDir(const std::string& root, const std::string& sub);
};

} // namespace st
