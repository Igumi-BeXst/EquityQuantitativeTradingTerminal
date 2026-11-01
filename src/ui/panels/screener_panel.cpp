#include "ui/panels/screener_panel.h"
#include "ui/utils/table_csv_export.h"
#include "ui/models/screen_result_model.h"
#include "ui/widgets/stock_pool_picker.h"
#include "data/idata_provider.h"
#include "data/data_cache.h"
#include "engine/screener/stock_screener.h"
#include "engine/screener/factor_library.h"
#include "intelligence/screener/composite_screener.h"
#include "intelligence/sentiment/sentiment_analyzer.h"
#include "intelligence/sentiment/eastmoney_news_provider.h"
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
#include <numeric>

namespace st {

ScreenerPanel::ScreenerPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider), cache_(std::make_shared<DataCache>()) {
    newsProvider_ = std::make_shared<st::sentiment::EastMoneyNewsProvider>();
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 选股配置 ----
    auto* form = new QGroupBox(tr("选股配置"));
    auto* fl = new QFormLayout(form);

    // 因子区：按 Alpha191 / 量价 / 基本面 三分组展示（组内等权，三组各自总权重 1.0）
    candidateFactors_ = factors::defaultFactorSet();
    factorChecks_.assign(candidateFactors_.size(), nullptr);
    const std::vector<FactorCategory> categories = {
        FactorCategory::Alpha191, FactorCategory::VolumePrice, FactorCategory::Fundamental};
    for (FactorCategory category : categories) {
        auto* factorBox = new QWidget;
        auto* fg = new QGridLayout(factorBox);
        fg->setContentsMargins(0, 0, 0, 0);
        fg->setSpacing(2);
        int gridRow = 0;
        int gridCol = 0;
        size_t count = 0;
        const int maxCols = category == FactorCategory::Alpha191 ? 8 : 4;
        for (size_t i = 0; i < candidateFactors_.size(); ++i) {
            const auto& factor = candidateFactors_[i].first;
            if (factor->category() != category) continue;
            auto* cb = new QCheckBox(QString::fromStdString(factor->displayName()));
            cb->setChecked(false);  // 默认不勾选：由用户显式选择参与打分的因子
            cb->setToolTip(QString::fromStdString(factor->description()));
            connect(cb, &QCheckBox::toggled, this, [this] { updateRunButton(); });
            factorChecks_[i] = cb;
            fg->addWidget(cb, gridRow, gridCol);
            if (++gridCol == maxCols) {
                gridCol = 0;
                ++gridRow;
            }
            ++count;
        }
        const QString label = QStringLiteral("%1 (%2)")
            .arg(QString::fromStdString(factors::categoryName(category)))
            .arg(count);
        fl->addRow(label, factorBox);
    }
    auto* factorHint = new QLabel(
        tr("因子默认不勾选；请至少选择 1 个因子后开始选股（三组各自总权重 1/3）"));
    factorHint->setStyleSheet(QStringLiteral("color:#888888;"));
    fl->addRow(QString(), factorHint);

    // 综合评分分项（K线形态 + 舆情情绪；技术指标恒参与；舆情网络拉取限量）
    auto* compositeBox = new QWidget;
    auto* compositeRow = new QHBoxLayout(compositeBox);
    compositeRow->setContentsMargins(0, 0, 0, 0);
    compositeRow->setSpacing(8);
    compositePatternCheck_ = new QCheckBox(tr("K线形态"));
    compositePatternCheck_->setChecked(true);
    compositePatternCheck_->setToolTip(tr(
        "近 3 根 K 线的形态识别（如吞没/锤头/三兵等）参与综合评分。\n"
        "与上方因子区的 Alpha191 / 量价 / 基本面因子相互独立，不影响因子打分。"));
    compositeSentimentCheck_ = new QCheckBox(tr("舆情情绪"));
    compositeSentimentCheck_->setChecked(true);
    compositeSentimentCheck_->setToolTip(tr(
        "东财新闻的舆情情绪参与综合评分。\n"
        "仅拉取股票池顺序前 30 只的新闻（非按因子得分排名）；其余股票该分项缺失，\n"
        "综合评分按剩余分项权重折减，不会阻塞选股。"));
    auto* compositePrefix = new QLabel(tr("分项:"));
    compositePrefix->setToolTip(tr(
        "综合评分（0~100）= K线形态 0.4 + 舆情情绪 0.3 + 技术指标 0.3 加权；\n"
        "缺失分项在剩余分项间归一化权重，技术指标恒参与。"));
    compositeRow->addWidget(compositePrefix);
    compositeRow->addWidget(compositePatternCheck_);
    compositeRow->addWidget(compositeSentimentCheck_);
    auto* compositeHint = new QLabel(tr("（勾选后结果表加「综合评分」列并按分降序重排；技术指标恒参与；舆情仅取池前 30 只，其余缺失分项折减）"));
    compositeHint->setStyleSheet(QStringLiteral("color:#888888;"));
    compositeRow->addWidget(compositeHint);
    compositeRow->addStretch();
    fl->addRow(tr("综合评分"), compositeBox);

    // 股票池（全市场，异步加载 + 搜索过滤 + 多选）
    stockPicker_ = new StockPoolPicker(provider_, this);
    fl->addRow(tr("股票池"), stockPicker_);

    topN_ = new QSpinBox;
    topN_->setRange(5, 1000);
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
    runBtn_->setToolTip(tr("请至少勾选一个因子"));
    updateRunButton();  // 未选因子 → 禁用开始按钮
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    runRow->addWidget(runBtn_);
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        st::ui::exportViewToCsv(resultView_, this, "screener_result.csv");
    });
    runRow->addWidget(exportBtn);
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
    resultView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultView_->setMinimumHeight(180);
    layout->addWidget(new QLabel(tr("选股结果（双击开图）")));
    layout->addWidget(resultView_, 1);

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
    return stockPicker_ ? stockPicker_->selectedSymbols() : std::vector<StockCode>{};
}

