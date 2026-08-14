#include "ui/panels/strategy_compare_panel.h"
#include "ui/models/comparison_table_model.h"
#include "ui/widgets/equity_curve_widget.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "data/curated_stocks.h"
#include "engine/analyzer/monte_carlo.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QListWidget>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTableView>
#include <QLabel>
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

namespace {

struct Preset {
    const char* label;
    const char* id;
    std::vector<std::pair<std::string, int>> params;
};

// 内置策略预设（10 个：6 策略 × 代表性参数）
const std::vector<Preset> kPresets = {
    {"MA 5/20", "MACross", {{"fastPeriod", 5}, {"slowPeriod", 20}}},
    {"MA 10/60", "MACross", {{"fastPeriod", 10}, {"slowPeriod", 60}}},
    {"Turtle 20/10", "Turtle", {{"entryPeriod", 20}, {"exitPeriod", 10}}},
    {"Turtle 40/20", "Turtle", {{"entryPeriod", 40}, {"exitPeriod", 20}}},
    {"动量 20/10", "Momentum", {{"lookbackPeriod", 20}, {"exitPeriod", 10}}},
    {"动量 30/15", "Momentum", {{"lookbackPeriod", 30}, {"exitPeriod", 15}}},
    {"突破 20/10", "Breakout", {{"entryPeriod", 20}, {"exitPeriod", 10}}},
    {"均值回归 20/30", "MeanReversion", {{"maPeriod", 20}, {"deviationPct", 30}}},
    {"RSI 30/70", "Rsi", {{"buyLevel", 30}, {"sellLevel", 70}}},
    {"RSI 20/80", "Rsi", {{"buyLevel", 20}, {"sellLevel", 80}}},
};

// 曲线配色
const QColor kSeriesColors[] = {
    QColor("#e54648"), QColor("#4fc3f7"), QColor("#ffa726"),
    QColor("#4caf50"), QColor("#ffd54f"), QColor("#ab47bc"),
};

}  // namespace

StrategyComparePanel::StrategyComparePanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* form = new QGroupBox(tr("策略对比"));
    auto* fl = new QFormLayout(form);

    strategyList_ = new QListWidget;
    strategyList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    strategyList_->setMaximumHeight(100);
    for (size_t i = 0; i < kPresets.size(); ++i) {
        auto* item = new QListWidgetItem(QString::fromUtf8(kPresets[i].label));
        item->setData(Qt::UserRole, static_cast<int>(i));
        strategyList_->addItem(item);
    }
    for (int i = 0; i < 2; ++i) strategyList_->item(i)->setSelected(true);
    fl->addRow(tr("策略"), strategyList_);

    // 股票池（精选 129 只，多选）
    stockList_ = new QListWidget;
    stockList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    stockList_->setMaximumHeight(90);
    auto addPool = [this](Market m, const std::vector<CuratedStock>& table) {
        for (const auto& c : table) {
            auto* item = new QListWidgetItem(QStringLiteral("%1  %2")
                .arg(QString::fromUtf8(c.code), QString::fromUtf8(c.name)));
            item->setData(Qt::UserRole, QString::fromStdString(StockCode(m, c.code).fullCode()));
            item->setSelected(true);
            stockList_->addItem(item);
        }
    };
    addPool(Market::SH, kCuratedSH);
    addPool(Market::SZ, kCuratedSZ);
    fl->addRow(tr("股票池"), stockList_);

    startDate_ = new QDateEdit(QDate(2023, 1, 1));
    startDate_->setCalendarPopup(true);
    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    auto* dateRow = new QHBoxLayout;
    dateRow->addWidget(startDate_);
    dateRow->addWidget(new QLabel(tr("~")));
    dateRow->addWidget(endDate_);
    fl->addRow(tr("日期"), dateRow);

    capital_ = new QDoubleSpinBox;
    capital_->setRange(1000, 1e9);
    capital_->setValue(100000.0);
    capital_->setDecimals(0);
    capital_->setSingleStep(10000);
    fl->addRow(tr("初始资金"), capital_);

    auto* runRow = new QHBoxLayout;
    runBtn_ = new QPushButton(tr("开始对比"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(progress_, 1);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &StrategyComparePanel::onRunClicked);

    resultModel_ = new ComparisonTableModel(this);
    resultView_ = new QTableView;
    resultView_->setModel(resultModel_);
    resultView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultView_->setMinimumHeight(110);
    layout->addWidget(resultView_);

    // 净值叠加
    equityCurve_ = new EquityCurveWidget;
    equityCurve_->setMinimumHeight(150);
    layout->addWidget(equityCurve_);

    // 蒙特卡洛
    auto* mcBox = new QGroupBox(tr("蒙特卡洛（最优策略日收益重采样）"));
    auto* mg = new QGridLayout(mcBox);
    mcBtn_ = new QPushButton(tr("模拟 1000 次"));
    mg->addWidget(mcBtn_, 0, 0, 1, 2);
    const auto addMc = [&](int row, int col, const QString& name, QLabel*& out) {
        mg->addWidget(new QLabel(name), row, col * 2);
        out = new QLabel("--");
        out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        mg->addWidget(out, row, col * 2 + 1);
    };
    addMc(1, 0, tr("P5"), p5_);
    addMc(1, 1, tr("P50"), p50_);
    addMc(2, 0, tr("P95"), p95_);
    addMc(2, 1, tr("亏损概率"), probLoss_);
    layout->addWidget(mcBox);
    connect(mcBtn_, &QPushButton::clicked, this, &StrategyComparePanel::onMonteCarloClicked);

    layout->addStretch();
    scroll->setWidget(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

std::vector<StockCode> StrategyComparePanel::selectedSymbols() const {
    std::vector<StockCode> symbols;
    for (int i = 0; i < stockList_->count(); ++i) {
        auto* item = stockList_->item(i);
        if (item->isSelected()) {
            symbols.push_back(StockCode(item->data(Qt::UserRole).toString().toStdString()));
        }
    }
    return symbols;
}

std::vector<ComparisonItem> StrategyComparePanel::selectedItems() const {
    std::vector<ComparisonItem> items;
    for (int i = 0; i < strategyList_->count(); ++i) {
        auto* item = strategyList_->item(i);
        if (!item->isSelected()) continue;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= static_cast<int>(kPresets.size())) continue;
        const auto& p = kPresets[static_cast<size_t>(idx)];
        ComparisonItem ci;
        ci.label = p.label;
        ci.strategyId = p.id;
        ci.params = p.params;
        items.push_back(std::move(ci));
    }
    return items;
}

void StrategyComparePanel::onRunClicked() {
    if (running_) return;
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "策略对比: 请至少选择一只股票");
        return;
    }
    if (selectedItems().empty()) {
        LogManager::instance()->log(LogLevel::Warn, "策略对比: 请至少选择一个策略");
        return;
    }
    running_ = true;
    runBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    cache_->clear();
    resultModel_->setItems({});
    equityCurve_->setSeries({});
    p5_->setText("--");
    p50_->setText("--");
    p95_->setText("--");
    probLoss_->setText("--");

    const auto start = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    const auto end = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());

    // 安全异步：捕获 provider + shared_ptr cache + QPointer 守卫
    IDataProvider* provider = provider_;
    const auto cache = cache_;
    QPointer<StrategyComparePanel> guard(this);
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

