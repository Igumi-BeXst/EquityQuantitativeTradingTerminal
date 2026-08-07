#include "ui/widgets/central_chart_widget.h"
#include "ui/widgets/kline_chart.h"
#include "ui/widgets/time_line_chart.h"
#include "ui/widgets/overlay_dialog.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QStackedWidget>
#include <QToolButton>
#include <QPushButton>
#include <QPointer>
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

    // 叠加对比按钮（作用于当前显示的图：分时↔K线各自独立叠加）
    overlayBtn_ = new QPushButton(tr("叠加对比"), this);
    overlayBtn_->setCheckable(true);
    overlayBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{color:#e6e6e6;border:1px solid #6a6a6a;border-radius:3px;"
        "padding:2px 10px;background:#2a2a2c;}"
        "QPushButton:hover{background:#3d3d40;border-color:#999999;}"
        "QPushButton:checked{color:#ffd700;border-color:#ffd700;"
        "background:#3a3220;font-weight:bold;}"));
    overlayBtn_->setEnabled(false);
    periodBar->addWidget(overlayBtn_);
    connect(overlayBtn_, &QPushButton::clicked, this, [this] {
        const bool showRs = (stack_->currentWidget() == kline_)
            ? kline_->isIndicatorVisible(KLineChart::Indicator::RelativeStrength) : false;
        OverlayDialog dlg(provider_, showRs, this);
        connect(&dlg, &OverlayDialog::overlaySelected, this, &CentralChartWidget::applyOverlay);
        connect(&dlg, &OverlayDialog::clearRequested, this, &CentralChartWidget::clearCurrentOverlay);
        dlg.exec();
        refreshOverlayButton();
    });

    // 筹码分布开关（点击切换主窗口筹码面板；勾选状态由主窗口经 setChipButtonChecked 同步）
    chipBtn_ = new QPushButton(tr("筹码分布"), this);
    chipBtn_->setCheckable(true);
    chipBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{color:#e6e6e6;border:1px solid #6a6a6a;border-radius:3px;"
        "padding:2px 10px;background:#2a2a2c;}"
        "QPushButton:hover{background:#3d3d40;border-color:#999999;}"
        "QPushButton:checked{color:#ffd700;border-color:#ffd700;"
        "background:#3a3220;font-weight:bold;}"));
    periodBar->addWidget(chipBtn_);
    connect(chipBtn_, &QPushButton::clicked, this, [this](bool) {
        emit chipDockToggled();
    });
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

    // 后台预热 TDX 板块指数列表缓存（叠加对话框选板块用；与股票搜索/市场面板复用 SH 列表缓存）
    if (provider_) {
        IDataProvider* p = provider_;
        ThreadPool::submitIO([p] { (void)p->getSectorIndices(); });
    }
}

void CentralChartWidget::loadStock(const StockCode& code, const QString& name) {
    clearCustomIndexMode();   // 退出自定义指数模式，图表回到 provider 拉取
    currentCode_ = code;
    currentName_ = name;
    // 保持当前周期，重新加载
    if (stack_->currentWidget() == timeline_) {
        timeline_->loadStock(code, name);
    } else {
        kline_->loadStock(code, name);
    }
    refreshOverlayButton();
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
        if (customIndex_) {
            // 自定义指数：kline 周期变化内部会触发外部重算；周期未变（如首次进 K 线）手动重算
            const bool periodChanged = (period != kline_->period());
            kline_->setPeriod(period);
            if (!periodChanged) reloadCustomIndexNow();
        } else {
            kline_->setPeriod(period);
            if (currentCode_.isValid()) kline_->loadStock(currentCode_, currentName_);
        }
    }
    refreshOverlayButton();
}

// --- 自定义指数（外部数据模式） ---

void CentralChartWidget::loadCustomIndex(const CustomIndex& idx) {
    customIndex_ = idx;
    customIndexCode_ = "CI" + idx.id;
    currentCode_ = StockCode(Market::US, customIndexCode_);
    currentName_ = QString::fromStdString(idx.name);

    // 两个图进入外部数据模式：切周期/分时刷新都重算（主线程同步调用 reloadCustomIndexNow）
    kline_->setExternalReloader([this] { reloadCustomIndexNow(); });
    timeline_->setExternalReloader([this] { reloadCustomIndexNow(); });

    reloadCustomIndexNow();
    refreshOverlayButton();
}

