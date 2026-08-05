#include "ui/panels/pattern_panel.h"
#include "ui/panels/stock_search_bar.h"
#include "ui/widgets/kline_chart.h"
#include "ui/models/pattern_table_model.h"
#include "intelligence/pattern/pattern_recognizer.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QPushButton>
#include <QProgressBar>
#include <QTableView>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QPointer>
#include <utility>

namespace st {

PatternPanel::PatternPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_unique<DataCache>()) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* topRow = new QHBoxLayout;
    searchBar_ = new StockSearchBar(provider_);
    runBtn_ = new QPushButton(tr("重新检测"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    topRow->addWidget(searchBar_, 1);
    topRow->addWidget(runBtn_);
    topRow->addWidget(progress_);
    layout->addLayout(topRow);

    resultModel_ = new PatternTableModel(this);
    resultView_ = new QTableView;
    resultView_->setModel(resultModel_);
    resultView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultView_->horizontalHeader()->setStretchLastSection(true);
    resultView_->setMinimumHeight(140);

    chart_ = new KLineChart(provider_);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(resultView_);
    splitter->addWidget(chart_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    connect(searchBar_, &StockSearchBar::stockSelected,
            this, &PatternPanel::onStockSelected);
    connect(runBtn_, &QPushButton::clicked, this, &PatternPanel::onRunClicked);
    connect(resultView_, &QTableView::doubleClicked, this,
            [this](const QModelIndex&) {
                if (code_.code().empty()) return;
                emit openChart(code_);
            });
}

void PatternPanel::onStockSelected(const StockInfo& info) {
    code_ = info.code;
    name_ = QString::fromUtf8(info.name.c_str(), static_cast<int>(info.name.size()));
    chart_->loadStock(code_, name_);
    startDetect();
}

void PatternPanel::onRunClicked() {
    if (running_) return;
    if (code_.code().empty()) {
        LogManager::instance()->log(LogLevel::Warn, "形态识别: 请先选择股票");
        return;
    }
    startDetect();
}

void PatternPanel::startDetect() {
    const int gen = ++detectGen_;
    running_ = true;
    runBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    cache_->clear();
    resultModel_->setRows({});

    const auto end = utils::now();
    const auto start = utils::addTradingDays(end, -120);  // 约 120 交易日
    const StockCode code = code_;
    // 安全异步：按值捕获 provider + QPointer 守卫投递回主线程
    IDataProvider* provider = provider_;
    DataCache* cache = cache_.get();
    QPointer<PatternPanel> guard(this);
    ThreadPool::submitIO([provider, cache, guard, gen, code, start, end] {
        auto bars = provider->getBars(code, BarPeriod::Daily, start, end);
        if (!bars.empty()) cache->cacheBars(code, BarPeriod::Daily, std::move(bars));
        QMetaObject::invokeMethod(guard, [guard, gen] {
            if (gen == guard->detectGen_) guard->onAllDataFetched(gen);
        }, Qt::QueuedConnection);
    });
}

void PatternPanel::onAllDataFetched(int gen) {
    progress_->setValue(50);
    const auto bars = cache_->getBars(code_, BarPeriod::Daily);
    QPointer<PatternPanel> guard(this);
    ThreadPool::submitWorker([guard, gen, bars = std::move(bars)] {
        std::vector<PatternTableModel::Row> rows;
        if (bars.size() >= 40) {
            st::pattern::PatternRecognizer rec;
            rec.setMinBars(40);
            BarSeries series(bars);
            const auto result = rec.detect(series);
            rows.reserve(result.items.size());
            for (const auto& sig : result.items) {
                PatternTableModel::Row row;
                row.date = QString::fromStdString(utils::toDateString(bars[sig.index].time));
                row.name = QString::fromStdString(st::pattern::PatternRecognizer::typeName(sig.type));
                row.confidence = sig.confidence;
                if (st::pattern::PatternRecognizer::isBullish(sig.type)) {
                    row.direction = PatternTableModel::Direction::Bullish;
                } else if (st::pattern::PatternRecognizer::isBearish(sig.type)) {
                    row.direction = PatternTableModel::Direction::Bearish;
                } else {
                    row.direction = PatternTableModel::Direction::Neutral;
                }
                row.description = QString::fromStdString(sig.description);
                rows.push_back(std::move(row));
            }
        }
        QMetaObject::invokeMethod(guard, [guard, gen, rows = std::move(rows)] {
            if (gen == guard->detectGen_) guard->onResult(rows);
        }, Qt::QueuedConnection);
    });
}

void PatternPanel::onResult(const std::vector<PatternTableModel::Row>& rows) {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setValue(100);
    progress_->setVisible(false);
    resultModel_->setRows(rows);
    LogManager::instance()->log(LogLevel::Info,
        "形态识别完成: {} 条信号", rows.size());
}

void PatternPanel::resetToIdle() {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setVisible(false);
}

} // namespace st

#include "moc_pattern_panel.cpp"
