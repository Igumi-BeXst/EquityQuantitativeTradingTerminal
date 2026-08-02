#pragma once

#include <QString>
#include <QKeySequence>
#include <functional>

// 全局前向声明（不能放在 namespace st 内，否则会遮蔽 ::QWidget）
class QWidget;

namespace st {

/// 快捷键管理器 — 动作 → 快捷键映射
///
/// 注册时读取 ConfigManager("ui.shortcuts.<name>") 覆盖默认键（用户自定义），
/// 在 context widget 上创建 QShortcut（随 context 生命周期销毁）。
class ShortcutManager {
public:
    /// 注册动作: 默认快捷键可被用户配置覆盖
    void registerAction(const QString& name, const QKeySequence& defaultKey,
                        QWidget* context, std::function<void()> handler);
};

} // namespace st
