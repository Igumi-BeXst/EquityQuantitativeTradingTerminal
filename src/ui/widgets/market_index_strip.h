#pragma once

#include "foundation/stock_code.h"
#include <QWidget>
#include <QString>
#include <vector>

class QToolButton;

namespace st {

class TencentProvider;

/// 顶部指数条 — 上证/深证/创业板/科创50 实时行情，红涨绿跌，可点击
///
/// 通过 IDataProvider::subscribeQuote 订阅 4 大指数，
/// 监听 EventBus::QuoteReceived 更新数值，颜色按涨跌（A股红涨绿跌）。
class MarketIndexStrip : public QWidget {
    Q_OBJECT

public:
    explicit MarketIndexStrip(TencentProvider* provider, QWidget* parent = nullptr);

signals:
    /// 点击某个指数（P6 跳转 K线图）
    void indexClicked(const StockCode& code);

private slots:
    void onQuoteEvent(const QString& event, const QVariantMap& data);

private:
    struct IndexItem {
        StockCode code;
        QString name;
        QToolButton* button = nullptr;
    };

    TencentProvider* provider_ = nullptr;
    std::vector<IndexItem> items_;
};

} // namespace st
