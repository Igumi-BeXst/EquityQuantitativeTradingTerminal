#pragma once

#include <QString>

namespace st {

/// 主题管理 — 暗色/亮色 QSS 切换，当前主题持久化到 ConfigManager("ui.theme")
class ThemeManager {
public:
    enum class Theme { Dark, Light };

    /// 从配置读取当前主题（默认 dark）
    static Theme current();

    /// 应用主题并持久化到配置
    static void apply(Theme theme);

    /// 应用配置中的当前主题
    static void applyCurrent();

    /// 切换主题，返回新主题
    static Theme toggle();

    /// 主题名: "dark" / "light"
    static QString themeName(Theme theme);
};

} // namespace st
