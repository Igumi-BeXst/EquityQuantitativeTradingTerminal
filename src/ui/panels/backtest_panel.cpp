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
#include "engine/strategy/templates/momentum_strategy.h"
#include "engine/strategy/templates/breakout_strategy.h"
#include "engine/strategy/templates/mean_reversion_strategy.h"
#include "engine/strategy/templates/rsi_strategy.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "core/app_paths.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/csv.h"
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
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <iomanip>
#include <sstream>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <unordered_map>

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
    fl->setVerticalSpacing(6);
    // 标签+控件紧贴行（无标签列，间距 6px；控件拉伸占满保持宽度）
    auto labeled = [](QLabel* label, QWidget* field) {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        row->addWidget(label);
        row->addWidget(field, 1);
        return row;
    };
    auto labeledL = [](QLabel* label, QLayout* field) {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        row->addWidget(label);
        row->addLayout(field, 1);
        return row;
    };
    strategyCombo_ = new QComboBox();
    strategyCombo_->addItem(tr("[趋势] 双均线 MACross"), QStringLiteral("MACross"));
    strategyCombo_->addItem(tr("[趋势] 海龟 Turtle"), QStringLiteral("Turtle"));
    strategyCombo_->addItem(tr("[动量] 动量 Momentum"), QStringLiteral("Momentum"));
    strategyCombo_->addItem(tr("[突破] 收盘突破 Breakout"), QStringLiteral("Breakout"));
    strategyCombo_->addItem(tr("[均值回归] 均值回归 MeanReversion"), QStringLiteral("MeanReversion"));
    strategyCombo_->addItem(tr("[反转] RSI Rsi"), QStringLiteral("Rsi"));
    fl->addRow(labeled(new QLabel(tr("策略")), strategyCombo_));

    p1Label_ = new QLabel(tr("快线周期"));
    p2Label_ = new QLabel(tr("慢线周期"));
    p1_ = new QSpinBox(); p1_->setRange(1, 200); p1_->setValue(5);
    p2_ = new QSpinBox(); p2_->setRange(2, 300); p2_->setValue(20);
    fl->addRow(labeled(p1Label_, p1_));
    fl->addRow(labeled(p2Label_, p2_));
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
    fl->addRow(labeled(new QLabel(tr("股票池")), stockList_));

    startDate_ = new QDateEdit(QDate(2023, 1, 1));
    startDate_->setCalendarPopup(true);
    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    auto* dateRow = new QHBoxLayout();
    dateRow->addWidget(startDate_);
    dateRow->addWidget(new QLabel(tr("~")));
    dateRow->addWidget(endDate_);
    fl->addRow(labeledL(new QLabel(tr("日期")), dateRow));

    capital_ = new QDoubleSpinBox();
    capital_->setRange(1000, 1e9);
    capital_->setValue(100000.0);
    capital_->setDecimals(0);
    capital_->setSingleStep(10000);
    fl->addRow(labeled(new QLabel(tr("初始资金")), capital_));

    auto* runRow = new QHBoxLayout();
    runBtn_ = new QPushButton(tr("运行回测"));
    exportBtn_ = new QPushButton(tr("导出结果"));
    progress_ = new QProgressBar();
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(exportBtn_);
    runRow->addWidget(progress_, 1);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &BacktestPanel::onRunClicked);
    connect(exportBtn_, &QPushButton::clicked, this, &BacktestPanel::onExportClicked);

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
    tradesView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
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
    const QString id = strategyCombo_->currentData().toString();
    updateParamLabels();
    if (id == QStringLiteral("MACross")) {
        p1_->setValue(5); p2_->setValue(20);
    } else if (id == QStringLiteral("Turtle")) {
        p1_->setValue(20); p2_->setValue(10);
    } else if (id == QStringLiteral("Momentum")) {
        p1_->setValue(20); p2_->setValue(10);
    } else if (id == QStringLiteral("Breakout")) {
        p1_->setValue(20); p2_->setValue(10);
    } else if (id == QStringLiteral("MeanReversion")) {
        p1_->setValue(20); p2_->setValue(30);
    } else if (id == QStringLiteral("Rsi")) {
        p1_->setValue(30); p2_->setValue(70);
    }
}

void BacktestPanel::loadStrategy(const QString& id, const QVariantMap& params) {
    int idx = strategyCombo_->findData(id);
    if (idx >= 0) strategyCombo_->setCurrentIndex(idx);
    if (params.contains("fastPeriod")) p1_->setValue(params["fastPeriod"].toInt());
    if (params.contains("slowPeriod")) p2_->setValue(params["slowPeriod"].toInt());
    if (params.contains("entryPeriod")) p1_->setValue(params["entryPeriod"].toInt());
    if (params.contains("exitPeriod")) p2_->setValue(params["exitPeriod"].toInt());
    if (params.contains("lookbackPeriod")) p1_->setValue(params["lookbackPeriod"].toInt());
    if (params.contains("maPeriod")) p1_->setValue(params["maPeriod"].toInt());
    if (params.contains("deviationPct")) p2_->setValue(params["deviationPct"].toInt());
    if (params.contains("buyLevel")) p1_->setValue(params["buyLevel"].toInt());
    if (params.contains("sellLevel")) p2_->setValue(params["sellLevel"].toInt());
    updateParamLabels();
}

