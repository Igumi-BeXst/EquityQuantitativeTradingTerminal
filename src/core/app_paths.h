#pragma once

#include <string>

namespace st {

/// 应用路径管理 — 用户目录隔离
///
/// 所有私有数据（配置/数据库/凭证/日志）放在用户目录，程序目录保持只读。
/// 分享给他人使用时，每个用户的私有数据互不干扰。
///
/// 目录结构:
///   %APPDATA%\StockTerminal\
///   ├── config\    (非敏感配置)
///   ├── data\      (SQLite 数据库)
///   ├── secrets\   (加密凭证, DPAPI)
///   └── logs\      (日志)
class AppPaths {
public:
    /// 应用根目录 (%APPDATA%/StockTerminal)
    static std::string appRoot();

    /// 配置文件目录
    static std::string configDir();

    /// 数据目录 (SQLite 等)
    static std::string dataDir();

    /// 凭证文件路径 (encrypted secrets)
    static std::string secretFilePath();

    /// 日志目录
    static std::string logDir();

    /// 创建所有目录（幂等），失败返回 false
    static bool ensureDirectories();

private:
    /// 在根目录下拼接子目录并确保存在
    static std::string ensureDir(const std::string& root, const std::string& sub);
};

} // namespace st
