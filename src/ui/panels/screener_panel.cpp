#include "ui/panels/screener_panel.h"
#include "ui/models/screen_result_model.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "data/curated_stocks.h"
#include "engine/screener/stock_screener.h"
#include "engine/screener/factor_library.h"
#include "intelligence/screener/pattern_factor.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QCheckBox>
#include <QListWidget>
#include <QSpinBox>
#include <QDateEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QTableView>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QHeaderView>
#include <QDate>
#include <QMetaObject>
#include <QPointer>
#include <unordered_map>
#include <algorithm>

namespace st {

namespace {

// 因子英文名 → 中文显示名
QString factorDisplayName(const std::string& name) {
    static const std::unordered_map<std::string, const char*> kNames = {
        {"roc_20", "20日动量"}, {"rsi_14", "RSI(14)"}, {"macd_hist", "MACD柱"},
        {"volatility", "年化波动率"}, {"atr_14", "ATR(14)"}, {"max_drawdown", "最大回撤"},
        {"ma_alignment", "均线多头"}, {"adx_14", "ADX(14)"}, {"volume_ratio", "量比"},
        {"turnover", "换手率"}, {"obv", "OBV"},
        {"pattern_score", "形态评分"},
    };
    auto it = kNames.find(name);
    return it != kNames.end()
        ? QStringLiteral("%1 (%2)").arg(QString::fromUtf8(it->second),
                                        QString::fromStdString(name))
        : QString::fromStdString(name);
}

}  // namespace

ScreenerPanel::ScreenerPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 选股配置 ----
    auto* form = new QGroupBox(tr("选股配置"));
    auto* fl = new QFormLayout(form);

    // 因子区（2 列勾选）
    candidateFactors_ = factors::defaultFactorSet();
    // 形态因子（Intelligence 下接选股面板，权重 1.0）
    candidateFactors_.emplace_back(std::make_shared<st::screener::PatternFactor>(), 1.0);
    auto* factorBox = new QWidget;
    auto* fg = new QGridLayout(factorBox);
    fg->setContentsMargins(0, 0, 0, 0);
    fg->setSpacing(2);
    for (size_t i = 0; i < candidateFactors_.size(); ++i) {
        auto* cb = new QCheckBox(factorDisplayName(candidateFactors_[i].first->name()));
        cb->setChecked(true);
        factorChecks_.push_back(cb);
        fg->addWidget(cb, static_cast<int>(i) / 2, static_cast<int>(i) % 2);
    }
    fl->addRow(tr("因子"), factorBox);

    // 股票池（精选 129 只，多选）
    stockList_ = new QListWidget;
    stockList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    stockList_->setMaximumHeight(120);
    auto addPool = [this](Market m, const std::vector<CuratedStock>& table) {
        for (const auto& c : table) {
            auto* item = new QListWidgetItem(QStringLiteral("%1  %2")
                .arg(QString::fromUtf8(c.code), QString::fromUtf8(c.name)));
            item->setData(Qt::UserRole, QString::fromStdString(StockCode(m, c.code).fullCode()));
            stockList_->addItem(item);
        }
    };
    addPool(Market::SH, kCuratedSH);
    addPool(Market::SZ, kCuratedSZ);
    for (int i = 0; i < std::min(3, stockList_->count()); ++i) {
        stockList_->item(i)->setSelected(true);
    }
    fl->addRow(tr("股票池"), stockList_);

    topN_ = new QSpinBox;
    topN_->setRange(5, 129);
    topN_->setValue(50);
    fl->addRow(tr("输出前N"), topN_);

    lookback_ = new QSpinBox;
    lookback_->setRange(30, 750);
    lookback_->setValue(250);
    fl->addRow(tr("回看天数"), lookback_);

    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    fl->addRow(tr("截止日期"), endDate_);