void BacktestPanel::updateParamLabels() {
    const QString id = strategyCombo_->currentData().toString();
    if (id == QStringLiteral("MACross")) {
        p1Label_->setText(tr("快线周期"));
        p2Label_->setText(tr("慢线周期"));
    } else if (id == QStringLiteral("Turtle") || id == QStringLiteral("Breakout")) {
        p1Label_->setText(tr("入场/突破周期"));
        p2Label_->setText(tr("出场周期"));
    } else if (id == QStringLiteral("Momentum")) {
        p1Label_->setText(tr("动量回看"));
        p2Label_->setText(tr("离场均线"));
    } else if (id == QStringLiteral("MeanReversion")) {
        p1Label_->setText(tr("均线周期"));
        p2Label_->setText(tr("超跌阈值‰"));
    } else if (id == QStringLiteral("Rsi")) {
        p1Label_->setText(tr("买入线"));
        p2Label_->setText(tr("卖出线"));
    }
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
    const QString id = strategyCombo_->currentData().toString();
    if (id == QStringLiteral("MACross")) {
        auto s = std::make_shared<MACrossStrategy>();
        s->fastPeriod_ = p1_->value();
        s->slowPeriod_ = p2_->value();
        return s;
    }
    if (id == QStringLiteral("Turtle") || id == QStringLiteral("Breakout")) {
        if (id == QStringLiteral("Turtle")) {
            auto s = std::make_shared<TurtleStrategy>();
            s->entryPeriod_ = p1_->value();
            s->exitPeriod_ = p2_->value();
            return s;
        }
        auto s = std::make_shared<BreakoutStrategy>();
        s->entryPeriod_ = p1_->value();
        s->exitPeriod_ = p2_->value();
        return s;
    }
    if (id == QStringLiteral("Momentum")) {
        auto s = std::make_shared<MomentumStrategy>();
        s->lookbackPeriod_ = p1_->value();
        s->exitPeriod_ = p2_->value();
        return s;
    }
    if (id == QStringLiteral("MeanReversion")) {
        auto s = std::make_shared<MeanReversionStrategy>();
        s->maPeriod_ = p1_->value();
        s->deviationPct_ = p2_->value();
        return s;
    }
    if (id == QStringLiteral("Rsi")) {
        auto s = std::make_shared<RsiStrategy>();
        s->buyLevel_ = p1_->value();
        s->sellLevel_ = p2_->value();
        return s;
    }
    // 默认回退双均线
    auto s = std::make_shared<MACrossStrategy>();
    s->fastPeriod_ = p1_->value();
    s->slowPeriod_ = p2_->value();
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
    // 名称映射（精选池静态表；非池内代码显示 "--"）
    std::unordered_map<std::string, std::string> nameByCode;
    const auto addTable = [&](Market m, const std::vector<CuratedStock>& table) {
        for (const auto& c : table) {
            nameByCode[StockCode(m, c.code).fullCode()] = c.name;
        }
    };
    addTable(Market::SH, kCuratedSH);
    addTable(Market::SZ, kCuratedSZ);
    tradeModel_->setNameByCode(std::move(nameByCode));
    tradeModel_->setTrades(result.trades);
    hasResult_ = true;
    lastPerf_ = result.performance;
    lastTrades_ = result.trades;
    LogManager::instance()->log(LogLevel::Info,
        "回测完成: 总收益 {:.2f}% 夏普 {:.2f} 交易 {} 笔",
        result.performance.totalReturn, result.performance.sharpeRatio,
        static_cast<int>(result.trades.size()));
}

void BacktestPanel::onExportClicked() {
    if (!hasResult_) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出回测结果"),
        QString::fromStdString(AppPaths::dataDir() + "/backtest.csv"),
        tr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    const auto fmt = [](double v, int prec) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(prec) << v;
        return s.str();
    };
    std::ostringstream os;
    const auto row = [&os](const std::vector<std::string>& cols) {
        os << csv::joinRow(cols) << '\n';
    };

    // 绩效
    os << "=== 绩效指标 ===\n";
    row({"总收益率(%)", fmt(lastPerf_.totalReturn, 2)});
    row({"年化收益率(%)", fmt(lastPerf_.annualReturn, 2)});
    row({"最大回撤(%)", fmt(lastPerf_.maxDrawdown, 2)});
    row({"夏普比率", fmt(lastPerf_.sharpeRatio, 2)});
    row({"胜率(%)", fmt(lastPerf_.winRate, 2)});
    row({"盈亏比", fmt(lastPerf_.profitFactor, 2)});
    row({"卡玛比率", fmt(lastPerf_.calmarRatio, 2)});
    row({"波动率(%)", fmt(lastPerf_.volatility, 2)});
    row({"索提诺比率", fmt(lastPerf_.sortinoRatio, 2)});
    row({"Alpha", fmt(lastPerf_.alpha, 4)});
    row({"Beta", fmt(lastPerf_.beta, 4)});
    row({"总交易次数", std::to_string(lastPerf_.totalTrades)});
    row({"盈利交易", std::to_string(lastPerf_.winningTrades)});
    row({"总盈亏", fmt(lastPerf_.totalPnl, 2)});

    // 净值曲线
    os << "\n=== 净值曲线 ===\n";
    row({"序号", "净值"});
    for (size_t i = 0; i < lastPerf_.equityCurve.size(); ++i) {
        row({std::to_string(i), fmt(lastPerf_.equityCurve[i], 4)});
    }

    // 成交明细
    os << "\n=== 成交明细 ===\n";
    row({"时间", "代码", "方向", "价格", "数量", "成交额", "费用"});
    for (const auto& t : lastTrades_) {
        row({utils::toDateTimeString(t.time),
             t.code.displayCode(),
             t.direction == Direction::Buy ? "买入" : "卖出",
             fmt(t.price, 2),
             std::to_string(t.volume),
             fmt(t.amount, 2),
             fmt(t.totalFee, 2)});
    }

    const std::string text = os.str();
    file.write(text.c_str(), static_cast<qint64>(text.size()));
    LogManager::instance()->log(LogLevel::Info, "已导出回测结果: {}", path.toStdString());
}

} // namespace st

#include "moc_backtest_panel.cpp"
