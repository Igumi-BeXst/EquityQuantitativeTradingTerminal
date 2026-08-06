#include "ui/panels/stress_test_panel.h"
#include "ui/widgets/equity_curve_widget.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "data/curated_stocks.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QScrollArea>
#include <QHeaderView>
#include <QDate>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>

namespace st {

StressTestPanel::StressTestPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* form = new QGroupBox(tr("压力测试"));
    auto* fl = new QFormLayout(form);

    strategyCombo_ = new QComboBox;
    strategyCombo_->addItem(tr("双均线 MACross"), QStringLiteral("MACross"));
    strategyCombo_->addItem(tr("海龟 Turtle"), QStringLiteral("Turtle"));
    fl->addRow(tr("策略"), strategyCombo_);
    connect(strategyCombo_, &QComboBox::currentIndexChanged,
            this, &StressTestPanel::onStrategyChanged);

    p1Label_ = new QLabel(tr("快线周期"));
    p2Label_ = new QLabel(tr("慢线周期"));
    p1_ = new QSpinBox;
    p1_->setRange(1, 200);
    p1_->setValue(5);
    p2_ = new QSpinBox;
    p2_->setRange(2, 300);
    p2_->setValue(20);
    fl->addRow(p1Label_, p1_);
    fl->addRow(p2Label_, p2_);

    capital_ = new QDoubleSpinBox;
    capital_->setRange(1000, 1e9);
    capital_->setValue(100000.0);
    capital_->setDecimals(0);
    capital_->setSingleStep(10000);
    fl->addRow(tr("初始资金"), capital_);

    windowCombo_ = new QComboBox;
    windowCombo_->addItem(tr("全部窗口"), QStringLiteral("__all__"));
    for (const auto& w : StressTest::defaultWindows()) {
        windowCombo_->addItem(QString::fromUtf8(w.name.c_str()),
                              QString::fromStdString(w.id));
    }
    fl->addRow(tr("压力场景"), windowCombo_);
    connect(windowCombo_, &QComboBox::currentIndexChanged,
            this, &StressTestPanel::onWindowChanged);

