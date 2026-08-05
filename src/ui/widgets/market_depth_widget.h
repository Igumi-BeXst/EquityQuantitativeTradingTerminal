#pragma once

#include "foundation/stock_code.h"
#include "foundation/tick.h"
#include <QString>
#include <QWidget>

class QLabel;
class QTimer;
class QTableView;
class QStandardItemModel;

namespace st {

class IDataProvider;

/// 盘口五档面板 — TDX 五档买卖盘 + 最新价/涨跌，定时轮询刷新
///
/// 刷新走 IO 线程池（getMarketDepth + batchQuote），主线程 QTimer tick，
/// polling_ 防重叠；showEvent 启动 / hideEvent 停止（dock 收起即省网）。
class MarketDepthWidget : public QWidget {
    Q_OBJECT

public:
    explicit MarketDepthWidget(IDataProvider* provider, QWidget* parent = nullptr);

    /// 切换到某只股票（立即刷新一次）
    void setStock(const StockCode& code, const QString& name);
    void setPollIntervalMs(int ms);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPoll();

private:
    void applyDepth(const MarketDepth& md);
    void applyQuote(const Quote& q);
    void applyTransactions(const std::vector<Tick>& ticks);
    void resetLabels();

    IDataProvider* provider_ = nullptr;
    StockCode code_;
    QString name_;   // 正在查看的个股名称
    QTimer* timer_ = nullptr;
    bool polling_ = false;

    QLabel* title_ = nullptr;
    QLabel* priceLabel_ = nullptr;
    QLabel* changeLabel_ = nullptr;
    QLabel* askPrice_[5] = {};
    QLabel* askVol_[5] = {};
    QLabel* bidPrice_[5] = {};
    QLabel* bidVol_[5] = {};
    QTableView* txView_ = nullptr;      // 成交明细表
    QStandardItemModel* txModel_ = nullptr;
};

} // namespace st
