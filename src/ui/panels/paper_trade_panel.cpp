#include "ui/panels/paper_trade_panel.h"
#include "ui/strategy_catalog.h"
#include "ui/models/trade_table_model.h"
#include "data/idata_provider.h"
#include "data/curated_stocks.h"
#include "engine/paper_trade/paper_trade_engine.h"
#include "engine/journal/trade_journal.h"
#include "engine/optimizer/grid_search.h"
#include "engine/strategy/istrategy.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QTableView>
#include <QPlainTextEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QPointer>

namespace st {

namespace {
constexpr int kRefreshMs = 3000;
}  // namespace

PaperTradePanel::PaperTradePanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ---- 配置 ----
    auto* form = new QGroupBox(tr("模拟交易配置"));
    auto* fl = new QFormLayout(form);

    stockCombo_ = new QComboBox;
    auto addPool = [this](Market m, const std::vector<CuratedStock>& table) {
        for (const auto& c : table) {
            stockCombo_->addItem(QStringLiteral("%1  %2")
                .arg(QString::fromUtf8(c.name), QString::fromUtf8(c.code)),
                QString::fromStdString(StockCode(m, c.code).fullCode()));
        }
    };
    addPool(Market::SH, kCuratedSH);
    addPool(Market::SZ, kCuratedSZ);
    fl->addRow(tr("股票"), stockCombo_);

    strategyCombo_ = new QComboBox;
    for (const auto& s : strategy_catalog::all()) {
        strategyCombo_->addItem(QStringLiteral("[%1] %2").arg(s.category, s.display),
                                s.id);
    }
    fl->addRow(tr("策略"), strategyCombo_);

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
    connect(strategyCombo_, &QComboBox::currentIndexChanged,
            this, &PaperTradePanel::onStrategyChanged);

    capital_ = new QDoubleSpinBox;
    capital_->setRange(1000, 1e9);
    capital_->setValue(100000.0);
    capital_->setDecimals(0);
    capital_->setSingleStep(10000);
    fl->addRow(tr("初始资金"), capital_);

    slippage_ = new QDoubleSpinBox;
    slippage_->setRange(0.0, 0.01);
    slippage_->setValue(0.001);
    slippage_->setDecimals(4);
    slippage_->setSingleStep(0.0005);
    fl->addRow(tr("滑点"), slippage_);

    toggleBtn_ = new QPushButton(tr("启动模拟交易"));
    fl->addRow(toggleBtn_);
    layout->addWidget(form);
    connect(toggleBtn_, &QPushButton::clicked, this, &PaperTradePanel::onToggleClicked);

    // ---- 状态 ----
    auto* statusBox = new QGroupBox(tr("账户状态"));
    auto* sg = new QGridLayout(statusBox);
    const auto addStatus = [&](int col, const QString& name, QLabel*& out) {
        sg->addWidget(new QLabel(name), 0, col * 2);
        out = new QLabel("--");
        out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sg->addWidget(out, 0, col * 2 + 1);
    };
    addStatus(0, tr("现金"), cash_);
    addStatus(1, tr("市值"), marketValue_);
    addStatus(2, tr("总资产"), totalAsset_);
    addStatus(3, tr("总盈亏"), todayPnl_);
    addStatus(4, tr("持仓数"), posCount_);
    layout->addWidget(statusBox);

    // ---- 成交表 ----
    tradeModel_ = new TradeTableModel(this);
    tradesView_ = new QTableView;
    tradesView_->setModel(tradeModel_);
    tradesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tradesView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tradesView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tradesView_->setMinimumHeight(120);
    layout->addWidget(new QLabel(tr("成交记录")));
    layout->addWidget(tradesView_);

    // ---- 日志 ----
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumHeight(90);
    layout->addWidget(new QLabel(tr("模拟交易日志")));
    layout->addWidget(log_);

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &PaperTradePanel::onTimerTick);

    onStrategyChanged();
}

