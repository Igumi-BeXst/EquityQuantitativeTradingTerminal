#pragma once

#include "foundation/stock_code.h"
#include "foundation/tick.h"
#include "data/quote_fundamentals.h"
#include <QWidget>
#include <memory>
#include <unordered_map>
#include <vector>

class QTimer;
class QTableView;
class QLabel;
class QPushButton;
class QTabWidget;

namespace st {

class IDataProvider;
class AKShareProvider;
class MarketRankModel;
class SectorListPage;
struct MarketRankItem;

/// 市场全景面板 — 涨幅榜/跌幅榜/市场宽度（实时 batchQuote 轻量方案）
///
/// 池 = 精选 129 只；QTimer 10s + 手动刷新；refreshing_ 防重叠 + gen_ 防陈旧回写。
/// 双击行 → openChart(StockCode) 打开 K 线图。
class MarketPanel : public QWidget {
    Q_OBJECT

public:
    explicit MarketPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 手动刷新行情（供定时任务/外部调用）
    void refresh();

signals:
    void openChart(const StockCode& code, const QString& name);
    /// 板块指数开图（双击板块行；右侧盘口/关键数据/筹码面板保持不动）
    void openSectorChart(const StockCode& code, const QString& name);

private slots:
    void onQuotesReady(const std::vector<Quote>& quotes);

private:
    void onGainersDoubleClicked(const QModelIndex& index);
    void onLosersDoubleClicked(const QModelIndex& index);
    void onOpenSectorChart(const StockCode& code, const QString& name);

    /// 异步拉取显示股票的东财换手率，回填排名表
    void requestTurnover(const std::vector<MarketRankItem>& items);
    void applyTurnover(const std::vector<QuoteFundamentals>& funds);

    IDataProvider* provider_ = nullptr;
    // 基本面专用数据源（东财 ulist，不依赖主源）；shared 供异步按值捕获
    std::shared_ptr<AKShareProvider> fundProvider_;
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

    SectorListPage* industryPage_ = nullptr;   // 行业板块 tab
    SectorListPage* conceptPage_ = nullptr;    // 概念板块 tab
};

} // namespace st
