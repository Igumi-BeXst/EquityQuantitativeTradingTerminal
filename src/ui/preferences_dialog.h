#pragma once

#include <QDialog>

class QComboBox;
class QTableWidget;

namespace st {

/// 偏好设置对话框 — 主题切换 + 快捷键一览（只读，自定义快捷键后续版本）
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

private:
    QComboBox* themeCombo_ = nullptr;
    QTableWidget* shortcutTable_ = nullptr;
};

} // namespace st
