#include "ui/panels/advisor_panel.h"
#include "ui/strategy_catalog.h"
#include "ui/models/grid_search_table_model.h"
#include "ui/widgets/stock_pool_picker.h"
#include "intelligence/advisor/strategy_advisor.h"
#include "engine/optimizer/grid_search.h"
#include "engine/backtest/fee_calculator.h"
#include "engine/analyzer/monte_carlo.h"
#include "engine/analyzer/stress_test.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "data/parallel_bar_fetcher.h"
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
#include <QPlainTextEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHeaderView>
#include <QDate>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include <QVariantMap>
#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>

namespace st {

namespace {
constexpr char kUpColor[] = "#e54648";    // 红涨
constexpr char kDownColor[] = "#2e9e5b";  // 绿跌

QSpinBox* makeRangeSpin(QWidget* parent, int from, int to, int value) {
    auto* s = new QSpinBox(parent);
    s->setRange(from, to);
    s->setValue(value);
    // 不拉伸：行内保持自然宽度（避免被 QHBoxLayout 撑开造成标签间距）
    s->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return s;
}

std::string paramsText(const std::vector<std::pair<std::string, int>>& params) {
    std::ostringstream os;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) os << ", ";
        os << params[i].first << "=" << params[i].second;
    }
    return os.str();
}
}  // namespace