    auto* runRow = new QHBoxLayout;
    runBtn_ = new QPushButton(tr("开始选股"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(progress_, 1);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &ScreenerPanel::onRunClicked);

    // ---- 结果表 ----
    resultModel_ = new ScreenResultModel(this);
    resultView_ = new QTableView;
    resultView_->setModel(resultModel_);
    resultView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultView_->horizontalHeader()->setStretchLastSection(true);
    resultView_->setMinimumHeight(180);
    layout->addWidget(new QLabel(tr("选股结果（双击开图）")));
    layout->addWidget(resultView_);

    layout->addStretch();
    scroll->setWidget(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(resultView_, &QTableView::doubleClicked, this,
            [this](const QModelIndex& idx) {
                const auto* r = resultModel_->resultAt(idx.row());
                if (r) emit openChart(r->code);
            });
}

std::vector<StockCode> ScreenerPanel::selectedSymbols() const {
    std::vector<StockCode> symbols;
    for (int i = 0; i < stockList_->count(); ++i) {
        auto* item = stockList_->item(i);
        if (item->isSelected()) {
            symbols.push_back(StockCode(item->data(Qt::UserRole).toString().toStdString()));
        }
    }
    return symbols;
}

void ScreenerPanel::onRunClicked() {
    if (running_) return;
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "选股: 请至少选择一只股票");
        return;
    }
    running_ = true;
    runBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    cache_->clear();
    resultModel_->setResults({}, {}, {});

    const auto end = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    const auto start = utils::addTradingDays(end, -lookback_->value());

    // ① IO 池拉数据 → 缓存（安全异步：捕获 provider + shared_ptr cache + QPointer 守卫）
    IDataProvider* provider = provider_;
    const auto cache = cache_;
    QPointer<ScreenerPanel> guard(this);
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

void ScreenerPanel::onAllDataFetched() {
    progress_->setValue(50);
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        resetToIdle();
        return;
    }

    // 收集勾选的因子
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> selected;
    std::vector<std::string> factorNames;
    for (size_t i = 0; i < factorChecks_.size(); ++i) {
        if (factorChecks_[i]->isChecked() && i < candidateFactors_.size()) {
            selected.push_back(candidateFactors_[i]);
            factorNames.push_back(candidateFactors_[i].first->name());
        }
    }
    if (selected.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "选股: 请至少勾选一个因子");
        resetToIdle();
        return;
    }

    const auto end = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    ScreenerConfig cfg;
    cfg.period = BarPeriod::Daily;
    cfg.endDate = end;
    cfg.lookbackDays = lookback_->value();
    cfg.topN = topN_->value();

    // ② Worker 池选股
    ThreadPool::submitWorker(
        [this, selected = std::move(selected), factorNames, symbols, cfg]() mutable {
            StockScreener screener;
            screener.setConfig(cfg);
            screener.setDataCache(cache_.get());
            for (auto& [f, w] : selected) screener.addFactor(f, w);
            screener.setProgressCallback([this](double p) {
                QMetaObject::invokeMethod(this, [this, p] {
                    progress_->setValue(50 + static_cast<int>(p * 50));
                }, Qt::QueuedConnection);
            });
            auto results = screener.run(symbols);
            QMetaObject::invokeMethod(this,
                [this, results = std::move(results), factorNames]() mutable {
                    onResult(results, factorNames);
                }, Qt::QueuedConnection);
        });
}

void ScreenerPanel::onResult(const std::vector<ScreenResult>& results,
                             const std::vector<std::string>& factorNames) {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setValue(100);
    progress_->setVisible(false);

    std::unordered_map<std::string, std::string> nameByCode;
    const auto addTable = [&](Market m, const std::vector<CuratedStock>& table) {
        for (const auto& c : table) {
            nameByCode[StockCode(m, c.code).fullCode()] = c.name;
        }
    };
    addTable(Market::SH, kCuratedSH);
    addTable(Market::SZ, kCuratedSZ);

    resultModel_->setResults(results, factorNames, nameByCode);
    LogManager::instance()->log(LogLevel::Info, "选股完成: {} 只股票进入排名",
                                results.size());
}

void ScreenerPanel::resetToIdle() {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setVisible(false);
}

} // namespace st

#include "moc_screener_panel.cpp"
