#include "ui/panels/funds_window.h"
#include "data/eastmoney_funds_provider.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>
#include <string>

namespace st {

namespace {
/// 元 → 亿（龙虎榜/两融余额）；万元 → 亿（北向）
QString yi(double v, double divisor = 1e8) {
    return QString::number(v / divisor, 'f', 2);
}
const char* kUpColor = "#ef5350";
const char* kDownColor = "#26a69a";
const char* kFlatColor = "#9e9e9e";
}  // namespace

// ============================================================
// 龙虎榜面板
// ============================================================
FundsDragonTigerPanel::FundsDragonTigerPanel(
    std::shared_ptr<EastMoneyFundsProvider> funds, IDataProvider* provider, QWidget* parent)
    : QWidget(parent), funds_(std::move(funds)), provider_(provider) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(tr("交易日："), this));
    dateCombo_ = new QComboBox(this);
    dateCombo_->setMinimumWidth(120);
    dateCombo_->setToolTip(tr("仅显示有数据的交易日（来自上证指数日线日历，周末/节假日不可选）"));
    topRow->addWidget(dateCombo_);
    auto* refreshBtn = new QPushButton(tr("刷新"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &FundsDragonTigerPanel::refresh);
    topRow->addWidget(refreshBtn);
    topRow->addStretch();
    layout->addLayout(topRow);

    table_ = new QTableWidget(0, 8, this);
    table_->setHorizontalHeaderLabels(
        {tr("代码"), tr("名称"), tr("现价"), tr("涨跌幅%"), tr("净买额亿"),
         tr("买入亿"), tr("卖出亿"), tr("上榜原因")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);

    connect(dateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FundsDragonTigerPanel::refresh);
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (row < 0 || row >= table_->rowCount()) return;
                const auto code = StockCode(table_->item(row, 0)->text().toStdString());
                const QString name = table_->item(row, 1)->text();
                emit openChart(code, name);
            });

    populateTradingDates();  // 异步填交易日下拉（完成后触发首刷）
}

void FundsDragonTigerPanel::populateTradingDates() {
    if (!provider_) return;
    const int gen = ++dateGen_;
    IDataProvider* provider = provider_;
    QPointer<FundsDragonTigerPanel> guard(this);
    ThreadPool::submitIO([provider, guard, gen] {
        // 用上证指数日线提取真实交易日（TDX 已接入，含节假日休市）——近 200 天
        auto bars = provider->getBars(StockCode("SH000001"), BarPeriod::Daily,
                                      utils::today() - std::chrono::hours(200 * 24),
                                      utils::today());
        QMetaObject::invokeMethod(guard, [guard, gen, bars = std::move(bars)]() mutable {
            if (!guard || gen != guard->dateGen_) return;
            guard->dateCombo_->clear();
            for (auto it = bars.rbegin(); it != bars.rend(); ++it) {  // 最新在前
                guard->dateCombo_->addItem(
                    QString::fromStdString(utils::toDateString(it->time)));
            }
            if (guard->dateCombo_->count() == 0) {
                guard->dateCombo_->addItem(QStringLiteral("--"));
            }
            guard->refresh();
        }, Qt::QueuedConnection);
    });
}

void FundsDragonTigerPanel::refresh() {
    if (!funds_ || !dateCombo_ || dateCombo_->count() == 0) return;
    const std::string date = dateCombo_->currentText().toStdString();
    const int gen = ++loadGen_;
    auto funds = funds_;
    QPointer<FundsDragonTigerPanel> guard(this);
    ThreadPool::submitIO([funds, guard, gen, date] {
        auto rows = funds->fetchDragonTiger(date);
        QMetaObject::invokeMethod(guard, [guard, gen, date, rows = std::move(rows)]() mutable {
            if (!guard || gen != guard->loadGen_) return;
            guard->table_->setRowCount(static_cast<int>(rows.size()));
            for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
                const auto& x = rows[r];
                const auto item = [&](int col, const QString& text) {
                    auto* it = new QTableWidgetItem(text);
                    if (col == 3 || col == 4) {
                        const double v = (col == 3) ? x.changeRate : x.netAmt;
                        it->setForeground(v >= 0 ? QColor(kUpColor) : QColor(kDownColor));
                    }
                    guard->table_->setItem(r, col, it);
                };
                item(0, QString::fromStdString(x.code));
                item(1, QString::fromStdString(x.name));
                item(2, QString::number(x.closePrice, 'f', 2));
                item(3, QString::number(x.changeRate, 'f', 2));
                item(4, yi(x.netAmt));
                item(5, yi(x.buyAmt));
                item(6, yi(x.sellAmt));
                item(7, QString::fromStdString(x.reason));
            }
            LogManager::instance()->log(LogLevel::Info,
                "龙虎榜 {}: {} 条", date, rows.size());
        }, Qt::QueuedConnection);
    });
}

