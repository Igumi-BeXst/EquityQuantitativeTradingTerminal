#pragma once

#include "foundation/stock_code.h"
#include "foundation/tick.h"
#include <QWidget>
#include <unordered_map>
#include <vector>

class QTimer;
class QTableView;
class QLabel;
class QPushButton;
class QTabWidget;

namespace st {

class IDataProvider;
class MarketRankModel;
struct MarketRankItem;

/// 市场全景面板 — 涨幅榜/跌幅榜/市场宽度（实时 batchQuote 轻量方案）
///
/// 池 = 精选 129 只；QTimer 10s + 手动刷新；refreshing_ 防重叠 + gen_ 防陈旧回写。
/// 双击行 → openChart(StockCode) 打开 K 线图。
class MarketPanel : public QWidget {
    Q_OBJECT

public:
    explicit MarketPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    void openChart(const StockCode& code);

private slots:
    void refresh();
    void onQuotesReady(const std::vector<Quote>& quotes);

private:
    void onGainersDoubleClicked(const QModelIndex& index);
    void onLosersDoubleClicked(const QModelIndex& index);

    IDataProvider* provider_ = nullptr;
    QTimer* timer_ = nullptr;
    bool refreshing_ = false;
    int gen_ = 0;

    std::vector<StockCode> pool_;
    std::unordered_map<std::string, std::string> nameByCode_;

    QTabWidget* tabs_ = nullptr;
    MarketRankModel* gainersModel_ = nullptr;
    MarketRankModel* losersModel_ = nullptr;
    QTableView* gainersView_ = nullptr;
    QTableView* losersView_ = nullptr;
    QLabel* adv_ = nullptr;
    QLabel* dec_ = nullptr;
    QLabel* flat_ = nullptr;
    QLabel* ratio_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
};

} // namespace st
