#include "ui/widgets/market_depth_widget.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include <QLabel>
#include <QTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QMetaObject>
#include <QShowEvent>
#include <QHideEvent>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QColor>
#include <QPointer>
#include <utility>

namespace st {

namespace {
constexpr char kUpColor[] = "#e54648";    // 红涨
constexpr char kDownColor[] = "#2e9e5b";  // 绿跌

QString volText(Volume v) {
    // 股 → 手，自动缩放
    const double hands = static_cast<double>(v) / 100.0;
    if (hands >= 1e4) {
        return QStringLiteral("%1万手").arg(hands / 1e4, 0, 'f', 2);
    }
    return QStringLiteral("%1手").arg(hands, 0, 'f', 0);
}

/// 标题：名称 + 代码（无名称时仅代码）
QString titleFor(const QString& name, const StockCode& code) {
    const QString c = QString::fromStdString(code.code());
    if (name.isEmpty()) return c;
    return name + QStringLiteral(" ") + c;
}
}  // namespace

MarketDepthWidget::MarketDepthWidget(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);
    auto* grid = new QGridLayout;
    grid->setSpacing(2);

    title_ = new QLabel(tr("盘口 —"));
    title_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(title_, 0, 0, 1, 3);

    // 最新价 / 涨跌（左上角）
    auto* priceRow = new QHBoxLayout;
    priceLabel_ = new QLabel(tr("--"));
    auto priceFont = priceLabel_->font();
    priceFont.setBold(true);
    priceFont.setPointSize(priceFont.pointSize() + 2);
    priceLabel_->setFont(priceFont);
    changeLabel_ = new QLabel(tr("--"));
    priceRow->addWidget(priceLabel_);
    priceRow->addWidget(changeLabel_);
    priceRow->addStretch();
    grid->addLayout(priceRow, 1, 0, 1, 3);
    int row = 2;

    auto makeSep = [&]() {
        auto* sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        grid->addWidget(sep, row, 0, 1, 3);
        ++row;
    };
    makeSep();  // 价格区下方分隔线

    // 卖5..卖1（价高在上）
    const char* askTags[5] = {"卖5", "卖4", "卖3", "卖2", "卖1"};
    for (int k = 0; k < 5; ++k) {
        auto* tag = new QLabel(askTags[k]);
        tag->setAlignment(Qt::AlignCenter);
        askPrice_[k] = new QLabel(tr("--"));
        askPrice_[k]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        askPrice_[k]->setStyleSheet(QStringLiteral("color:%1;").arg(kDownColor));  // 卖价绿
        askVol_[k] = new QLabel(tr("--"));
        askVol_[k]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(tag, row, 0);
        grid->addWidget(askPrice_[k], row, 1);
        grid->addWidget(askVol_[k], row, 2);
        ++row;
    }

    makeSep();  // 买卖之间分隔线

    // 买1..买5
    const char* bidTags[5] = {"买1", "买2", "买3", "买4", "买5"};
    for (int k = 0; k < 5; ++k) {
        auto* tag = new QLabel(bidTags[k]);
        tag->setAlignment(Qt::AlignCenter);
        bidPrice_[k] = new QLabel(tr("--"));
        bidPrice_[k]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bidPrice_[k]->setStyleSheet(QStringLiteral("color:%1;").arg(kUpColor));
        bidVol_[k] = new QLabel(tr("--"));
        bidVol_[k]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(tag, row, 0);
        grid->addWidget(bidPrice_[k], row, 1);
        grid->addWidget(bidVol_[k], row, 2);
        ++row;
    }

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    root->addLayout(grid);

    // 成交明细（盘口下方）
    auto* txSep = new QFrame(this);
    txSep->setFrameShape(QFrame::HLine);
    txSep->setFrameShadow(QFrame::Sunken);
    root->addWidget(txSep);
    auto* txLabel = new QLabel(tr("成交明细"));
    txLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    root->addWidget(txLabel);

    txModel_ = new QStandardItemModel(0, 4, this);
    txModel_->setHorizontalHeaderLabels({tr("时间"), tr("价格"), tr("量"), tr("方向")});
    txView_ = new QTableView;
    txView_->setModel(txModel_);
    txView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    txView_->setSelectionMode(QAbstractItemView::NoSelection);
    txView_->setShowGrid(false);
    txView_->verticalHeader()->setVisible(false);
    txView_->horizontalHeader()->setStretchLastSection(true);
    txView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    root->addWidget(txView_, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(2500);
    connect(timer_, &QTimer::timeout, this, &MarketDepthWidget::onPoll);
}

void MarketDepthWidget::setPollIntervalMs(int ms) {
    timer_->setInterval(ms);
}

void MarketDepthWidget::setStock(const StockCode& code, const QString& name) {
    code_ = code;
    name_ = name;
    resetLabels();
    onPoll();
}

void MarketDepthWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (timer_ && !timer_->isActive()) {
        timer_->start();
        if (!code_.code().empty()) onPoll();
    }
}

void MarketDepthWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (timer_) timer_->stop();
}

void MarketDepthWidget::onPoll() {
    if (polling_ || code_.code().empty()) return;
    polling_ = true;
    const StockCode code = code_;
    // 安全异步：IO 线程用按值捕获的 provider，避免 widget 销毁后读悬垂 this->provider_；
    // 结果用 QPointer 守卫投递回主线程（widget 销毁后 guard 变 null，invokeMethod 安全跳过）
    IDataProvider* provider = provider_;
    QPointer<MarketDepthWidget> guard(this);
    ThreadPool::submitIO([provider, guard, code] {
        auto md = provider->getMarketDepth(code);
        auto quotes = provider->batchQuote({code});
        auto ticks = provider->getTransactions(code, 30);
        QMetaObject::invokeMethod(guard,
            [guard, md = std::move(md), quotes = std::move(quotes),
             ticks = std::move(ticks)]() mutable {
                guard->polling_ = false;
                if (md.has_value()) guard->applyDepth(*md);
                if (!quotes.empty()) guard->applyQuote(quotes.front());
                guard->applyTransactions(ticks);
            }, Qt::QueuedConnection);
    });
}

void MarketDepthWidget::applyDepth(const MarketDepth& md) {
    for (int k = 0; k < 5; ++k) {
        // 卖5 最上：asks[4]=卖5（价最高）… asks[0]=卖1
        const auto& ask = md.asks[4 - k];
        if (ask.price > 0.0) {
            askPrice_[k]->setText(QString::number(ask.price, 'f', 2));
            askVol_[k]->setText(volText(ask.volume));
        }
        // 买1 在上：bids[0]=买1（价最高）… bids[4]=买5
        const auto& bid = md.bids[k];
        if (bid.price > 0.0) {
            bidPrice_[k]->setText(QString::number(bid.price, 'f', 2));
            bidVol_[k]->setText(volText(bid.volume));
        }
    }
    title_->setText(tr("盘口 — %1").arg(titleFor(name_, code_)));
}

void MarketDepthWidget::applyQuote(const Quote& q) {
    priceLabel_->setText(QString::number(q.lastPrice, 'f', 2));
    changeLabel_->setText(QStringLiteral("%1%").arg(q.change, 0, 'f', 2));
    // 价格/涨跌：涨红、跌绿、平盘保持主题默认（黑色）
    QString color;
    if (q.change > 0.0) color = kUpColor;
    else if (q.change < 0.0) color = kDownColor;
    const QString qss = color.isEmpty() ? QString()
        : QStringLiteral("color:%1;").arg(color);
    priceLabel_->setStyleSheet(qss);
    changeLabel_->setStyleSheet(qss);
}

void MarketDepthWidget::applyTransactions(const std::vector<Tick>& ticks) {
    if (!txModel_) return;
    txModel_->removeRows(0, txModel_->rowCount());
    // ticks 最新在前 → 逆序填充，使旧记录在上、最新记录在底部
    for (auto it = ticks.rbegin(); it != ticks.rend(); ++it) {
        const auto& t = *it;
        const int row = txModel_->rowCount();
        txModel_->insertRow(row);
        txModel_->setItem(row, 0, new QStandardItem(
            QString::fromStdString(utils::toDateTimeString(t.time)).mid(11, 5)));
        txModel_->setItem(row, 1, new QStandardItem(QString::number(t.price, 'f', 2)));
        auto* vol = new QStandardItem(
            QStringLiteral("%1").arg(static_cast<double>(t.volume) / 100.0, 0, 'f', 0));
        vol->setForeground(t.direction == Direction::Sell ? QColor(kDownColor)
                                                          : QColor(kUpColor));
        txModel_->setItem(row, 2, vol);
        txModel_->setItem(row, 3, new QStandardItem(
            t.direction == Direction::Sell ? tr("卖") : tr("买")));
    }
    txView_->scrollToBottom();  // 最新记录在底部，滚动到可见
}

void MarketDepthWidget::resetLabels() {
    for (int k = 0; k < 5; ++k) {
        askPrice_[k]->setText(tr("--"));
        askVol_[k]->setText(tr("--"));
        bidPrice_[k]->setText(tr("--"));
        bidVol_[k]->setText(tr("--"));
    }
    priceLabel_->setText(tr("--"));
    changeLabel_->setText(tr("--"));
    changeLabel_->setStyleSheet(QString());
    if (code_.code().empty()) {
        title_->setText(tr("盘口 —"));
    } else {
        title_->setText(tr("盘口 — %1").arg(titleFor(name_, code_)));
    }
}

} // namespace st

#include "moc_market_depth_widget.cpp"