AdvisorPanel::AdvisorPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 配置区 ----
    auto* form = new QGroupBox(tr("参数网格 + 优化建议"));
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
            this, &AdvisorPanel::onStrategyChanged);

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

    // 股票池（全市场，异步加载 + 搜索过滤 + 多选）
    stockPicker_ = new StockPoolPicker(provider_, this);
    fl->addRow(labeled(new QLabel(tr("股票池")), stockPicker_));

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
    runBtn_ = new QPushButton(tr("开始优化建议"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    runRow->addWidget(progress_, 1);
    progressEtaLabel_ = new QLabel(tr(""), this);
    progressEtaLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    progressEtaLabel_->setVisible(false);
    runRow->addWidget(progressEtaLabel_);
    fl->addRow(runRow);
    layout->addWidget(form);
    connect(runBtn_, &QPushButton::clicked, this, &AdvisorPanel::onRunClicked);

    // ---- 建议区 ----
    auto* advBox = new QGroupBox(tr("参数建议"));
    auto* al = new QVBoxLayout(advBox);
    advParams_ = new QLabel(tr("—"));
    auto paramsFont = advParams_->font();
    paramsFont.setBold(true);
    advParams_->setFont(paramsFont);
    advConfidence_ = new QLabel;
    advWarnings_ = new QLabel;
    advStress_ = new QLabel(tr("压力测试: —"));
    advText_ = new QPlainTextEdit;
    advText_->setReadOnly(true);
    advText_->setMaximumHeight(110);
    refinedText_ = new QLabel(tr("—"));
    useRefinedBtn_ = new QPushButton(tr("用精化网格再优化"));
    auto* refinedRow = new QHBoxLayout;
    refinedRow->addWidget(refinedText_, 1);
    refinedRow->addWidget(useRefinedBtn_);
    al->addWidget(advParams_);
    al->addWidget(advConfidence_);
    al->addWidget(advWarnings_);
    al->addWidget(advStress_);
    al->addWidget(advText_);
    al->addLayout(refinedRow);
    layout->addWidget(advBox);
    connect(useRefinedBtn_, &QPushButton::clicked, this, &AdvisorPanel::onUseRefined);

    // ---- 结果表（列宽均分 + 单元格居中，仅本面板） ----
    resultModel_ = new GridSearchTableModel(this);
    resultView_ = new QTableView;
    resultView_->setModel(resultModel_);
    resultView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultView_->setMinimumHeight(160);
    layout->addWidget(new QLabel(tr("网格结果（单击行应用参数到回测）")));
    layout->addWidget(resultView_);
    connect(resultView_, &QTableView::clicked, this,
            [this](const QModelIndex& idx) {
                const auto params = resultModel_->paramsAt(idx.row());
                if (params.empty()) return;
                QVariantMap map;
                for (const auto& [name, val] : params) map[QString::fromStdString(name)] = val;
                emit applyParams(strategyCombo_->currentData().toString(), map);
            });

    layout->addStretch();
    scroll->setWidget(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    onStrategyChanged();
}

void AdvisorPanel::onStrategyChanged() {
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

Objective AdvisorPanel::currentObjective() const {
    switch (objectiveCombo_->currentIndex()) {
        case 1: return Objective::SharpeRatio;
        case 2: return Objective::MaxDrawdown;
        case 3: return Objective::CalmarRatio;
        case 4: return Objective::ProfitFactor;
        default: return Objective::TotalReturn;
    }
}

std::vector<StockCode> AdvisorPanel::selectedSymbols() const {
    return stockPicker_ ? stockPicker_->selectedSymbols() : std::vector<StockCode>{};
}

void AdvisorPanel::onRunClicked() {
    if (running_) return;
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "优化建议: 请至少选择一只股票");
        return;
    }
    running_ = true;
    runBtn_->setEnabled(false);
    useRefinedBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);
    progressEtaLabel_->setVisible(true);
    // 预估提示：组合数 × 股票数（全市场大池预计较久）
    {
        const int n1 = (p1To_->value() - p1From_->value()) / std::max(1, p1Step_->value()) + 1;
        const int n2 = (p2To_->value() - p2From_->value()) / std::max(1, p2Step_->value()) + 1;
        const int combos = n1 * n2;
        const int stocks = static_cast<int>(symbols.size());
        progressEtaLabel_->setText(tr("共 %1 组合 × %2 只（全市场大池预计较久）")
            .arg(combos).arg(stocks));
    }
    eta_.reset();
    cache_->clear();
    resultModel_->setResults({}, {}, {});

    const auto start = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    const auto end = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    // 压力测试默认窗口最早到 2015-01-01 → 加载范围提前，缓存多装历史。
    // BacktestEngine 按 config_.startDate/endDate 过滤（backtest_engine.cpp:200），
    // 不影响网格回测的日期范围。
    const auto loadStart = std::min(start, utils::parseDate("2015-01-01"));

    // 安全异步：按值捕获 provider + shared_ptr cache + QPointer 守卫（面板销毁后 cache 仍存活）
    IDataProvider* provider = provider_;
    const auto cache = cache_;
    QPointer<AdvisorPanel> guard(this);
    ThreadPool::submitIO([provider, cache, guard, symbols, loadStart, end] {
        // 并行拉取不复权日线（TDX 多连接）+ 收集真实价区间，替代旧的两次串行全市场拉取
        std::map<std::string, std::vector<Bar>> rawBars;
        ParallelBarFetcher::fetchRawBars(
            provider, symbols, BarPeriod::Daily, loadStart, loadStart, end, cache, rawBars, 4,
            [guard](int done, int total) {
                QMetaObject::invokeMethod(guard, [guard, done, total] {
                    if (!guard) return;
                    guard->progress_->setValue(done * 50 / total);
                    guard->progressEtaLabel_->setText(
                        guard->eta_.text(static_cast<double>(done) * 50.0 / total));
                }, Qt::QueuedConnection);
            });
        // 沪深300 基准（用于 Alpha/Beta）
        auto benchmarkBars = provider->getBars(
            StockCode(Market::SH, "000300"), BarPeriod::Daily, loadStart, end);
        QMetaObject::invokeMethod(guard, [guard, benchmarkBars = std::move(benchmarkBars),
                                          rawBars = std::move(rawBars)]() mutable {
            if (!guard) return;
            guard->onAllDataFetched(std::move(benchmarkBars), std::move(rawBars));
        }, Qt::QueuedConnection);
    });
}

