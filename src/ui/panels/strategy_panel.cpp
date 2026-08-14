#include "ui/panels/strategy_panel.h"
#include <QListWidget>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace st {

StrategyPanel::StrategyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("策略模板（按类别分组）")));
    list_ = new QListWidget();
    for (const auto& s : strategy_catalog::all()) {
        list_->addItem(QStringLiteral("[%1] %2").arg(s.category, s.display));
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
    p1DescLabel_ = new QLabel();
    p1DescLabel_->setWordWrap(true);
    p1DescLabel_->setStyleSheet(QStringLiteral("color:#888888; font-size:11px;"));
    auto* p1Box = new QVBoxLayout();
    p1Box->setContentsMargins(0, 0, 0, 0);
    p1Box->setSpacing(2);
    p1Box->addWidget(p1_);
    p1Box->addWidget(p1DescLabel_);
    form->addRow(p1Label_, p1Box);

    p2Label_ = new QLabel();
    p2_ = new QSpinBox();
    p2DescLabel_ = new QLabel();
    p2DescLabel_->setWordWrap(true);
    p2DescLabel_->setStyleSheet(QStringLiteral("color:#888888; font-size:11px;"));
    auto* p2Box = new QVBoxLayout();
    p2Box->setContentsMargins(0, 0, 0, 0);
    p2Box->setSpacing(2);
    p2Box->addWidget(p2_);
    p2Box->addWidget(p2DescLabel_);
    form->addRow(p2Label_, p2Box);
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

const strategy_catalog::StrategySpec* StrategyPanel::currentSpec() const {
    int row = list_->currentRow();
    if (row < 0) return nullptr;
    return &strategy_catalog::all()[static_cast<size_t>(row)];
}

void StrategyPanel::onTemplateChanged() {
    const auto* s = currentSpec();
    if (!s) return;
    desc_->setText(s->desc);
    p1Label_->setText(s->p1Name);
    p2Label_->setText(s->p2Name);
    p1_->setRange(s->p1Min, s->p1Max);
    p2_->setRange(s->p2Min, s->p2Max);
    p1_->setValue(s->p1);
    p2_->setValue(s->p2);
    // 参数说明常显（灰色小字）
    p1DescLabel_->setText(s->p1Desc);
    p2DescLabel_->setText(s->p2Desc);
}

void StrategyPanel::onApplyClicked() {
    const auto* s = currentSpec();
    if (!s) return;
    emit applyStrategy(s->id, strategy_catalog::makeParams(*s, p1_->value(), p2_->value()));
}

void StrategyPanel::onResetClicked() {
    const auto* s = currentSpec();
    if (!s) return;
    p1_->setValue(s->p1);
    p2_->setValue(s->p2);
}

} // namespace st

#include "moc_strategy_panel.cpp"
