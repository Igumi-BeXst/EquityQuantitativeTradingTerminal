#include "ui/panels/optimization_panel.h"
#include "ui/strategy_catalog.h"
#include "ui/models/grid_search_table_model.h"
#include "ui/widgets/grid_heatmap_widget.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "data/curated_stocks.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTableView>
#include <QListWidget>
#include <QTabWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHeaderView>
#include <QDate>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace st {

namespace {
QSpinBox* makeRangeSpin(QWidget* parent, int from, int to, int value) {
    auto* s = new QSpinBox(parent);
    s->setRange(from, to);
    s->setValue(value);
    // 不拉伸：行内保持自然宽度（避免被 QHBoxLayout 撑开造成标签间距）
    s->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return s;
}
}  // namespace

OptimizationPanel::OptimizationPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* form = new QGroupBox(tr("参数优化"));
    auto* fl = new QFormLayout(form);
    fl->setVerticalSpacing(6);
    // 标签+控件紧贴行（无标签列，间距 6px；控件拉伸占满保持宽度）
    auto labeled = [](QLabel* label, QWidget* field) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addWidget(label);
        row->addWidget(field, 1);
        return row;
    };
    auto labeledL = [](QLabel* label, QLayout* field) {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        row->addWidget(label);
        row->addLayout(field, 1);
        return row;
    };

    strategyCombo_ = new QComboBox;
    for (const auto& s : strategy_catalog::all()) {
        strategyCombo_->addItem(QStringLiteral("[%1] %2").arg(s.category, s.display),
                                s.id);
    }
    fl->addRow(labeled(new QLabel(tr("策略")), strategyCombo_));
    connect(strategyCombo_, &QComboBox::currentIndexChanged,
            this, &OptimizationPanel::onStrategyChanged);

    auto makeRangeRow = [&](QSpinBox*& from, QSpinBox*& to, QSpinBox*& step) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("从")));
        from = makeRangeSpin(this, 1, 500, 2);
        row->addWidget(from, 1);
        row->addWidget(new QLabel(tr("到")));
        to = makeRangeSpin(this, 2, 600, 30);
        row->addWidget(to, 1);
        row->addWidget(new QLabel(tr("步长")));
        step = makeRangeSpin(this, 1, 100, 2);
        row->addWidget(step, 1);
        return row;
    };
    p1Label_ = new QLabel(tr("快线周期"));
    fl->addRow(labeledL(p1Label_, makeRangeRow(p1From_, p1To_, p1Step_)));
    p2Label_ = new QLabel(tr("慢线周期"));
    fl->addRow(labeledL(p2Label_, makeRangeRow(p2From_, p2To_, p2Step_)));

    objectiveCombo_ = new QComboBox;
    objectiveCombo_->addItem(tr("总收益"));
    objectiveCombo_->addItem(tr("夏普"));
    objectiveCombo_->addItem(tr("最大回撤"));
    objectiveCombo_->addItem(tr("卡玛"));
    objectiveCombo_->addItem(tr("盈亏比"));
    fl->addRow(labeled(new QLabel(tr("目标函数")), objectiveCombo_));

    // 股票池
    stockList_ = new QListWidget;
    stockList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    stockList_->setMaximumHeight(110);
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
    fl->addRow(labeled(new QLabel(tr("股票池")), stockList_));

    startDate_ = new QDateEdit(QDate(2023, 1, 1));
    startDate_->setCalendarPopup(true);
    startDate_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    endDate_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* dateRow = new QHBoxLayout;
    dateRow->addWidget(startDate_, 1);
    dateRow->addWidget(new QLabel(tr("~")));
    dateRow->addWidget(endDate_, 1);
    fl->addRow(labeledL(new QLabel(tr("日期")), dateRow));

    capital_ = new QDoubleSpinBox;
    capital_->setRange(1000, 1e9);
    capital_->setValue(100000.0);
    capital_->setDecimals(0);
    capital_->setSingleStep(10000);
    capital_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    fl->addRow(labeled(new QLabel(tr("初始资金")), capital_));

    auto* runRow = new QHBoxLayout;
    runBtn_ = new QPushButton(tr("开始优化"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(progress_, 1);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &OptimizationPanel::onRunClicked);

    resultModel_ = new GridSearchTableModel(this);
    resultView_ = new QTableView;
    resultView_->setModel(resultModel_);
    resultView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultView_->setMinimumHeight(180);

    // 结果表 ↔ 热力图 双视图
    resultTabs_ = new QTabWidget;
    resultTabs_->addTab(resultView_, tr("结果表"));
    heatmap_ = new GridHeatmapWidget;
    resultTabs_->addTab(heatmap_, tr("热力图"));
    resultTabs_->setTabEnabled(1, false);   // 单参数/空结果时禁用
    resultInfoLabel_ = new QLabel(tr("尚未运行优化"));
    resultInfoLabel_->setWordWrap(true);
    resultInfoLabel_->setStyleSheet(QStringLiteral("color:#999999;"));
    layout->addWidget(resultInfoLabel_);
    layout->addWidget(new QLabel(tr("优化结果（单击行/双击热力图格应用参数到回测）")));
    layout->addWidget(resultTabs_);

    layout->addStretch();
    scroll->setWidget(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(resultView_, &QTableView::clicked, this,
            [this](const QModelIndex& idx) {
                const auto params = resultModel_->paramsAt(idx.row());
                if (params.empty()) return;
                QVariantMap map;
                for (const auto& [name, val] : params) map[QString::fromStdString(name)] = val;
                emit applyParams(strategyCombo_->currentData().toString(), map);
            });
    // 热力图双击格子 → 应用该组参数到回测面板
    connect(heatmap_, &GridHeatmapWidget::cellActivated, this,
            [this](double x, double y) {
                if (lastP1Param_.isEmpty() || lastP2Param_.isEmpty()) return;
                QVariantMap map;
                map[lastP1Param_] = static_cast<int>(std::lround(x));
                map[lastP2Param_] = static_cast<int>(std::lround(y));
                emit applyParams(strategyCombo_->currentData().toString(), map);
            });

    onStrategyChanged();
}

void OptimizationPanel::onStrategyChanged() {
    const auto* s = strategy_catalog::byId(strategyCombo_->currentData().toString());
    if (!s) return;
    p1Label_->setText(s->p1Name);
    p2Label_->setText(s->p2Name);
    // 搜索范围：默认值 ~ 默认值+20（步长 2），避免组合爆炸
    p1From_->setRange(s->p1Min, s->p1Max);
    p1To_->setRange(s->p1Min, s->p1Max);
    p2From_->setRange(s->p2Min, s->p2Max);
    p2To_->setRange(s->p2Min, s->p2Max);
    p1From_->setValue(s->p1);
    p1To_->setValue(std::min(s->p1Max, s->p1 + 20));
    p1Step_->setValue(2);
    p2From_->setValue(s->p2);
    p2To_->setValue(std::min(s->p2Max, s->p2 + 20));
    p2Step_->setValue(2);
}

Objective OptimizationPanel::currentObjective() const {
    switch (objectiveCombo_->currentIndex()) {
        case 1: return Objective::SharpeRatio;
        case 2: return Objective::MaxDrawdown;
        case 3: return Objective::CalmarRatio;
        case 4: return Objective::ProfitFactor;
        default: return Objective::TotalReturn;
    }
}

std::vector<StockCode> OptimizationPanel::selectedSymbols() const {
    std::vector<StockCode> symbols;
    for (int i = 0; i < stockList_->count(); ++i) {
        auto* item = stockList_->item(i);
        if (item->isSelected()) {
            symbols.push_back(StockCode(item->data(Qt::UserRole).toString().toStdString()));
        }
    }
    return symbols;
}

void OptimizationPanel::onRunClicked() {
    if (running_) return;
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "参数优化: 请至少选择一只股票");
        return;
    }
    running_ = true;
    runBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    cache_->clear();
    resultModel_->setResults({}, {}, {});

    // 结果上下文信息行：明确本结果对应的股票池 / 目标函数 / 日期区间
    {
        std::unordered_map<std::string, std::string> nameByCode;
        const auto addTable = [&](Market m, const std::vector<CuratedStock>& table) {
            for (const auto& c : table) {
                nameByCode[StockCode(m, c.code).fullCode()] = c.name;
            }
        };
        addTable(Market::SH, kCuratedSH);
        addTable(Market::SZ, kCuratedSZ);
        QStringList names;
        for (const auto& code : symbols) {
            auto it = nameByCode.find(code.fullCode());
            names << (it != nameByCode.end()
                          ? QString::fromUtf8(it->second.c_str())
                          : QString::fromStdString(code.displayCode()));
        }
        const QString poolText = names.size() > 3
            ? names.mid(0, 3).join(QStringLiteral("、"))
                  + QStringLiteral(" 等 %1 只").arg(names.size())
            : names.join(QStringLiteral("、"));
        resultInfoLabel_->setText(tr("股票池: %1 · 目标: %2 · 区间: %3 ~ %4")
            .arg(poolText, objectiveCombo_->currentText(),
                 startDate_->date().toString(Qt::ISODate),
                 endDate_->date().toString(Qt::ISODate)));
    }

    const auto start = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    const auto end = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());

    // 安全异步：按值捕获 provider + shared_ptr cache + QPointer 守卫（面板销毁后 cache 仍存活）
    IDataProvider* provider = provider_;
    const auto cache = cache_;
    QPointer<OptimizationPanel> guard(this);
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