void AdvisorPanel::onAllDataFetched(std::vector<Bar> benchmarkBars,
                                    std::map<std::string, std::vector<Bar>> rawBars) {
    progress_->setValue(50);
    eta_.reset();  // IO 阶段很快，进入计算阶段后重新估算剩余时间
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
    cfg.benchmarkBars = std::move(benchmarkBars);
    cfg.rawBars = std::move(rawBars);
    cfg.startDate = utils::parseDate(startDate_->date().toString(Qt::ISODate).toStdString());
    cfg.endDate = utils::parseDate(endDate_->date().toString(Qt::ISODate).toStdString());
    cfg.initialCapital = capital_->value();
    cfg.feeConfig = FeeConfig::defaultAShare();
    cfg.slippagePerShare = 0.01;  // 对齐聚宽默认 1 跳滑点
    cfg.objective = currentObjective();
    const auto cache = cache_;  // shared_ptr：worker 内 cfg.cache 指向它，面板销毁后仍存活
    cfg.cache = cache.get();
#ifdef _DEBUG
    cfg.parallelLanes = 2;
#else
    // Release：全市场大池并行 lane 上限 4（同 optimization_panel，控内存）
    cfg.parallelLanes = std::max(1, std::min(4, QThreadPool::globalInstance()->maxThreadCount()));
#endif

    const QString p1Name = p1Label_->text();
    const QString p2Name = p2Label_->text();

    // 安全异步：QPointer 守卫投递回主线程（面板销毁后自动跳过）；cache shared_ptr 按值捕获
    QPointer<AdvisorPanel> guard(this);
    ThreadPool::submitWorker([guard, cache, cfg = std::move(cfg), p1Name, p2Name]() mutable {
        // 两阶段进度映射（单调不倒退）：
        //   网格优化   0 ~ 90%   （原 50~100 → 完成后进度条会显示满，掩盖后续压力测试阶段）
        //   压力测试   90 ~ 100% （stress 回调同步更新 ETA）
        GridSearchOptimizer opt;
        opt.setProgressCallback([guard](double p) {
            QMetaObject::invokeMethod(guard, [guard, p] {
                if (!guard) return;
                guard->progress_->setValue(static_cast<int>(p * 90.0 / 100.0));
                guard->progressEtaLabel_->setText(guard->eta_.text(p));
            }, Qt::QueuedConnection);
        });
        auto results = opt.run(cfg);

        // 蒙特卡洛风险（从最优净值曲线派生日收益，避免重跑回测）
        std::optional<MonteCarlo::Output> mc;
        if (!results.empty()) {
            const auto& eq = results.front().equityCurve;
            std::vector<double> daily;
            if (eq.size() > 1) {
                daily.reserve(eq.size() - 1);
                for (size_t i = 1; i < eq.size(); ++i) {
                    if (eq[i - 1] > 0.0) daily.push_back(eq[i] / eq[i - 1] - 1.0);
                }
            }
            if (!daily.empty()) {
                mc = MonteCarlo::simulate({daily, 1000, 0, 1.0});
            }
        }

        // 压力测试：最优参数组跑历史极端窗口（2015股灾/2016熔断/2018熊市/2020疫情/2024微盘）。
        // 复用已加载缓存，纯计算无 IO；StrategyAdvisor::detectRisk 消费任一窗口回撤>20%。
        std::optional<StressTestOutput> stress;
        if (!results.empty()) {
            StressTestConfig scfg;
            scfg.strategyId = cfg.strategyId;
            scfg.params = results.front().params;
            scfg.symbols = cfg.symbols;
            scfg.initialCapital = cfg.initialCapital;
            scfg.feeConfig = cfg.feeConfig;
            scfg.slippagePerShare = cfg.slippagePerShare;
            scfg.benchmarkBars = cfg.benchmarkBars;
            scfg.rawBars = cfg.rawBars;
            scfg.cache = cfg.cache;
            scfg.baselineStart = utils::parseDate("2015-01-01");
            scfg.baselineEnd = cfg.endDate;
            StressTest st;
            st.setProgressCallback([guard](double p) {
                QMetaObject::invokeMethod(guard, [guard, p] {
                    if (!guard) return;
                    // 压力测试阶段映射 90~100，ETA 继续更新（显示真实剩余时间）
                    guard->progress_->setValue(90 + static_cast<int>(p * 10.0 / 100.0));
                    guard->progressEtaLabel_->setText(guard->eta_.text(p));
                }, Qt::QueuedConnection);
            });
            stress = st.run(scfg, StressTest::defaultWindows());
        }

        st::advisor::AdvisorContext actx;
        actx.strategyId = cfg.strategyId;
        actx.results = results;
        actx.monteCarlo = mc;
        actx.stressTest = stress;
        actx.objective = cfg.objective;
        st::advisor::StrategyAdvisor advisor;
        auto sug = advisor.advise(actx);
        auto refined = advisor.suggestRefinedRanges(actx);

        QMetaObject::invokeMethod(guard,
            [guard, results = std::move(results), sug = std::move(sug),
             refined = std::move(refined), stress = std::move(stress),
             p1Name, p2Name]() mutable {
                guard->onResult(results, sug, refined, stress, p1Name, p2Name);
            }, Qt::QueuedConnection);
    });
}