// ============================================================
// 融资融券面板
// ============================================================
FundsMarginPanel::FundsMarginPanel(
    std::shared_ptr<EastMoneyFundsProvider> funds, QWidget* parent)
    : QWidget(parent), funds_(std::move(funds)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    stockLabel_ = new QLabel(tr("选择股票以查看两融明细"), this);
    layout->addWidget(stockLabel_);
    overview_ = new QLabel(this);
    layout->addWidget(overview_);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels(
        {tr("日期"), tr("融资余额亿"), tr("融券余额亿"), tr("两融余额亿"), tr("融资买入亿")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
}

void FundsMarginPanel::setStock(const StockCode& code, const QString& name) {
    code_ = code;
    name_ = name;
    refresh();
}

void FundsMarginPanel::refresh() {
    if (!funds_) return;
    const int gen = ++loadGen_;
    auto funds = funds_;
    const StockCode code = code_;
    QPointer<FundsMarginPanel> guard(this);
    ThreadPool::submitIO([funds, guard, gen, code] {
        auto market = funds->fetchMarginMarket();
        auto stock = code.isValid() ? funds->fetchMargin(code.code()) : std::vector<MarginRecord>{};
        QMetaObject::invokeMethod(guard, [guard, gen, market = std::move(market),
                                          stock = std::move(stock)]() mutable {
            if (!guard || gen != guard->loadGen_) return;
            if (!market.empty()) {
                const auto& m = market.front();
                guard->overview_->setText(tr("沪深两市(%1) 融资 %2 亿 / 两融 %3 亿")
                    .arg(utils::toDateString(m.date).c_str(), yi(m.financeBalance), yi(m.marginBalance)));
            }
            guard->stockLabel_->setText(guard->code_.isValid()
                ? tr("%1 (%2) 两融明细").arg(guard->name_, QString::fromStdString(guard->code_.displayCode()))
                : tr("选择股票以查看两融明细"));
            guard->table_->setRowCount(static_cast<int>(stock.size()));
            for (int r = 0; r < static_cast<int>(stock.size()); ++r) {
                const auto& x = stock[r];
                guard->table_->setItem(r, 0, new QTableWidgetItem(utils::toDateString(x.date).c_str()));
                guard->table_->setItem(r, 1, new QTableWidgetItem(yi(x.financeBalance)));
                guard->table_->setItem(r, 2, new QTableWidgetItem(yi(x.shortBalance)));
                guard->table_->setItem(r, 3, new QTableWidgetItem(yi(x.marginBalance)));
                guard->table_->setItem(r, 4, new QTableWidgetItem(yi(x.financeBuy)));
            }
        }, Qt::QueuedConnection);
    });
}

// ============================================================
// 资金数据窗口
// ============================================================
FundsWindow::FundsWindow(IDataProvider* provider, QWidget* parent)
    : QMainWindow(parent), provider_(provider) {
    setWindowTitle(tr("资金数据"));
    resize(900, 620);

    funds_ = std::make_shared<EastMoneyFundsProvider>();
    tabs_ = new QTabWidget(this);
    setCentralWidget(tabs_);

    dragonTiger_ = new FundsDragonTigerPanel(funds_, provider_, tabs_);
    margin_ = new FundsMarginPanel(funds_, tabs_);
    tabs_->addTab(dragonTiger_, tr("龙虎榜"));
    tabs_->addTab(margin_, tr("融资融券"));

    connect(dragonTiger_, &FundsDragonTigerPanel::openChart,
            this, &FundsWindow::openChart);
}

void FundsWindow::setCurrentStock(const StockCode& code, const QString& name) {
    if (margin_) margin_->setStock(code, name);
}

} // namespace st

#include "moc_funds_window.cpp"