void StrategyComparePanel::onAllDataFetched() {
    progress_->setValue(50);
    auto symbols = selectedSymbols();
    auto items = selectedItems();
    if (symbols.empty() || items.empty()) {
        resetToIdle();
        return;
    }

    ComparisonConfig cfg;
    cfg.items = std::move(items);
    cfg.symbols = std::move(symbols);
    cfg.startDate = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    cfg.endDate = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    cfg.initialCapital = capital_->value();
    cfg.feeConfig = FeeConfig::defaultAShare();
    const auto cache = cache_;  // shared_ptr：worker 内 cfg.cache 指向它，面板销毁后仍存活
    cfg.cache = cache.get();

    // 安全异步：QPointer 守卫 + shared_ptr cache
    QPointer<StrategyComparePanel> guard(this);
    ThreadPool::submitWorker([guard, cache, cfg = std::move(cfg)]() mutable {
        StrategyComparator comp;
        comp.setProgressCallback([guard](double p) {
            QMetaObject::invokeMethod(guard, [guard, p] {
                guard->progress_->setValue(50 + static_cast<int>(p * 50));
            }, Qt::QueuedConnection);
        });
        auto results = comp.run(cfg);
        QMetaObject::invokeMethod(guard, [guard, results = std::move(results)]() mutable {
            guard->onResult(results);
        }, Qt::QueuedConnection);
    });
}

void StrategyComparePanel::onResult(const std::vector<ComparisonItemResult>& items) {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setValue(100);
    progress_->setVisible(false);
    lastResults_ = items;

    resultModel_->setItems(items);

    // 净值叠加曲线
    std::vector<EquityCurveWidget::EquitySeries> series;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].success || items[i].equityCurve.empty()) continue;
        EquityCurveWidget::EquitySeries s;
        s.name = QString::fromStdString(items[i].item.label);
        s.color = kSeriesColors[i % (sizeof(kSeriesColors) / sizeof(QColor))];
        s.data = items[i].equityCurve;
        series.push_back(std::move(s));
    }
    equityCurve_->setSeries(series);

    LogManager::instance()->log(LogLevel::Info, "策略对比完成: {} 个策略",
                                items.size());
}

void StrategyComparePanel::onMonteCarloClicked() {
    if (lastResults_.empty() || !lastResults_.front().success) return;
    // 最优策略（按 totalReturn 降序，首项即最优）
    const auto& best = lastResults_.front();
    const std::vector<double> returns = best.performance.dailyReturns;

    // 安全异步：QPointer 守卫
    QPointer<StrategyComparePanel> guard(this);
    ThreadPool::submitWorker([guard, returns] {
        MonteCarlo::Input in;
        in.dailyReturns = returns;
        in.iterations = 1000;
        auto out = MonteCarlo::simulate(in);
        QMetaObject::invokeMethod(guard, [guard, out = std::move(out)]() mutable {
            guard->p5_->setText(QString::number(out.p5, 'f', 3));
            guard->p50_->setText(QString::number(out.p50, 'f', 3));
            guard->p95_->setText(QString::number(out.p95, 'f', 3));
            guard->probLoss_->setText(
                QStringLiteral("%1%").arg(out.probOfLoss * 100.0, 0, 'f', 1));
        }, Qt::QueuedConnection);
    });
}

void StrategyComparePanel::resetToIdle() {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setVisible(false);
}

} // namespace st

#include "moc_strategy_compare_panel.cpp"