void AdvisorPanel::onResult(const std::vector<GridSearchResult>& results,
                            const st::advisor::AdvisorSuggestion& suggestion,
                            const std::vector<st::ParamRange>& refined,
                            const std::optional<StressTestOutput>& stress,
                            const QString& p1Name, const QString& p2Name) {
    running_ = false;
    runBtn_->setEnabled(true);
    useRefinedBtn_->setEnabled(!refined.empty());
    progress_->setValue(100);
    progress_->setVisible(false);

    resultModel_->setResults(results, p1Name, p2Name);
    displaySuggestion(suggestion);
    displayStress(stress);

    refinedRanges_ = refined;
    std::ostringstream os;
    for (const auto& r : refined) {
        os << r.name << " [" << r.from << ".." << r.to << "] ";
    }
    refinedText_->setText(refined.empty()
        ? tr("—") : tr("精化网格: %1").arg(QString::fromStdString(os.str())));

    LogManager::instance()->log(LogLevel::Info,
        "优化建议完成: 共 {} 组", results.size());
}

void AdvisorPanel::displaySuggestion(const st::advisor::AdvisorSuggestion& s) {
    if (!s.hasRecommendation) {
        advParams_->setText(tr("无建议"));
        advConfidence_->setText(QString());
        advWarnings_->setText(tr("无可用的回测结果"));
        advWarnings_->setStyleSheet(QString());
        advText_->setPlainText(QString());
        return;
    }
    advParams_->setText(QString::fromStdString(paramsText(s.recommendedParams)));
    advConfidence_->setText(tr("置信度: %1%")
        .arg(static_cast<int>(s.confidence * 100.0 + 0.5)));
    advConfidence_->setStyleSheet(QStringLiteral("color:%1;")
        .arg(s.confidence >= 0.6 ? kUpColor : kDownColor));

    QStringList warns;
    if (s.overfitWarning) warns << tr("过拟合");
    if (s.riskWarning) warns << tr("风险偏高");
    if (s.poorResultWarning) warns << tr("网格整体不佳");
    advWarnings_->setText(warns.isEmpty()
        ? tr("警告: 无") : tr("警告: %1").arg(warns.join(tr(" / "))));
    advWarnings_->setStyleSheet(warns.isEmpty()
        ? QString() : QStringLiteral("color:%1;").arg(kDownColor));

    advText_->setPlainText(QStringLiteral("%1\n%2")
        .arg(QString::fromStdString(s.text),
             QString::fromStdString(s.rationale)));
}

void AdvisorPanel::displayStress(const std::optional<StressTestOutput>& stress) {
    if (!advStress_) return;
    if (!stress.has_value()) {
        advStress_->setText(tr("压力测试: —"));
        advStress_->setStyleSheet(QString());
        return;
    }
    QStringList parts;
    bool anyRisk = false;
    for (const auto& w : stress->windows) {
        if (!w.success) continue;
        const double dd = w.performance.maxDrawdown;
        parts << QStringLiteral("%1 -%2%")
            .arg(QString::fromStdString(w.windowName),
                 QString::number(dd, 'f', 1));
        if (dd > 20.0) anyRisk = true;
    }
    if (parts.isEmpty()) {
        advStress_->setText(tr("压力测试: 无窗口数据（需 2015-01-01 起历史数据）"));
        advStress_->setStyleSheet(QString());
    } else {
        advStress_->setText(tr("压力测试: %1").arg(parts.join(" / ")));
        // 任一窗口回撤>20% 红字（与 riskWarning 口径一致）
        advStress_->setStyleSheet(anyRisk
            ? QStringLiteral("color:%1;").arg(kDownColor) : QString());
    }
}

void AdvisorPanel::fillRefinedRanges(const std::vector<st::ParamRange>& ranges) {
    if (ranges.size() >= 1) {
        p1From_->setValue(ranges[0].from);
        p1To_->setValue(ranges[0].to);
        p1Step_->setValue(ranges[0].step);
    }
    if (ranges.size() >= 2) {
        p2From_->setValue(ranges[1].from);
        p2To_->setValue(ranges[1].to);
        p2Step_->setValue(ranges[1].step);
    }
}

void AdvisorPanel::onUseRefined() {
    if (running_) return;
    fillRefinedRanges(refinedRanges_);
    onRunClicked();
}

void AdvisorPanel::resetToIdle() {
    running_ = false;
    runBtn_->setEnabled(true);
    useRefinedBtn_->setEnabled(true);
    progress_->setVisible(false);
    progressEtaLabel_->setVisible(false);
}

} // namespace st

#include "moc_advisor_panel.cpp"
