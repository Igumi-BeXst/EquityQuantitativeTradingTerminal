#include "ui/panels/paper_trade_panel.h"
#include "ui/models/trade_table_model.h"
#include "data/tencent_provider.h"
#include "data/curated_stocks.h"
#include "engine/paper_trade/paper_trade_engine.h"
#include "engine/strategy/istrategy.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "engine/strategy/templates/turtle_strategy.h"
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

namespace st {

namespace {
constexpr int kRefreshMs = 3000;
}  // namespace

PaperTradePanel::PaperTradePanel(TencentProvider* provider, QWidget* parent)
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
    strategyCombo_->addItem(tr("双均线 MACross"), QStringLiteral("MACross"));
    strategyCombo_->addItem(tr("海龟 Turtle"), QStringLiteral("Turtle"));
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
    tradesView_->horizontalHeader()->setStretchLastSection(true);
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

StockCode PaperTradePanel::selectedCode() const {
    const QString full = stockCombo_->currentData().toString();
    return full.isEmpty() ? StockCode{} : StockCode(full.toStdString());
}

std::shared_ptr<IStrategy> PaperTradePanel::makeStrategy() const {
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

void PaperTradePanel::onStrategyChanged() {
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
        ThreadPool::submitIO([this, code, start, now, capital, slippage] {
            auto seed = provider_->getBars(code, BarPeriod::Daily, start, now);
            QMetaObject::invokeMethod(this, [this, code, seed = std::move(seed),
                                             capital, slippage]() mutable {
                // 重建引擎（清状态）
                engine_ = std::make_unique<PaperTradeEngine>();
                PaperTradeConfig cfg;
                cfg.initialCapital = capital;
                cfg.slippage = slippage;
                cfg.feeConfig = FeeConfig::defaultAShare();
                engine_->setConfig(cfg);
                engine_->addStrategy(makeStrategy());
                engine_->seedHistory(code, seed);
                if (seed.empty()) {
                    LogManager::instance()->log(LogLevel::Warn,
                        "模拟交易: 历史播种为空（无网络？），策略需积累报价后才交易");
                }
                engine_->start();
                toggleBtn_->setText(tr("停止模拟交易"));
                toggleBtn_->setEnabled(true);
                timer_->start();
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
    ThreadPool::submitIO([this, gen, code] {
        auto quotes = provider_->batchQuote({code});
        QMetaObject::invokeMethod(this, [this, gen, quotes = std::move(quotes)]() mutable {
            refreshing_ = false;
            if (gen != gen_) return;
            if (quotes.empty()) return;
            const auto& q = quotes.front();
            if (q.lastPrice > 0) {
                engine_->onQuote(q.code, q.lastPrice, q.time);
                refreshStatus();
                if (engine_->trades().size() > lastTradeCount_) {
                    const auto& t = engine_->trades().back();
                    log_->appendPlainText(QStringLiteral("[%1] %2 %3 x%4 @ %5")
                        .arg(QString::fromStdString(utils::toDateTimeString(t.time)))
                        .arg(t.direction == Direction::Buy ? tr("买入") : tr("卖出"))
                        .arg(QString::fromStdString(t.code.displayCode()))
                        .arg(t.volume)
                        .arg(t.price, 0, 'f', 2));
                    lastTradeCount_ = engine_->trades().size();
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
