#include "ui/shortcut_manager.h"
#include "core/config_manager.h"
#include <QShortcut>
#include <QWidget>
#include <QObject>

namespace st {

void ShortcutManager::registerAction(const QString& name, const QKeySequence& defaultKey,
                                     QWidget* context, std::function<void()> handler) {
    if (!context) return;

    // 用户自定义快捷键优先（ConfigManager 持久化）
    std::string keyPath = "ui.shortcuts." + name.toStdString();
    std::string keyStr = ConfigManager::instance()->get<std::string>(
        keyPath, defaultKey.toString(QKeySequence::NativeText).toStdString());

    QKeySequence key(QString::fromStdString(keyStr));
    if (key.isEmpty()) key = defaultKey;

    // 以 context 为父对象创建（生命周期随 context）
    auto* shortcut = new QShortcut(key, context);
    QObject::connect(shortcut, &QShortcut::activated, context,
                     [handler = std::move(handler)] { handler(); });
}

} // namespace st
