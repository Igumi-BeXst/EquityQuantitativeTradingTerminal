#pragma once

#include "ui/strategy_catalog.h"
#include <QWidget>
#include <QVariantMap>

class QListWidget;
class QLabel;
class QSpinBox;
class QPushButton;

namespace st {

/// 策略面板 — 内置策略模板库（共享目录）+ 参数编辑，应用到回测面板
class StrategyPanel : public QWidget {
    Q_OBJECT

public:
    explicit StrategyPanel(QWidget* parent = nullptr);

signals:
    /// 用户点"应用到回测" → 携带策略 id + 参数
    void applyStrategy(const QString& id, const QVariantMap& params);

private slots:
    void onTemplateChanged();
    void onApplyClicked();
    void onResetClicked();

private:
    const strategy_catalog::StrategySpec* currentSpec() const;

    QListWidget* list_ = nullptr;
    QLabel* desc_ = nullptr;
    QLabel* p1Label_ = nullptr;
    QSpinBox* p1_ = nullptr;
    QLabel* p1DescLabel_ = nullptr;   // 参数 1 说明（常显小字）
    QLabel* p2Label_ = nullptr;
    QSpinBox* p2_ = nullptr;
    QLabel* p2DescLabel_ = nullptr;   // 参数 2 说明
    QPushButton* applyBtn_ = nullptr;
};

} // namespace st
