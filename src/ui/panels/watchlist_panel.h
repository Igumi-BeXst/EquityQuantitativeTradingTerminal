#pragma once

#include "foundation/stock_code.h"
#include "data/idata_provider.h"
#include "ui/models/watchlist_model.h"
#include <QString>
#include <QWidget>
#include <vector>

class QTimer;
class QTableView;
class QStackedWidget;
class QLabel;

namespace st {

/// 自选股面板 — 用户自选列表（图表周期栏「加入自选」添加；右键移除；双击开图）
///
/// 数据：batchQuoteInteractive（交互优先级）定时刷新（10s）；持久化 watchlist.json（WatchlistStore）。
/// 展示：QTableView + WatchlistModel 虚拟化（名称/现价/涨跌幅，红涨绿跌）。
class WatchlistPanel : public QWidget {
    Q_OBJECT

public:
    explicit WatchlistPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 拉取自选股行情（IO 池；seq 去陈旧；在途则跳过）
    void refresh();
    bool contains(const StockCode& code) const;
    void add(const StockCode& code, const QString& name);
    void remove(const StockCode& code);

signals:
    void openChart(const StockCode& code, const QString& name);
    void watchlistChanged(const StockCode& code);

private:
    void onContextMenu(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);
    void onQuotesReady(int seq, std::vector<Quote> quotes);
    void load();
    void save();

    IDataProvider* provider_ = nullptr;
    std::string path_;
    int fetchSeq_ = 0;
    bool fetching_ = false;
    std::vector<WatchItem> items_;
    QTimer* timer_ = nullptr;
    QTableView* table_ = nullptr;
    WatchlistModel* model_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
};

} // namespace st