void OptimizationPanel::onAllDataFetched() {
    progress_->setValue(50);
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        resetToIdle();
        return;
    }

    GridSearchConfig cfg;
    cfg.strategyId = strategyCombo_->currentData().toString().toStdString();
    const auto* spec = strategy_catalog::byId(strategyCombo_->currentData().toString());
    const QString p1Key = spec ? spec->p1Key : QStringLiteral("fastPeriod");
    const QString p2Key = spec ? spec->p2Key : QStringLiteral("slowPeriod");
    cfg.ranges = {
        {p1Key.toStdString(), p1From_->value(), p1To_->value(), p1Step_->value()},
        {p2Key.toStdString(), p2From_->value(), p2To_->value(), p2Step_->value()},
    };
    cfg.symbols = symbols;
    cfg.startDate = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    cfg.endDate = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    cfg.initialCapital = capital_->value();
    cfg.feeConfig = FeeConfig::defaultAShare();
    cfg.objective = currentObjective();
    const auto cache = cache_;  // shared_ptr：worker 内 cfg.cache 指向它，面板销毁后仍存活
    cfg.cache = cache.get();
#ifdef _DEBUG
    // Debug CRT 堆有全局锁，多线程抢锁反而比单线程慢（实测 8 线程 = 单线程 2.3 倍耗时）
    cfg.parallelLanes = 2;