PaperTradePanel::~PaperTradePanel() {
    // 停止轮询定时器：防止关闭窗口的间隙里定时器触发 → 提交 provider 异步任务
    if (timer_) timer_->stop();
    if (engine_) engine_->stop();
}

void PaperTradePanel::setJournal(std::shared_ptr<TradeJournalEngine> journal) {
    journal_ = std::move(journal);
}

StockCode PaperTradePanel::selectedCode() const {
    const QString full = stockCombo_->currentData().toString();
    return full.isEmpty() ? StockCode{} : StockCode(full.toStdString());
}

std::shared_ptr<IStrategy> PaperTradePanel::makeStrategy() const {
    // 统一走 GridSearchOptimizer::makeStrategy（共享策略注册表）
    const auto* spec = strategy_catalog::byId(strategyCombo_->currentData().toString());
    if (!spec) return std::make_shared<MACrossStrategy>();
    const auto params = strategy_catalog::makeParams(*spec, p1_->value(), p2_->value());
    std::vector<std::pair<std::string, int>> pairs;
    pairs.emplace_back(spec->p1Key.toStdString(), params[spec->p1Key].toInt());
    pairs.emplace_back(spec->p2Key.toStdString(), params[spec->p2Key].toInt());
    auto s = GridSearchOptimizer::makeStrategy(spec->id.toStdString(), pairs);
    return s ? s : std::make_shared<MACrossStrategy>();
}

void PaperTradePanel::onStrategyChanged() {
    const auto* s = strategy_catalog::byId(strategyCombo_->currentData().toString());
    if (!s) return;
    p1Label_->setText(s->p1Name);
    p2Label_->setText(s->p2Name);
    p1_->setRange(s->p1Min, s->p1Max);
    p2_->setRange(s->p2Min, s->p2Max);
    p1_->setValue(s->p1);
    p2_->setValue(s->p2);
}

void PaperTradePanel::onToggleClicked() {
    if (!running_) {
        const auto code = selectedCode();
        if (!code.isValid()) {
            LogManager::instance()->log(LogLevel::Warn, "模拟交易: 请选择股票");
            return;
        }
        // 启动：IO 拉历史播种 → 主线程建引擎启动
        running_ = true;
        toggleBtn_->setEnabled(false);
        toggleBtn_->setText(tr("正在启动…"));
        stockCombo_->setEnabled(false);
        strategyCombo_->setEnabled(false);
        p1_->setEnabled(false);
        p2_->setEnabled(false);
        capital_->setEnabled(false);
        slippage_->setEnabled(false);
        tradeModel_->clear();
        log_->clear();
        lastTradeCount_ = 0;
        refreshStatus();

        const auto now = utils::now();
        const auto start = utils::addTradingDays(now, -120);
        const double capital = capital_->value();
        const double slippage = slippage_->value();
        // 安全异步：捕获 provider 按值 + QPointer 守卫
        IDataProvider* provider = provider_;
        QPointer<PaperTradePanel> guard(this);
        ThreadPool::submitIO([provider, guard, code, start, now, capital, slippage] {
            auto seed = provider->getBars(code, BarPeriod::Daily, start, now);
            QMetaObject::invokeMethod(guard, [guard, code, seed = std::move(seed),
                                             capital, slippage]() mutable {
                // 重建引擎（清状态）
                guard->engine_ = std::make_unique<PaperTradeEngine>();
                // 模拟成交自动落库（安全异步：回调可能在 IO 线程，转主线程落库）
                const QString strategyName = guard->strategyCombo_->currentText();
                if (guard->journal_) {
                    auto j = guard->journal_;
                    QPointer<PaperTradePanel> pGuard(guard);
                    guard->engine_->setOnTrade([j, pGuard, strategyName](const Trade& t) {
                        // 回调在 IO 线程执行 → invokeMethod 转主线程（appendAuto 内部有锁线程安全）
                        QMetaObject::invokeMethod(pGuard, [j, t, strategyName] {
                            j->appendAuto(t, strategyName.toStdString());
                        }, Qt::QueuedConnection);
                    });
                }
                PaperTradeConfig cfg;
                cfg.initialCapital = capital;
                cfg.slippage = slippage;
                cfg.feeConfig = FeeConfig::defaultAShare();
                guard->engine_->setConfig(cfg);
                guard->engine_->addStrategy(guard->makeStrategy());
                guard->engine_->seedHistory(code, seed);
                if (seed.empty()) {
                    LogManager::instance()->log(LogLevel::Warn,
                        "模拟交易: 历史播种为空（无网络？），策略需积累报价后才交易");
                }
                guard->engine_->start();
                guard->toggleBtn_->setText(tr("停止模拟交易"));
                guard->toggleBtn_->setEnabled(true);
                guard->timer_->start();
            }, Qt::QueuedConnection);
        });
    } else {
        // 停止
        timer_->stop();
        if (engine_) engine_->stop();
        running_ = false;
        toggleBtn_->setText(tr("启动模拟交易"));
        stockCombo_->setEnabled(true);
        strategyCombo_->setEnabled(true);
        p1_->setEnabled(true);
        p2_->setEnabled(true);
        capital_->setEnabled(true);
        slippage_->setEnabled(true);
    }
}

