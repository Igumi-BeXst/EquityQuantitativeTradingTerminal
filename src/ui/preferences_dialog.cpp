#include "ui/preferences_dialog.h"
#include "ui/theme_manager.h"
#include "core/config_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QStringList>
#include <string>

namespace st {

namespace {
struct ShortcutRow {
    const char* name;
    const char* label;
    const char* defaultKey;
};
const ShortcutRow kShortcutRows[] = {
    {"focusSearch", "聚焦搜索", "Ctrl+Space"},
    {"focusLog", "聚焦日志", "Ctrl+L"},
    {"refreshQuotes", "刷新行情", "F5"},
    {"settings", "偏好设置", "Ctrl+,"},
};
constexpr int kRowCount = static_cast<int>(sizeof(kShortcutRows) / sizeof(kShortcutRows[0]));
}  // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("偏好设置"));
    setMinimumWidth(380);

    auto* layout = new QVBoxLayout(this);

    // ---- 主题 ----
    layout->addWidget(new QLabel(tr("主题")));
    themeCombo_ = new QComboBox();
    themeCombo_->addItem(tr("暗色"), QStringLiteral("dark"));
    themeCombo_->addItem(tr("亮色"), QStringLiteral("light"));
    const bool dark = ThemeManager::current() == ThemeManager::Theme::Dark;
    themeCombo_->setCurrentIndex(dark ? 0 : 1);
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        ThemeManager::apply(themeCombo_->currentData().toString() == QStringLiteral("dark")
                                ? ThemeManager::Theme::Dark
                                : ThemeManager::Theme::Light);
    });
    layout->addWidget(themeCombo_);

    // ---- 快捷键一览（只读）----
    layout->addWidget(new QLabel(tr("快捷键")));
    shortcutTable_ = new QTableWidget(kRowCount, 2, this);
    shortcutTable_->setHorizontalHeaderLabels(
        QStringList() << tr("动作") << tr("快捷键"));
    shortcutTable_->verticalHeader()->setVisible(false);
    shortcutTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shortcutTable_->setSelectionMode(QAbstractItemView::NoSelection);
    shortcutTable_->setAlternatingRowColors(true);

    for (int i = 0; i < kRowCount; ++i) {
        const auto& row = kShortcutRows[i];
        std::string key = ConfigManager::instance()->get<std::string>(
            "ui.shortcuts." + std::string(row.name), row.defaultKey);
        shortcutTable_->setItem(i, 0,
            new QTableWidgetItem(QString::fromUtf8(row.label)));
        shortcutTable_->setItem(i, 1,
            new QTableWidgetItem(QString::fromStdString(key)));
    }
    shortcutTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    shortcutTable_->setFixedHeight(170);
    layout->addWidget(shortcutTable_);

    // ---- 关闭 ----
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* closeBtn = new QPushButton(tr("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}

} // namespace st

#include "moc_preferences_dialog.cpp"