#else
    cfg.parallelLanes = std::max(1, QThreadPool::globalInstance()->maxThreadCount());
#endif

    const QString p1Name = p1Label_->text();
    const QString p2Name = p2Label_->text();
    const QString p1Param = QString::fromStdString(cfg.ranges[0].name);
    const QString p2Param = QString::fromStdString(cfg.ranges[1].name);

    // 安全异步：QPointer 守卫 + cache shared_ptr 按值捕获
    QPointer<OptimizationPanel> guard(this);
    ThreadPool::submitWorker(
        [guard, cache, cfg = std::move(cfg), p1Name, p2Name, p1Param, p2Param]() mutable {
        GridSearchOptimizer opt;
        opt.setProgressCallback([guard](double p) {
            QMetaObject::invokeMethod(guard, [guard, p] {
                guard->progress_->setValue(50 + static_cast<int>(p * 50));
            }, Qt::QueuedConnection);
        });
        auto results = opt.run(cfg);
        QMetaObject::invokeMethod(guard, [guard, results = std::move(results),
                                        p1Name, p2Name, p1Param, p2Param]() mutable {
            guard->onResult(results, p1Name, p2Name, p1Param, p2Param);
        }, Qt::QueuedConnection);
    });
}

void OptimizationPanel::onResult(const std::vector<GridSearchResult>& results,
                                 const QString& p1Name, const QString& p2Name,
                                 const QString& p1Param, const QString& p2Param) {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setValue(100);
    progress_->setVisible(false);

    resultModel_->setResults(results, p1Name, p2Name);
    lastP1Param_ = p1Param;
    lastP2Param_ = p2Param;

    // 热力图：两参数且 ≥2 组结果才可用；最优组合白框高亮
    heatmap_->clear();
    const bool heatmapUsable = results.size() > 1;
    if (heatmapUsable) {
        auto hm = buildHeatmap(results, p1Param.toStdString(), p2Param.toStdString());
        if (hm) {
            double bestX = 0.0, bestY = 0.0;
            for (const auto& [name, val] : results.front().params) {
                if (name == p1Param.toStdString()) bestX = static_cast<double>(val);
                else if (name == p2Param.toStdString()) bestY = static_cast<double>(val);
            }
            const bool betterIsHigher = !GridSearchOptimizer::objectiveMinimized(currentObjective());
            const QString objName = objectiveCombo_->currentText();
            heatmap_->setData(*hm, bestX, bestY, betterIsHigher,
                              p1Name, p2Name, objName);
        }
    }
    resultTabs_->setTabEnabled(1, heatmapUsable && heatmap_->hasData());

    if (!results.empty()) {
        const auto& best = results.front();
        LogManager::instance()->log(LogLevel::Info,
            "参数优化完成: 共 {} 组，最优目标值 {:.2f}",
            results.size(), best.objectiveValue);
    }
}

void OptimizationPanel::resetToIdle() {
    running_ = false;
    runBtn_->setEnabled(true);
    progress_->setVisible(false);
}

} // namespace st

#include "moc_optimization_panel.cpp"