bool ScreenerPanel::compositeEnabled() const {
    return compositePatternCheck_ && compositeSentimentCheck_ &&
        (compositePatternCheck_->isChecked() || compositeSentimentCheck_->isChecked());
}

bool ScreenerPanel::anyFactorChecked() const {
    for (const auto* cb : factorChecks_) {
        if (cb && cb->isChecked()) return true;
    }
    return false;
}

void ScreenerPanel::updateRunButton() {
    if (runBtn_) runBtn_->setEnabled(!running_ && anyFactorChecked());
}

void ScreenerPanel::onRunClicked() {
    if (running_) return;
    if (!anyFactorChecked()) {
        LogManager::instance()->log(LogLevel::Warn, "选股: 请至少勾选一个因子");
        return;
    }
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

    // ① IO 池拉数据 → 缓存；勾选「舆情情绪」时同步拉取股票池前 30 只的东财新闻（限量防网络阻塞）
    const bool fetchSentiment = compositeSentimentCheck_->isChecked();
    constexpr int kSentimentLimit = 30;
    IDataProvider* provider = provider_;
    auto newsProvider = newsProvider_;
    const auto cache = cache_;
    // 基准指数：Alpha191 基准类因子（Alpha075/149/181/182 等）用沪深300日线
    const StockCode benchmarkCode(Market::SH, "000300");
    QPointer<ScreenerPanel> guard(this);
    ThreadPool::submitIO([provider, cache, newsProvider, guard, symbols, start, end,
                          fetchSentiment, benchmarkCode] {
        const int total = static_cast<int>(symbols.size());
        int done = 0;
        std::vector<std::optional<st::sentiment::SentimentScore>> sentiments;
        sentiments.reserve(symbols.size());
        st::sentiment::SentimentAnalyzer analyzer;
        for (size_t i = 0; i < symbols.size(); ++i) {
            const auto& code = symbols[i];
            auto bars = provider->getBars(code, BarPeriod::Daily, start, end);
            if (!bars.empty()) cache->cacheBars(code, BarPeriod::Daily, std::move(bars));
            // 情绪：仅池前 kSentimentLimit 只，其余 nullopt 降级缺失（综合评分按剩余分项折减）
            if (fetchSentiment && newsProvider && i < kSentimentLimit) {
                auto news = newsProvider->fetchNews(code, 10);
                sentiments.push_back(news.empty()
                    ? std::nullopt
                    : std::optional<st::sentiment::SentimentScore>(analyzer.averageScore(news)));
            } else {
                sentiments.push_back(std::nullopt);
            }
            ++done;
            QMetaObject::invokeMethod(guard, [guard, done, total] {
                guard->progress_->setValue(done * 50 / total);
            }, Qt::QueuedConnection);
        }
        // 基本面快照（估值/规模因子用）：批量一次拉取；失败时缺项 valid=false 降级
        auto quotes = provider->batchQuoteFundamentals(symbols);
        // 基准指数日线（取不到时基准类因子自动降级为缺失）
        auto benchmarkBars = provider->getBars(benchmarkCode, BarPeriod::Daily, start, end);
        QMetaObject::invokeMethod(guard, [guard, sentiments = std::move(sentiments),
                                          quotes = std::move(quotes),
                                          benchmarkBars = std::move(benchmarkBars)]() mutable {
            guard->onAllDataFetched(std::move(sentiments), std::move(quotes),
                                    std::move(benchmarkBars));
        }, Qt::QueuedConnection);
    });
}

