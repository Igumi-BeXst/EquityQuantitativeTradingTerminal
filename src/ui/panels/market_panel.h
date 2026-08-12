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
class QShowEvent;
class QHideEvent;

namespace st {

class IDataProvider;
class AKShareProvider;
class MarketRankModel;
class SectorListPage;
struct MarketRankItem;

/// 市场全景面板 — 4 tab（涨幅榜/跌幅榜/行业板块/概念板块）+ 市场宽度常驻底部
/// 统一 30s 错峰刷新：市场池 t=0、行业 +1s、概念 +2s；板块走交互优先级 batchQuoteInteractive。
/// 板块行双击 → openSectorChart（轻量开图，不动右侧盘口面板）。
class MarketPanel : public QWidget {
    Q_OBJECT

public:
    explicit MarketPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 手动刷新行情（供定时任务/外部调用）
    void refresh();

protected:
    /// 显示时重启定时刷新（重开窗口数据保鲜）
    void showEvent(QShowEvent* event) override;
    /// 隐藏时暂停定时刷新（窗口关闭隐藏后不再占 TDX 连接）
    void hideEvent(QHideEvent* event) override;

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
    void onOpenConstituents(const QString& name);

    /// 异步拉取显示股票的换手率（东财 ulist，失败回退腾讯），回填排名表
    void requestTurnover(const std::vector<MarketRankItem>& items);
    void applyTurnover(const std::vector<QuoteFundamentals>& funds);

    /// 异步加载全 A 股池（TDX 全列表过滤可交易股，覆盖完整市场），完成后立即刷新
    void loadFullPool();

    IDataProvider* provider_ = nullptr;
    qint64 lastPoolLoadSec_ = 0;  // 上次全量池加载时间（epoch 秒；0=未加载）
    // 基本面专用数据源（东财 ulist 主源 + 腾讯备源）；shared 供异步按值捕获
    std::shared_ptr<AKShareProvider> fundProvider_;
    std::shared_ptr<class TencentProvider> tencentProvider_;
    QTimer* timer_ = nullptr;
    bool refreshing_ = false;
    int gen_ = 0;

    std::vector<StockCode> pool_;
    std::unordered_map<std::string, std::string> nameByCode_;

    QTabWidget* tabs_ = nullptr;
    MarketRankModel* gainersModel_ = nullptr;
    MarketRankModel* losersModel_ = nullptr;
    class MarketRankSortProxy* gainersProxy_ = nullptr;  // 列头排序代理
    class MarketRankSortProxy* losersProxy_ = nullptr;
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
