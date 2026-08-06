#include "ui/panels/backtest_panel.h"
#include "ui/widgets/equity_curve_widget.h"
#include "ui/models/trade_table_model.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "data/curated_stocks.h"
#include "engine/backtest/backtest_engine.h"
#include "engine/strategy/istrategy.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "engine/strategy/templates/turtle_strategy.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTableView>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QScrollArea>
#include <QDate>
#include <QHeaderView>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>

namespace st {

namespace {

QString metricValue(double v, int prec = 2, bool percent = false) {
    if (percent) return QString("%1%").arg(v, 0, 'f', prec);
    return QString::number(v, 'f', prec);
}

void colorMetric(QLabel* label, double v) {
    label->setStyleSheet(QStringLiteral("color:%1;")
        .arg(v >= 0 ? "#e54648" : "#2e9e5b"));
}

}  // namespace

BacktestPanel::BacktestPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 配置表单 ----
    auto* form = new QGroupBox(tr("回测配置"));
    auto* fl = new QFormLayout(form);
    strategyCombo_ = new QComboBox();
    strategyCombo_->addItem(tr("双均线 MACross"), QStringLiteral("MACross"));
    strategyCombo_->addItem(tr("海龟 Turtle"), QStringLiteral("Turtle"));
    fl->addRow(tr("策略"), strategyCombo_);

    p1Label_ = new QLabel(tr("快线周期"));
    p2Label_ = new QLabel(tr("慢线周期"));
    p1_ = new QSpinBox(); p1_->setRange(1, 200); p1_->setValue(5);
    p2_ = new QSpinBox(); p2_->setRange(2, 300); p2_->setValue(20);
    fl->addRow(p1Label_, p1_);
    fl->addRow(p2Label_, p2_);
    connect(strategyCombo_, &QComboBox::currentIndexChanged,
            this, &BacktestPanel::onStrategyChanged);

    // 股票池（精选 129 只，多选）
    stockList_ = new QListWidget();
    stockList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    stockList_->setMaximumHeight(120);
    auto addPool = [this](Market m, const std::vector<CuratedStock>& table) {
        for (const auto& c : table) {
            StockCode sc(m, c.code);
            auto* item = new QListWidgetItem(QStringLiteral("%1  %2")
                .arg(QString::fromUtf8(c.code), QString::fromUtf8(c.name)));
            item->setData(Qt::UserRole, QString::fromStdString(sc.fullCode()));
            stockList_->addItem(item);
        }
    };
    addPool(Market::SH, kCuratedSH);
    addPool(Market::SZ, kCuratedSZ);
    for (int i = 0; i < std::min(3, stockList_->count()); ++i) {
        stockList_->item(i)->setSelected(true);
    }
    fl->addRow(tr("股票池"), stockList_);

    startDate_ = new QDateEdit(QDate(2023, 1, 1));
    startDate_->setCalendarPopup(true);
    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    auto* dateRow = new QHBoxLayout();
    dateRow->addWidget(startDate_);
    dateRow->addWidget(new QLabel(tr("~")));
    dateRow->addWidget(endDate_);
    fl->addRow(tr("日期"), dateRow);

    capital_ = new QDoubleSpinBox();
    capital_->setRange(1000, 1e9);
    capital_->setValue(100000.0);
    capital_->setDecimals(0);
    capital_->setSingleStep(10000);
    fl->addRow(tr("初始资金"), capital_);

