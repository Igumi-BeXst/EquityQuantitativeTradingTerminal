#include "ui/theme_manager.h"
#include "core/config_manager.h"
#include <QApplication>
#include <QFile>

namespace st {

ThemeManager::Theme ThemeManager::current() {
    auto name = ConfigManager::instance()->get<std::string>("ui.theme", "dark");
    return name == "light" ? Theme::Light : Theme::Dark;
}

QString ThemeManager::themeName(Theme theme) {
    return theme == Theme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

void ThemeManager::apply(Theme theme) {
    const QString path = theme == Theme::Dark
        ? QStringLiteral(":/themes/dark.qss")
        : QStringLiteral(":/themes/light.qss");

    QString qss;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        qss = QString::fromUtf8(f.readAll());
    }

    if (auto* app = qApp) {
        app->setStyleSheet(qss);
    }

    // 持久化当前主题（set() 不 emit configChanged，无需监听）
    ConfigManager::instance()->set("ui.theme", themeName(theme).toStdString());
    ConfigManager::instance()->save();
}

void ThemeManager::applyCurrent() {
    apply(current());
}

ThemeManager::Theme ThemeManager::toggle() {
    Theme next = current() == Theme::Dark ? Theme::Light : Theme::Dark;
    apply(next);
    return next;
}

} // namespace st