void CentralChartWidget::clearCustomIndexMode() {
    if (!customIndex_) return;
    customIndex_.reset();
    ++customIndexGen_;                 // 作废在途指数重算
    kline_->setExternalReloader({});
    timeline_->setExternalReloader({});
}

void CentralChartWidget::reloadCustomIndexNow() {
    if (!customIndex_) return;
    const CustomIndex idx = *customIndex_;
    const StockCode code = StockCode(Market::US, customIndexCode_);
    const QString name = currentName_;
    const int gen = ++customIndexGen_;
    IDataProvider* provider = provider_;
    QPointer<CentralChartWidget> guard(this);
    const bool isTimeline = (stack_->currentWidget() == timeline_);
    const BarPeriod period = kline_->period();

    ThreadPool::submitIO([provider, guard, gen, idx, code, name, isTimeline, period] {
        // 成分股日线（指数 K 线/昨收统一从日线聚合）
        const auto fetchDaily = [provider](const StockCode& c, BarPeriod) {
            return provider ? provider->getBars(c, BarPeriod::Daily, DateTime{}, utils::now())
                            : std::vector<Bar>{};
        };
        if (isTimeline) {
            const auto daily = computeIndexBars(idx, fetchDaily, BarPeriod::Daily);
            const double prevClose = lastCompletedClose(daily, utils::now());
            const auto fetchIntraday = [provider](const StockCode& c) {
                auto d = provider ? provider->getIntraday(c) : std::nullopt;
                return d ? *d : IntradayData{};
            };
            auto intraday = computeIndexIntraday(idx, prevClose, fetchIntraday);
            LogManager::instance()->log(LogLevel::Info,
                "自定义指数[{}] 分时: 日线={} 昨收={:.2f} 分时点数={} 成分={}",
                idx.name, daily.size(), prevClose, intraday.points.size(),
                idx.constituents.size());
            QMetaObject::invokeMethod(guard, [guard, gen, code, name,
                                             intraday = std::move(intraday)]() mutable {
                if (!guard || gen != guard->customIndexGen_) return;  // 过期/已切股丢弃
                guard->timeline_->loadIntraday(std::move(intraday), code, name);
            }, Qt::QueuedConnection);
        } else {
            auto bars = computeIndexBars(idx, fetchDaily, period);
            LogManager::instance()->log(LogLevel::Info,
                "自定义指数[{}] K线: 周期={} bars={}", idx.name,
                static_cast<int>(period), bars.size());
            QMetaObject::invokeMethod(guard, [guard, gen, code, name,
                                             bars = std::move(bars)]() mutable {
                if (!guard || gen != guard->customIndexGen_) return;  // 过期/已切股丢弃
                guard->kline_->loadBars(bars, code, name);
            }, Qt::QueuedConnection);
        }
    });
}

// --- 叠加对比（作用于当前显示的图，按视图隔离） ---

void CentralChartWidget::applyOverlay(const OverlayTarget& target, bool showRelativeStrength) {
    // 自我叠加拒绝（指数/个股目标 == 当前股票）
    if (target.kind == OverlayKind::Security && target.stockCode == currentCode_) return;
    if (stack_->currentWidget() == timeline_) {
        timeline_->setOverlay(target);
    } else {
        kline_->setOverlay(target, showRelativeStrength);
    }
    refreshOverlayButton();
}

void CentralChartWidget::clearCurrentOverlay() {
    if (stack_->currentWidget() == timeline_) timeline_->clearOverlay();
    else kline_->clearOverlay();
    refreshOverlayButton();
}

void CentralChartWidget::refreshOverlayButton() {
    if (!overlayBtn_) return;
    bool active = false;
    QString name;
    if (stack_->currentWidget() == timeline_) {
        active = timeline_->overlayActive();
        name = timeline_->overlayName();
    } else {
        active = kline_->overlayActive();
        name = kline_->overlayName();
    }
    overlayBtn_->setChecked(active);
    overlayBtn_->setText(active && !name.isEmpty() ? tr("叠加: %1").arg(name) : tr("叠加对比"));
    overlayBtn_->setEnabled(currentCode_.isValid());
}

void CentralChartWidget::setChipButtonChecked(bool visible) {
    if (chipBtn_) chipBtn_->setChecked(visible);
}

} // namespace st

#include "moc_central_chart_widget.cpp"
