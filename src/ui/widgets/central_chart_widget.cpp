#include "ui/widgets/central_chart_widget.h"
#include "ui/widgets/kline_chart.h"
#include "ui/widgets/time_line_chart.h"
#include "data/idata_provider.h"
#include <QStackedWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QSignalBlocker>

namespace st {

CentralChartWidget::CentralChartWidget(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 周期栏: 分时 | 日 | 周 | 月
    auto* periodBar = new QHBoxLayout();
    periodBar->setContentsMargins(6, 4, 6, 4);
    periodBar->setSpacing(4);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    struct P { const char* label; BarPeriod p; };
    const P periods[] = {
        {"分时", BarPeriod::Minute5},   // 分时走 TimelineChart
        {"日线", BarPeriod::Daily},
        {"周线", BarPeriod::Weekly},
        {"月线", BarPeriod::Monthly},
    };
    for (const auto& pr : periods) {
        auto* btn = new QToolButton(this);
        btn->setText(QString::fromUtf8(pr.label));
        btn->setCheckable(true);
        btn->setAutoRaise(true);
        group->addButton(btn, static_cast<int>(pr.p));
        periodBar->addWidget(btn);
        connect(btn, &QToolButton::clicked, this, [this, pr] { setPeriod(pr.p); });
    }
    periodBar->addStretch();
    layout->addLayout(periodBar);

    // 默认日线选中
    if (auto* dayBtn = group->button(static_cast<int>(BarPeriod::Daily))) {
        dayBtn->setChecked(true);
    }

    // 图表栈
    timeline_ = new TimelineChart(provider_, this);
    kline_ = new KLineChart(provider_, this);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(timeline_);   // index 0
    stack_->addWidget(kline_);      // index 1
    layout->addWidget(stack_, 1);

    // 转发 K线十字光标日期（分时不发；筹码面板按日期查询）
    connect(kline_, &KLineChart::crosshairDateChanged, this,
            [this](const std::optional<DateTime>& date) {
                emit crosshairDateChanged(date);
            });
}

void CentralChartWidget::loadStock(const StockCode& code, const QString& name) {
    currentCode_ = code;
    currentName_ = name;
    // 保持当前周期，重新加载
    if (stack_->currentWidget() == timeline_) {
        timeline_->loadStock(code, name);
    } else {
        kline_->loadStock(code, name);
    }
}

void CentralChartWidget::setPeriod(BarPeriod period) {
    if (period == BarPeriod::Minute1 || period == BarPeriod::Minute5 ||
        period == BarPeriod::Minute15 || period == BarPeriod::Minute30 ||
        period == BarPeriod::Minute60) {
        // 分时/分钟 → TimelineChart
        stack_->setCurrentWidget(timeline_);
        if (currentCode_.isValid()) timeline_->loadStock(currentCode_, currentName_);
    } else {
        stack_->setCurrentWidget(kline_);
        kline_->setPeriod(period);
        if (currentCode_.isValid()) kline_->loadStock(currentCode_, currentName_);
    }
}

} // namespace st

#include "moc_central_chart_widget.cpp"
