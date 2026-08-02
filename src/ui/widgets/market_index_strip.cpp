#include "ui/widgets/market_index_strip.h"
#include "data/tencent_provider.h"
#include "core/event_bus.h"
#include <QHBoxLayout>
#include <QToolButton>
#include <QVariantMap>

namespace st {

namespace {
// A股习惯: 红涨绿跌
const char* kUpColor   = "#e53935";
const char* kDownColor = "#43A047";
const char* kFlatColor = "#9e9e9e";

struct IndexDef {
    Market market;
    const char* code;
    const char* name;
};

const IndexDef kIndices[] = {
    {Market::SH, "000001", "上证指数"},
    {Market::SZ, "399001", "深证成指"},
    {Market::SZ, "399006", "创业板指"},
    {Market::SH, "000688", "科创50"},
};
}  // namespace

MarketIndexStrip::MarketIndexStrip(TencentProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(8);

    for (const auto& def : kIndices) {
        IndexItem item;
        item.code = StockCode(def.market, def.code);
        item.name = QString::fromUtf8(def.name);

        item.button = new QToolButton(this);
        item.button->setText(item.name);
        item.button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        item.button->setAutoRaise(true);
        item.button->setCursor(Qt::PointingHandCursor);
        item.button->setStyleSheet(QStringLiteral("color:%1; background:transparent;")
                                       .arg(kFlatColor));
        item.button->setFocusPolicy(Qt::NoFocus);

        // 点击 → 发出 indexClicked
        connect(item.button, &QToolButton::clicked, this,
                [this, code = item.code] { emit indexClicked(code); });

        layout->addWidget(item.button);
        items_.push_back(std::move(item));
    }

    // 订阅 4 大指数实时行情
    if (provider_) {
        for (const auto& item : items_) {
            provider_->subscribeQuote(item.code);
        }
    }

    // 监听 EventBus::QuoteReceived（主线程直接连接，天然安全）
    connect(EventBus::instance(), &EventBus::eventFired,
            this, &MarketIndexStrip::onQuoteEvent);
}

void MarketIndexStrip::onQuoteEvent(const QString& event, const QVariantMap& data) {
    if (event != events::QuoteReceived) return;

    const QString fullCode = data.value(QStringLiteral("code")).toString();
    const double lastPrice = data.value(QStringLiteral("lastPrice")).toDouble();
    const double change    = data.value(QStringLiteral("change")).toDouble();

    for (auto& item : items_) {
        if (QString::fromStdString(item.code.fullCode()) != fullCode) continue;

        const QString text = QStringLiteral("%1  %2  %3%")
            .arg(item.name,
                 QString::number(lastPrice, 'f', 2),
                 QString::number(change, 'f', 2));
        item.button->setText(text);

        const char* color = change > 0 ? kUpColor
                           : (change < 0 ? kDownColor : kFlatColor);
        item.button->setStyleSheet(QStringLiteral("color:%1; background:transparent;")
                                       .arg(color));
        item.button->setToolTip(QStringLiteral("%1\n现价 %2\n涨跌幅 %3%")
                                    .arg(item.name,
                                         QString::number(lastPrice, 'f', 2),
                                         QString::number(change, 'f', 2)));
        break;
    }
}

} // namespace st

#include "moc_market_index_strip.cpp"