void ScreenerPanel::onAllDataFetched(
    std::vector<std::optional<st::sentiment::SentimentScore>> sentiments,
    std::vector<st::QuoteFundamentals> quotes,
    std::vector<Bar> benchmarkBars) {
    progress_->setValue(50);
    auto symbols = selectedSymbols();
    if (symbols.empty()) {
        resetToIdle();
        return;
    }

    // 收集勾选的因子（结果表列头用中文显示名；引擎内部权重/结果按英文 id 对齐）
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> selected;
    std::vector<std::string> factorNames;
    for (size_t i = 0; i < factorChecks_.size(); ++i) {
        if (factorChecks_[i] && factorChecks_[i]->isChecked() && i < candidateFactors_.size()) {
            selected.push_back(candidateFactors_[i]);
            factorNames.push_back(candidateFactors_[i].first->displayName());
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

    // 基本面快照映射（fullCode → 快照；缺失项 valid=false 由因子内部降级）
    std::unordered_map<std::string, st::QuoteFundamentals> quoteMap;
    quoteMap.reserve(quotes.size());
    for (auto& q : quotes) {
        quoteMap[q.code.fullCode()] = std::move(q);
    }

    // ② Worker 池选股（安全异步：按值捕获 shared_ptr cache + QPointer 守卫，
    //    面板销毁后任务仍安全；不能用裸 this/裸 cache_ 指针——会 use-after-free）
    const bool usePattern = compositePatternCheck_->isChecked();
    const bool useSentiment = compositeSentimentCheck_->isChecked();
    const bool useComposite = usePattern || useSentiment;
    const auto cache = cache_;
    QPointer<ScreenerPanel> guard(this);
    ThreadPool::submitWorker(
        [guard, cache, selected = std::move(selected), factorNames, symbols, cfg,
         sentiments = std::move(sentiments), quoteMap = std::move(quoteMap),
         benchmarkBars = std::move(benchmarkBars),
         useComposite, usePattern, useSentiment]() mutable {
            StockScreener screener;
            screener.setConfig(cfg);
            screener.setDataCache(cache.get());
            screener.setQuoteFundamentals(std::move(quoteMap));
            screener.setBenchmarkBars(std::move(benchmarkBars));
            for (auto& [f, w] : selected) screener.addFactor(f, w);
            screener.setProgressCallback([guard](double p) {
                QMetaObject::invokeMethod(guard, [guard, p] {
                    if (!guard) return;
                    guard->progress_->setValue(50 + static_cast<int>(p / 2.0));
                }, Qt::QueuedConnection);
            });
            auto results = screener.run(symbols);

            // 综合评分（K线形态+舆情情绪+技术指标）：compositeScore 按 code 对齐到结果行
            std::vector<double> compositeScores;
            if (useComposite) {
                std::vector<std::vector<Bar>> barsByCode;
                barsByCode.reserve(symbols.size());
                for (const auto& code : symbols) {
                    barsByCode.push_back(cache->getBars(code, BarPeriod::Daily));
                }
                st::screener::CompositeScreenerConfig compositeCfg;
                compositeCfg.usePattern = usePattern;      // 「K线形态」分项勾选状态
                compositeCfg.useSentiment = useSentiment;  // 「舆情情绪」分项勾选状态（IO 阶段已按同条件限量拉取）
                auto composite = st::screener::runCompositeScreener(
                    symbols, barsByCode, sentiments, compositeCfg);
                std::unordered_map<std::string, double> scoreByCode;
                for (const auto& s : composite) {
                    scoreByCode[s.code.fullCode()] = s.compositeScore;
                }
                compositeScores.reserve(results.size());
                for (const auto& r : results) {
                    auto it = scoreByCode.find(r.code.fullCode());
                    compositeScores.push_back(it != scoreByCode.end() ? it->second : 0.0);
                }
                // 按综合评分降序重排（同分保持原总分顺序）
                std::vector<size_t> idx(results.size());
                std::iota(idx.begin(), idx.end(), 0u);
                std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
                    return compositeScores[a] > compositeScores[b];
                });
                std::vector<ScreenResult> sorted;
                std::vector<double> sortedScores;
                sorted.reserve(results.size());
                sortedScores.reserve(compositeScores.size());
                for (size_t k : idx) {
                    sorted.push_back(results[k]);
                    sortedScores.push_back(compositeScores[k]);
                }
                results = std::move(sorted);
                compositeScores = std::move(sortedScores);
            }

            QMetaObject::invokeMethod(guard,
                [guard, results = std::move(results), factorNames,
                 compositeScores = std::move(compositeScores)]() mutable {
                    if (!guard) return;
                    guard->onResult(results, factorNames, compositeScores);
                }, Qt::QueuedConnection);
        });
}

void ScreenerPanel::onResult(const std::vector<ScreenResult>& results,
                             const std::vector<std::string>& factorNames,
                             const std::vector<double>& compositeScores) {
    running_ = false;
    updateRunButton();
    progress_->setValue(100);
    progress_->setVisible(false);

    std::unordered_map<std::string, std::string> nameByCode;
    if (stockPicker_) {
        for (const auto& s : stockPicker_->allStocks()) {
            nameByCode[s.code.fullCode()] = s.name;
        }
    }

    resultModel_->setResults(results, factorNames, nameByCode, compositeScores);
    LogManager::instance()->log(LogLevel::Info, "选股完成: {} 只股票进入排名{}",
                                results.size(),
                                compositeScores.empty() ? "" : "（按综合评分排序）");
}

void ScreenerPanel::resetToIdle() {
    running_ = false;
    updateRunButton();
    progress_->setVisible(false);
}

} // namespace st

#include "moc_screener_panel.cpp"
