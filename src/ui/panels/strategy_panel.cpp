#include "ui/panels/strategy_panel.h"
#include <QListWidget>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace st {

std::vector<StrategyPanel::Spec> StrategyPanel::builtinTemplates() {
    return {
        {
            QStringLiteral("MACross"), tr("双均线策略"),
            tr("快线上穿慢线金叉买入，下穿死叉清仓。经典趋势跟踪。"),
            tr("快线周期"), tr("慢线周期"), 5, 20, 1, 100, 2, 200,
        },
        {
            QStringLiteral("Turtle"), tr("海龟策略"),
            tr("唐奇安通道突破：突破 N 日最高买入，跌破 M 日最低卖出。"),
            tr("入场周期"), tr("出场周期"), 20, 10, 1, 120, 1, 120,
        },
    };
}

StrategyPanel::StrategyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("策略模板")));
    list_ = new QListWidget();
    for (const auto& s : builtinTemplates()) {
        list_->addItem(s.display);
    }
    list_->setCurrentRow(0);
    layout->addWidget(list_);

    desc_ = new QLabel();
    desc_->setWordWrap(true);
    desc_->setStyleSheet(QStringLiteral("color:#999999;"));
    layout->addWidget(desc_);

    auto* form = new QFormLayout();
    p1Label_ = new QLabel();
    p1_ = new QSpinBox();
    p2Label_ = new QLabel();
    p2_ = new QSpinBox();
    form->addRow(p1Label_, p1_);
    form->addRow(p2Label_, p2_);
    layout->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    auto* resetBtn = new QPushButton(tr("恢复默认"));
    applyBtn_ = new QPushButton(tr("应用到回测"));
    applyBtn_->setDefault(true);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    btnRow->addWidget(applyBtn_);
    layout->addLayout(btnRow);
    layout->addStretch();

    connect(list_, &QListWidget::currentRowChanged, this, &StrategyPanel::onTemplateChanged);
    connect(resetBtn, &QPushButton::clicked, this, &StrategyPanel::onResetClicked);
    connect(applyBtn_, &QPushButton::clicked, this, &StrategyPanel::onApplyClicked);

    onTemplateChanged();
}

const StrategyPanel::Spec* StrategyPanel::currentSpec() const {
    int row = list_->currentRow();
    if (row < 0) return nullptr;
    auto specs = builtinTemplates();
    return &specs[static_cast<size_t>(row)];
}

void StrategyPanel::onTemplateChanged() {
    auto specs = builtinTemplates();
    int row = list_->currentRow();
    if (row < 0) return;
    const auto& s = specs[static_cast<size_t>(row)];
    desc_->setText(s.desc);
    p1Label_->setText(s.p1Name);
    p2Label_->setText(s.p2Name);
    p1_->setRange(s.p1Min, s.p1Max);
    p2_->setRange(s.p2Min, s.p2Max);
    p1_->setValue(s.p1);
    p2_->setValue(s.p2);
}

void StrategyPanel::onApplyClicked() {
    auto specs = builtinTemplates();
    int row = list_->currentRow();
    if (row < 0) return;
    const auto& s = specs[static_cast<size_t>(row)];
    QVariantMap params;
    if (s.id == QStringLiteral("MACross")) {
        params["fastPeriod"] = p1_->value();
        params["slowPeriod"] = p2_->value();
    } else {
        params["entryPeriod"] = p1_->value();
        params["exitPeriod"] = p2_->value();
    }
    emit applyStrategy(s.id, params);
}

void StrategyPanel::onResetClicked() {
    auto specs = builtinTemplates();
    int row = list_->currentRow();
    if (row < 0) return;
    const auto& s = specs[static_cast<size_t>(row)];
    p1_->setValue(s.p1);
    p2_->setValue(s.p2);
}

} // namespace st

#include "moc_strategy_panel.cpp"