void PaperTradePanel::onTimerTick() {
    if (refreshing_ || !engine_ || !running_) return;
    const auto code = selectedCode();
    if (!code.isValid()) return;
    refreshing_ = true;
    const int gen = ++gen_;
    // 安全异步：捕获 provider 按值 + QPointer 守卫
    IDataProvider* provider = provider_;
    QPointer<PaperTradePanel> guard(this);
    ThreadPool::submitIO([provider, guard, gen, code] {
        auto quotes = provider->batchQuote({code});
        QMetaObject::invokeMethod(guard, [guard, gen, quotes = std::move(quotes)]() mutable {
            guard->refreshing_ = false;
            if (gen != guard->gen_) return;
            if (quotes.empty()) return;
            const auto& q = quotes.front();
            if (q.lastPrice > 0) {
                guard->engine_->onQuote(q.code, q.lastPrice, q.time);
                guard->refreshStatus();
                if (guard->engine_->trades().size() > guard->lastTradeCount_) {
                    const auto& t = guard->engine_->trades().back();
                    guard->log_->appendPlainText(QStringLiteral("[%1] %2 %3 x%4 @ %5")
                        .arg(QString::fromStdString(utils::toDateTimeString(t.time)))
                        .arg(t.direction == Direction::Buy ? tr("买入") : tr("卖出"))
                        .arg(QString::fromStdString(t.code.displayCode()))
                        .arg(t.volume)
                        .arg(t.price, 0, 'f', 2));
                    guard->lastTradeCount_ = guard->engine_->trades().size();
                }
            }
        }, Qt::QueuedConnection);
    });
}

void PaperTradePanel::refreshStatus() {
    if (!engine_) return;
    const auto& pf = engine_->portfolio();
    cash_->setText(QString::number(pf.cash, 'f', 2));
    marketValue_->setText(QString::number(pf.marketValue, 'f', 2));
    totalAsset_->setText(QString::number(pf.totalAsset, 'f', 2));
    const double pnl = engine_->todayPnl() != 0.0 ? engine_->todayPnl()
                                                  : pf.totalPnl;
    todayPnl_->setText(QStringLiteral("%1 (%2%)")
        .arg(pnl, 0, 'f', 2).arg(pf.totalPnlPct, 0, 'f', 2));
    todayPnl_->setStyleSheet(QStringLiteral("color:%1;")
        .arg(pnl >= 0 ? "#e54648" : "#2e9e5b"));
    posCount_->setText(QString::number(pf.positions.size()));
    tradeModel_->setTrades(engine_->trades());
}

} // namespace st

#include "moc_paper_trade_panel.cpp"
