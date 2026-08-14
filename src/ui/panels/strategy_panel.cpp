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
            QStringLiteral("MACross"), tr("趋势跟踪"), tr("双均线策略"),
            tr("快线上穿慢线金叉买入，下穿死叉清仓。经典趋势跟踪。"),
            tr("快线周期"), tr("均线金叉的快线周期，越小越敏感"),
            tr("慢线周期"), tr("均线金叉的慢线周期，越大越稳健"),
            5, 20, 1, 100, 2, 200,
        },
        {
            QStringLiteral("Turtle"), tr("趋势跟踪"), tr("海龟策略"),
            tr("唐奇安通道突破：突破 N 日最高买入，跌破 M 日最低卖出。"),
            tr("入场周期"), tr("突破回看：突破 N 日最高价买入"),
            tr("出场周期"), tr("止损回看：跌破 M 日最低价清仓"),
            20, 10, 1, 120, 1, 120,
        },
        {
            QStringLiteral("Momentum"), tr("动量"), tr("动量策略"),
            tr("N 日收益率突破阈值买入（趋势确认），收盘跌破 M 日均线离场。"),
            tr("动量回看"), tr("计算 N 日收益率（如 20 = 近 20 日涨幅）"),
            tr("离场均线"), tr("收盘跌破 M 日均线清仓（趋势破坏）"),
            20, 10, 2, 120, 2, 120,
        },
        {
            QStringLiteral("Breakout"), tr("突破"), tr("收盘突破策略"),
            tr("收盘价突破 N 日最高收盘买入，跌破 M 日最低收盘离场。"
               "与海龟不同：用收盘价确认突破，减少假突破。"),
            tr("突破回看"), tr("收盘突破 N 日最高收盘买入"),
            tr("离场回看"), tr("收盘跌破 M 日最低收盘清仓"),
            20, 10, 2, 120, 2, 120,
        },
        {
            QStringLiteral("MeanReversion"), tr("均值回归"), tr("均值回归策略"),
            tr("收盘低于均线 X% 买入（超跌反弹），回到均线上方离场。"),
            tr("均线周期"), tr("偏离基准均线周期"),
            tr("超跌阈值"), tr("低于均线千分数阈值（如 30 = 3%）触发买入"),
            20, 30, 2, 120, 5, 200,
        },
        {
            QStringLiteral("Rsi"), tr("反转"), tr("RSI 策略"),
            tr("RSI 超卖买入，超买离场（周期固定 14）。"),
            tr("买入线"), tr("RSI 低于该值买入（超卖，默认 30）"),
            tr("卖出线"), tr("RSI 高于该值清仓（超买，默认 70）"),
            30, 70, 5, 50, 50, 95,
        },
    };
}

StrategyPanel::StrategyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("策略模板（按类别分组）")));
    list_ = new QListWidget();
    for (const auto& s : builtinTemplates()) {
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
    // 参数说明常显（灰色小字，替代悬停 tooltip）
    p1DescLabel_->setText(s.p1Desc);
    p2DescLabel_->setText(s.p2Desc);
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
    } else if (s.id == QStringLiteral("Turtle") ||
               s.id == QStringLiteral("Breakout")) {
        params["entryPeriod"] = p1_->value();
        params["exitPeriod"] = p2_->value();
    } else if (s.id == QStringLiteral("Momentum")) {
        params["lookbackPeriod"] = p1_->value();
        params["exitPeriod"] = p2_->value();
    } else if (s.id == QStringLiteral("MeanReversion")) {
        params["maPeriod"] = p1_->value();
        params["deviationPct"] = p2_->value();
    } else if (s.id == QStringLiteral("Rsi")) {
        params["buyLevel"] = p1_->value();
        params["sellLevel"] = p2_->value();
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