    auto* runRow = new QHBoxLayout();
    runBtn_ = new QPushButton(tr("运行回测"));
    progress_ = new QProgressBar();
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(progress_, 1);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &BacktestPanel::onRunClicked);

    // ---- 绩效指标 ----
    auto* metricBox = new QGroupBox(tr("绩效指标"));
    auto* mg = new QGridLayout(metricBox);
    auto addMetric = [&](int row, int col, const QString& name, QLabel*& out) {
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
    addMetric(2, 0, tr("卡玛"), calmar_);
    addMetric(2, 1, tr("波动率"), vol_);
    addMetric(2, 2, tr("索提诺"), sortino_);
    addMetric(3, 0, tr("Alpha"), alpha_);
    addMetric(3, 1, tr("Beta"), beta_);
    addMetric(3, 2, tr("交易数"), trades_);
    layout->addWidget(metricBox);

    // ---- 净值曲线 ----
    equityCurve_ = new EquityCurveWidget();
    equityCurve_->setMinimumHeight(140);
    layout->addWidget(equityCurve_);

    // ---- 成交明细 ----
    tradeModel_ = new TradeTableModel(this);
    tradesView_ = new QTableView();
    tradesView_->setModel(tradeModel_);
    tradesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tradesView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tradesView_->horizontalHeader()->setStretchLastSection(true);
    tradesView_->setMinimumHeight(120);
    layout->addWidget(new QLabel(tr("成交明细")));
    layout->addWidget(tradesView_);

    layout->addStretch();
    scroll->setWidget(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    onStrategyChanged();
}

void BacktestPanel::onStrategyChanged() {
    const bool isMa = strategyCombo_->currentIndex() == 0;
    p1Label_->setText(isMa ? tr("快线周期") : tr("入场周期"));
    p2Label_->setText(isMa ? tr("慢线周期") : tr("出场周期"));
    if (isMa) {
        p1_->setValue(5); p2_->setValue(20);
    } else {
        p1_->setValue(20); p2_->setValue(10);
    }
}

void BacktestPanel::loadStrategy(const QString& id, const QVariantMap& params) {
    int idx = strategyCombo_->findData(id);
    if (idx >= 0) strategyCombo_->setCurrentIndex(idx);
    if (params.contains("fastPeriod")) p1_->setValue(params["fastPeriod"].toInt());
    if (params.contains("slowPeriod")) p2_->setValue(params["slowPeriod"].toInt());
    if (params.contains("entryPeriod")) p1_->setValue(params["entryPeriod"].toInt());
    if (params.contains("exitPeriod")) p2_->setValue(params["exitPeriod"].toInt());
    updateParamLabels();
}

void BacktestPanel::updateParamLabels() {
    const bool isMa = strategyCombo_->currentIndex() == 0;
    p1Label_->setText(isMa ? tr("快线周期") : tr("入场周期"));
    p2Label_->setText(isMa ? tr("慢线周期") : tr("出场周期"));
}

std::vector<StockCode> BacktestPanel::selectedSymbols() const {
    std::vector<StockCode> symbols;
    for (int i = 0; i < stockList_->count(); ++i) {
        auto* item = stockList_->item(i);
        if (item->isSelected()) {
            symbols.push_back(StockCode(item->data(Qt::UserRole).toString().toStdString()));
        }
    }
    return symbols;
}

BacktestConfig BacktestPanel::makeConfig(const std::vector<StockCode>& symbols) const {
    BacktestConfig cfg;
    cfg.symbols = symbols;
    cfg.startDate = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    cfg.endDate = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    cfg.initialCapital = capital_->value();
    cfg.period = BarPeriod::Daily;
    cfg.feeConfig = FeeConfig::defaultAShare();
    return cfg;
}

std::shared_ptr<IStrategy> BacktestPanel::makeStrategy() const {
    const bool isMa = strategyCombo_->currentIndex() == 0;
    if (isMa) {
        auto s = std::make_shared<MACrossStrategy>();
        s->fastPeriod_ = p1_->value();
        s->slowPeriod_ = p2_->value();
        return s;
    }
    auto s = std::make_shared<TurtleStrategy>();
    s->entryPeriod_ = p1_->value();
    s->exitPeriod_ = p2_->value();
    return s;
}

void BacktestPanel::onRunClicked() {
    if (running_) return;
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "回测: 请至少选择一只股票");
        return;
    }
    running_ = true;
    runBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    cache_->clear();
    tradeModel_->clear();
    equityCurve_->setData({});
    ret_->setText("--");

    const DateTime start = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    const DateTime end = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());

    // ① IO 池拉取数据 → 缓存（安全异步：捕获 provider + shared_ptr cache + QPointer 守卫）
    IDataProvider* provider = provider_;
    const auto cache = cache_;
    QPointer<BacktestPanel> guard(this);
    ThreadPool::submitIO([provider, cache, guard, symbols, start, end] {
        const int total = static_cast<int>(symbols.size());
        int done = 0;
        for (const auto& code : symbols) {
            auto bars = provider->getBars(code, BarPeriod::Daily, start, end);
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

void BacktestPanel::onAllDataFetched() {
    progress_->setValue(50);
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        running_ = false;
        runBtn_->setEnabled(true);
        return;
    }
    auto cfg = makeConfig(symbols);
    auto strategy = makeStrategy();

    // ② Worker 池回测（安全异步：QPointer 守卫 + shared_ptr cache）
    const auto cache = cache_;
    QPointer<BacktestPanel> guard(this);
    ThreadPool::submitWorker([guard, cache, cfg = std::move(cfg), strategy]() mutable {
        BacktestEngine engine;
        engine.setConfig(cfg);
        engine.setDataCache(cache.get());
        engine.addStrategy(strategy);
        engine.setProgressCallback([guard](double p) {
            QMetaObject::invokeMethod(guard, [guard, p] {
                guard->progress_->setValue(50 + static_cast<int>(p * 50));
            }, Qt::QueuedConnection);
        });
        auto result = engine.run();
        QMetaObject::invokeMethod(guard, [guard, result = std::move(result)]() mutable {
            guard->onResult(result);
        }, Qt::QueuedConnection);
    });
}

void BacktestPanel::setMetrics(const Performance& perf, const BacktestResult& result) {
    auto setPct = [](QLabel* l, double v) { l->setText(QString("%1%").arg(v, 0, 'f', 2)); };
    auto setVal = [](QLabel* l, double v, int p = 2) { l->setText(QString::number(v, 'f', p)); };

    setPct(ret_, perf.totalReturn);
    setPct(annual_, perf.annualReturn);
    setVal(sharpe_, perf.sharpeRatio);
    setPct(mdd_, perf.maxDrawdown);
    setPct(winRate_, perf.winRate);
    setVal(pf_, perf.profitFactor);
    setVal(calmar_, perf.calmarRatio);
    setPct(vol_, perf.volatility);
    setVal(sortino_, perf.sortinoRatio);
    setVal(alpha_, perf.alpha);
    setVal(beta_, perf.beta);
    trades_->setText(QString::number(result.trades.size()));

    // 红涨绿跌
    colorMetric(ret_, perf.totalReturn);
    colorMetric(annual_, perf.annualReturn);
    colorMetric(mdd_, -perf.maxDrawdown);
}

void BacktestPanel::onResult(const BacktestResult& result) {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setValue(100);
    progress_->setVisible(false);

    if (!result.success) {
        LogManager::instance()->log(LogLevel::Error, "回测失败: {}", result.error);
        ret_->setText(tr("回测失败: ") + QString::fromStdString(result.error));
        return;
    }

    setMetrics(result.performance, result);
    equityCurve_->setData(result.performance.equityCurve);
    tradeModel_->setTrades(result.trades);
    LogManager::instance()->log(LogLevel::Info,
        "回测完成: 总收益 {:.2f}% 夏普 {:.2f} 交易 {} 笔",
        result.performance.totalReturn, result.performance.sharpeRatio,
        static_cast<int>(result.trades.size()));
}

} // namespace st

#include "moc_backtest_panel.cpp"
