#pragma once

#include <QWidget>
#include <QVariantMap>
#include <vector>

class QListWidget;
class QLabel;
class QSpinBox;
class QPushButton;

namespace st {

/// 策略面板 — 内置策略模板库 + 参数编辑，应用到回测面板
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
    struct Spec {
        QString id;
        QString display;
        QString desc;
        QString p1Name, p2Name;
        int p1, p2, p1Min, p1Max, p2Min, p2Max;
    };
    static std::vector<Spec> builtinTemplates();
    const Spec* currentSpec() const;

    QListWidget* list_ = nullptr;
    QLabel* desc_ = nullptr;
    QLabel* p1Label_ = nullptr;
    QSpinBox* p1_ = nullptr;
    QLabel* p2Label_ = nullptr;
    QSpinBox* p2_ = nullptr;
    QPushButton* applyBtn_ = nullptr;
};

} // namespace st