    auto* runRow = new QHBoxLayout;
    runBtn_ = new QPushButton(tr("运行压力测试"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(progress_, 1);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &StressTestPanel::onRunClicked);

    // 净值曲线
    curve_ = new EquityCurveWidget;
    curve_->setMinimumHeight(140);
    layout->addWidget(curve_);

    // 指标
    auto* metricBox = new QGroupBox(tr("当前窗口绩效"));
    auto* mg = new QGridLayout(metricBox);
    const auto addMetric = [&](int row, int col, const QString& name, QLabel*& out) {
        mg->addWidget(new QLabel(name), row, col * 2);
        out = new QLabel("--");
        out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        mg->addWidget(out, row, col * 2 + 1);
    };
    addMetric(0, 0, tr("总收益"), ret_);
    addMetric(0, 1, tr("年化"), annual_);
    addMetric(0, 2, tr("夏普"), sharpe_);
    addMetric(1, 0, tr("最大回撤"), mdd_);
    addMetric(1, 1, tr("胜率"), winRate_);
    addMetric(1, 2, tr("盈亏比"), pf_);
    addMetric(2, 0, tr("交易数"), trades_);
    addMetric(2, 1, tr("期末净值"), endEquity_);
    layout->addWidget(metricBox);

    // 基线对比
    auto* baseBox = new QGroupBox(tr("与全期基线对比"));
    auto* bg = new QGridLayout(baseBox);
    const auto addBase = [&](int row, int col, const QString& name, QLabel*& out) {
        bg->addWidget(new QLabel(name), row, col * 2);
        out = new QLabel("--");
        out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bg->addWidget(out, row, col * 2 + 1);
    };
    addBase(0, 0, tr("基线总收益"), baseRet_);
    addBase(0, 1, tr("基线最大回撤"), baseMdd_);
    addBase(1, 0, tr("收益差(窗口-基线)"), deltaRet_);
    layout->addWidget(baseBox);

    layout->addStretch();
    scroll->setWidget(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    onStrategyChanged();
}

void StressTestPanel::onStrategyChanged() {
    const bool isMa = strategyCombo_->currentIndex() == 0;
    p1Label_->setText(isMa ? tr("快线周期") : tr("入场周期"));
    p2Label_->setText(isMa ? tr("慢线周期") : tr("出场周期"));
    if (isMa) {
        p1_->setValue(5);
        p2_->setValue(20);
    } else {
        p1_->setValue(20);
        p2_->setValue(10);
    }
}

std::vector<StockCode> StressTestPanel::selectedSymbols() const {
    // 用精选池全部（压力测试看整体抗压）
    std::vector<StockCode> symbols;
    for (const auto& c : kCuratedSH) symbols.push_back(StockCode(Market::SH, c.code));
    for (const auto& c : kCuratedSZ) symbols.push_back(StockCode(Market::SZ, c.code));
    return symbols;
}

void StressTestPanel::onRunClicked() {
    if (running_) return;
    running_ = true;
    runBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    cache_->clear();
    curve_->setSeries({});

    // 数据范围: 覆盖最老窗口(2015) 到今天
    const auto baseStart = utils::parseDate("2015-01-01");
    const auto end = utils::now();

    // 安全异步：捕获 provider + shared_ptr cache + QPointer 守卫
    const auto symbols = selectedSymbols();
    IDataProvider* provider = provider_;
    const auto cache = cache_;
    QPointer<StressTestPanel> guard(this);
    ThreadPool::submitIO([provider, cache, guard, symbols, baseStart, end] {
        const int total = static_cast<int>(symbols.size());
        int done = 0;
        for (const auto& code : symbols) {
            auto bars = provider->getBars(code, BarPeriod::Daily, baseStart, end);
            if (!bars.empty()) cache->cacheBars(code, BarPeriod::Daily, std::move(bars));
            ++done;
            QMetaObject::invokeMethod(guard, [guard, done, total] {
                guard->progress_->setValue(done * 50 / total);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(guard, [guard] { guard->onAllDataFetched(); },
                                  Qt::QueuedConnection);
    });
}

void StressTestPanel::onAllDataFetched() {
    progress_->setValue(50);
    const bool all = windowCombo_->currentData().toString() == QStringLiteral("__all__");
    auto windows = all ? StressTest::defaultWindows()
                       : std::vector<StressWindow>{};
    if (!all) {
        const auto id = windowCombo_->currentData().toString().toStdString();
        for (const auto& w : StressTest::defaultWindows()) {
            if (w.id == id) { windows.push_back(w); break; }
        }
    }

    const bool isMa = strategyCombo_->currentIndex() == 0;
    StressTestConfig cfg;
    cfg.strategyId = strategyCombo_->currentData().toString().toStdString();
    cfg.params = isMa
        ? std::vector<std::pair<std::string, int>>{
              {"fastPeriod", p1_->value()}, {"slowPeriod", p2_->value()}}
        : std::vector<std::pair<std::string, int>>{
              {"entryPeriod", p1_->value()}, {"exitPeriod", p2_->value()}};
    cfg.symbols = selectedSymbols();
    cfg.initialCapital = capital_->value();
    cfg.feeConfig = FeeConfig::defaultAShare();
    const auto cache = cache_;  // shared_ptr：worker 内 cfg.cache 指向它，面板销毁后仍存活
    cfg.cache = cache.get();
    cfg.baselineStart = utils::parseDate("2015-01-01");
    cfg.baselineEnd = utils::now();

    // 安全异步：QPointer 守卫 + shared_ptr cache
    QPointer<StressTestPanel> guard(this);
    ThreadPool::submitWorker([guard, cache, cfg = std::move(cfg), windows]() mutable {
        StressTest st;
        st.setProgressCallback([guard](double p) {
            QMetaObject::invokeMethod(guard, [guard, p] {
                guard->progress_->setValue(50 + static_cast<int>(p * 50));
            }, Qt::QueuedConnection);
        });
        auto output = st.run(cfg, windows);
        QMetaObject::invokeMethod(guard, [guard, output = std::move(output)]() mutable {
            guard->onResult(std::move(output));
        }, Qt::QueuedConnection);
    });
}

void StressTestPanel::onResult(StressTestOutput output) {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setValue(100);
    progress_->setVisible(false);
    output_ = std::move(output);

    // 基线对比
    if (output_.baseline.success) {
        baseRet_->setText(QStringLiteral("%1%").arg(output_.baseline.performance.totalReturn, 0, 'f', 2));
        baseMdd_->setText(QStringLiteral("%1%").arg(output_.baseline.performance.maxDrawdown, 0, 'f', 2));
    } else {
        baseRet_->setText(tr("失败"));
        baseMdd_->setText(tr("失败"));
    }

    displayWindow(windowCombo_->currentData().toString());
}

void StressTestPanel::displayWindow(const QString& windowId) {
    if (output_.windows.empty()) return;
    const StressTestResult* target = nullptr;
    if (windowId == QStringLiteral("__all__")) {
        // 全部窗口 → 展示第一个
        target = &output_.windows.front();
    } else {
        const std::string id = windowId.toStdString();
        for (const auto& w : output_.windows) {
            if (w.windowId == id) { target = &w; break; }
        }
    }
    if (!target) target = &output_.windows.front();

    const auto& p = target->performance;
    ret_->setText(QStringLiteral("%1%").arg(p.totalReturn, 0, 'f', 2));
    annual_->setText(QStringLiteral("%1%").arg(p.annualReturn, 0, 'f', 2));
    sharpe_->setText(QString::number(p.sharpeRatio, 'f', 2));
    mdd_->setText(QStringLiteral("%1%").arg(p.maxDrawdown, 0, 'f', 2));
    mdd_->setStyleSheet(QStringLiteral("color:#e54648;"));  // 回撤红色高亮
    winRate_->setText(QStringLiteral("%1%").arg(p.winRate, 0, 'f', 1));
    pf_->setText(QString::number(p.profitFactor, 'f', 2));
    trades_->setText(QString::number(p.totalTrades));
    endEquity_->setText(QString::number(target->equityCurve.empty() ? 0.0
                                                                    : target->equityCurve.back(), 'f', 3));

    if (output_.baseline.success) {
        deltaRet_->setText(QStringLiteral("%1%").arg(
            p.totalReturn - output_.baseline.performance.totalReturn, 0, 'f', 2));
    }

    std::vector<EquityCurveWidget::EquitySeries> series;
    if (!target->equityCurve.empty()) {
        series.push_back({QString::fromStdString(target->windowName),
                          QColor("#4fc3f7"), target->equityCurve});
    }
    curve_->setSeries(series);

    LogManager::instance()->log(LogLevel::Info,
        "压力测试: {} 总收益 {:.2f}% 最大回撤 {:.2f}%",
        target->windowName, p.totalReturn, p.maxDrawdown);
}

void StressTestPanel::onWindowChanged() {
    if (running_ || output_.windows.empty()) return;
    displayWindow(windowCombo_->currentData().toString());
}

void StressTestPanel::resetToIdle() {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setVisible(false);
}

} // namespace st

#include "moc_stress_test_panel.cpp"
