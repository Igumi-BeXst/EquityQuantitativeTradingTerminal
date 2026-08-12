#include "ui/panels/market_panel.h"
#include "ui/utils/table_csv_export.h"
#include "ui/models/market_rank_model.h"
#include "ui/models/market_rank_sort_proxy.h"
#include "ui/panels/sector_panel.h"          // SectorListPage
#include "ui/widgets/sector_constituents_dialog.h"
#include "data/eastmoney_sector_provider.h"  // SectorType
#include "data/idata_provider.h"
#include "data/akshare_provider.h"
#include "data/tencent_provider.h"
#include "data/curated_stocks.h"
#include "data/tdx/tdx_models.h"
#include "core/thread_pool.h"
#include "foundation/enums.h"
#include <QTimer>
#include <QTableView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QPointer>
#include <QShowEvent>
#include <QHideEvent>
#include <QDateTime>
#include <algorithm>

namespace st {

namespace {
constexpr int kTopN = 30;
constexpr int kRefreshMs = 30000;  // 全 A 股池（~5000 只）批量拉取较重，30s 刷新

/// 涨幅/跌幅榜列宽：名称自适应拉伸，其余固定宽度（配合模型 TextAlignmentRole 居中）
void applyRankHeader(QTableView* view) {
    auto* h = view->horizontalHeader();
    h->setSectionResizeMode(QHeaderView::Fixed);
    h->setSectionResizeMode(1, QHeaderView::Stretch);  // 名称自适应
    h->setStretchLastSection(false);
    view->setColumnWidth(0, 62);   // 代码
    view->setColumnWidth(2, 70);   // 现价
    view->setColumnWidth(3, 84);   // 涨跌幅
    view->setColumnWidth(4, 70);   // 换手率
}
}  // namespace

MarketPanel::MarketPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      fundProvider_(std::make_shared<AKShareProvider>()),
      tencentProvider_(std::make_shared<TencentProvider>()) {
    // 池 + 名称表（精选股票）
    for (const auto& c : kCuratedSH) {
        StockCode sc(Market::SH, c.code);
        pool_.push_back(sc);
        nameByCode_[sc.displayCode()] = c.name;
    }
    for (const auto& c : kCuratedSZ) {
        StockCode sc(Market::SZ, c.code);
        pool_.push_back(sc);
        nameByCode_[sc.displayCode()] = c.name;
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // 刷新按钮 + 导出当前 tab
    auto* topRow = new QHBoxLayout();
    refreshBtn_ = new QPushButton(tr("刷新"));
    topRow->addWidget(refreshBtn_);
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        QAbstractItemView* view = nullptr;
        const int cur = tabs_->currentIndex();
        if (cur == 0)
            view = gainersView_;
        else if (cur == 1)
            view = losersView_;
        else if (cur == tabs_->indexOf(industryPage_) && industryPage_)
            view = industryPage_->tableView();
        else if (cur == tabs_->indexOf(conceptPage_) && conceptPage_)
            view = conceptPage_->tableView();
        if (view) st::ui::exportViewToCsv(view, this, "market_ranking.csv");
    });
    topRow->addWidget(exportBtn);
    topRow->addStretch();
    layout->addLayout(topRow);
    connect(refreshBtn_, &QPushButton::clicked, this, &MarketPanel::refresh);

    // Tab: 涨幅榜 / 跌幅榜 / 市场宽度
    tabs_ = new QTabWidget(this);

    gainersModel_ = new MarketRankModel(this);
    gainersProxy_ = new MarketRankSortProxy(this);
    gainersProxy_->setSourceModel(gainersModel_);
    gainersView_ = new QTableView();
    gainersView_->setModel(gainersProxy_);
    gainersView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    gainersView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    gainersView_->setSortingEnabled(true);       // 点击列头排序
    gainersView_->sortByColumn(3, Qt::DescendingOrder);  // 默认涨跌幅降序
    applyRankHeader(gainersView_);  // 名称自适应拉伸，其余固定居中
    tabs_->addTab(gainersView_, tr("涨幅榜"));
    connect(gainersView_, &QTableView::doubleClicked,
            this, &MarketPanel::onGainersDoubleClicked);

    losersModel_ = new MarketRankModel(this);
    losersProxy_ = new MarketRankSortProxy(this);
    losersProxy_->setSourceModel(losersModel_);
    losersView_ = new QTableView();
    losersView_->setModel(losersProxy_);
    losersView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    losersView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    losersView_->setSortingEnabled(true);       // 点击列头排序
    losersView_->sortByColumn(3, Qt::AscendingOrder);   // 默认涨跌幅升序
    applyRankHeader(losersView_);  // 名称自适应拉伸，其余固定居中
    tabs_->addTab(losersView_, tr("跌幅榜"));
    connect(losersView_, &QTableView::doubleClicked,
            this, &MarketPanel::onLosersDoubleClicked);

    // 板块榜单页（固定类型；TDX 全量；刷新由统一错峰时钟调度，见 refresh()）
    industryPage_ = new SectorListPage(provider_, SectorType::Industry, this);
    tabs_->addTab(industryPage_, tr("行业板块"));
    conceptPage_ = new SectorListPage(provider_, SectorType::Concept, this);
    tabs_->addTab(conceptPage_, tr("概念板块"));
    connect(industryPage_, &SectorListPage::openSectorChart,
            this, &MarketPanel::onOpenSectorChart);
    connect(conceptPage_, &SectorListPage::openSectorChart,
            this, &MarketPanel::onOpenSectorChart);
    connect(industryPage_, &SectorListPage::openConstituents,
            this, &MarketPanel::onOpenConstituents);
    connect(conceptPage_, &SectorListPage::openConstituents,
            this, &MarketPanel::onOpenConstituents);
    // 切到板块 tab：立即后台刷新（缓存已由错峰轮询保持新鲜，此刷新覆盖首次/过期数据）
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == tabs_->indexOf(industryPage_) && industryPage_) industryPage_->refresh();
        else if (index == tabs_->indexOf(conceptPage_) && conceptPage_) conceptPage_->refresh();
    });

    // 市场宽度
    auto* breadthBox = new QGroupBox(tr("市场宽度"));
    auto* bg = new QGridLayout(breadthBox);
    auto addLabel = [&](int r, int c, const QString& name, QLabel*& out) {
        bg->addWidget(new QLabel(name), r, c * 2);
        out = new QLabel("--");
        bg->addWidget(out, r, c * 2 + 1);
    };
    addLabel(0, 0, tr("上涨"), adv_);
    addLabel(0, 1, tr("下跌"), dec_);
    addLabel(1, 0, tr("平盘"), flat_);
    addLabel(1, 1, tr("涨跌比"), ratio_);

    layout->addWidget(tabs_, 1);
    // 市场宽度常驻底部（不占 tab，免切换即见）
    layout->addWidget(breadthBox);

    // 定时刷新
    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &MarketPanel::refresh);
    timer_->start();
    refresh();  // 立即刷一次（先精选池）

    // 全量池首次加载由 showEvent 触发（池子小/过期时才重载，见 showEvent）
}

void MarketPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 重开窗口：恢复定时刷新（数据缓存已在，openMarketWindow 会再刷一次保持新鲜）
    if (timer_ && !timer_->isActive()) timer_->start();
    // 池子小（上次全量加载失败，只剩精选池）或过期（>1h，可能有新上市股票）
    // → 绕过 TDX 股票列表缓存重载全量池，避免新股/新列表永远缺失
    if (provider_) {
        const bool smallPool = pool_.size() < 500;
        const bool stalePool = lastPoolLoadSec_ == 0 ||
            (QDateTime::currentSecsSinceEpoch() - lastPoolLoadSec_ > 3600);
        if (smallPool || stalePool) {
            provider_->invalidateStockListCache();
            loadFullPool();
        }
    }
}

void MarketPanel::loadFullPool() {
    if (!provider_) return;
    // 安全异步：按值捕获 provider + QPointer 守卫投递回主线程（面板销毁后自动跳过）
    QPointer<MarketPanel> guard(this);
    IDataProvider* provider = provider_;
    ThreadPool::submitIO([provider, guard] {
        struct PoolData {
            std::vector<StockCode> pool;
            std::unordered_map<std::string, std::string> names;
        };
        PoolData d;
        auto sh = provider->getStockList(Market::SH);
        for (const auto& s : sh) {
            if (tdx::isTradableAShare(s.code)) {
                d.pool.push_back(s.code);
                d.names[s.code.displayCode()] = s.name;
            }
        }
        auto sz = provider->getStockList(Market::SZ);
        for (const auto& s : sz) {
            if (tdx::isTradableAShare(s.code)) {
                d.pool.push_back(s.code);
                d.names[s.code.displayCode()] = s.name;
            }
        }
        QMetaObject::invokeMethod(guard, [guard, d = std::move(d)]() mutable {
            if (d.pool.empty()) return;
            guard->pool_ = std::move(d.pool);
            guard->nameByCode_ = std::move(d.names);
            guard->lastPoolLoadSec_ = QDateTime::currentSecsSinceEpoch();
            guard->refresh();  // 用全市场池立即刷一次
        }, Qt::QueuedConnection);
    });
}

void MarketPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    // 窗口关闭隐藏：暂停定时刷新，避免后台持续占 TDX 连接
    if (timer_) timer_->stop();
}

void MarketPanel::refresh() {
    if (refreshing_ || !provider_) return;
    refreshing_ = true;
    const int gen = ++gen_;
    std::vector<StockCode> pool = pool_;
    // 安全异步：按值捕获 provider + QPointer 守卫投递回主线程
    QPointer<MarketPanel> guard(this);
    IDataProvider* provider = provider_;
    ThreadPool::submitIO([provider, guard, gen, pool] {
        auto quotes = provider->batchQuote(pool);
        QMetaObject::invokeMethod(guard, [guard, gen, quotes = std::move(quotes)]() mutable {
            guard->refreshing_ = false;
            if (gen != guard->gen_) return;  // 陈旧回写丢弃
            guard->onQuotesReady(quotes);
        }, Qt::QueuedConnection);
    });

    // 统一错峰：市场池 t=0（上面），行业 +1s，概念 +2s（板块走 batchQuoteInteractive
    // 交互优先级，不与批量队列竞争；singleShot 以 this 为 context，本面板销毁即取消）
    QTimer::singleShot(1000, this, [this] { if (industryPage_) industryPage_->refresh(); });
    QTimer::singleShot(2000, this, [this] { if (conceptPage_) conceptPage_->refresh(); });
}

void MarketPanel::onQuotesReady(const std::vector<Quote>& quotes) {
    std::vector<MarketRankItem> items;
    items.reserve(quotes.size());
    int advancing = 0, declining = 0, flat = 0;
    for (const auto& q : quotes) {
        // 排除停牌/无行情（TDX 对停牌股返回 price=0 → change 会误算成 -100%）。
        // 注：新股首日 preClose 可能为 0（无昨收），不再按 preClose 过滤，避免漏掉新上市股票。
        if (q.lastPrice <= 0) continue;
        MarketRankItem item;
        item.code = q.code;
        auto it = nameByCode_.find(q.code.displayCode());
        item.name = it != nameByCode_.end() ? it->second : q.code.displayCode();
        item.price = q.lastPrice;
        item.changePct = q.change;
        item.turnover = q.turnover;
        items.push_back(std::move(item));
        if (q.change > 0) ++advancing;
        else if (q.change < 0) ++declining;
        else ++flat;
    }

    std::sort(items.begin(), items.end(),
              [](const MarketRankItem& a, const MarketRankItem& b) {
                  return a.changePct > b.changePct;
              });

    // 涨幅榜 top30
    std::vector<MarketRankItem> gainers(items.begin(), items.begin() + std::min(kTopN, (int)items.size()));
    gainersModel_->setItems(gainers);
    // 跌幅榜 top30（升序）
    std::vector<MarketRankItem> losers;
    int start = std::max(0, (int)items.size() - kTopN);
    for (int i = (int)items.size() - 1; i >= start; --i) losers.push_back(items[i]);
    losersModel_->setItems(losers);

    // 市场宽度
    adv_->setText(QString::number(advancing));
    dec_->setText(QString::number(declining));
    flat_->setText(QString::number(flat));
    const double ratio = (advancing + declining) > 0
        ? static_cast<double>(advancing) / (advancing + declining) : 0.0;
    ratio_->setText(QString::number(ratio, 'f', 2));

    // 异步回填显示股票的换手率（东财 ulist 批量，TDX 报价不含换手率）
    std::vector<MarketRankItem> shown;
    shown.reserve(gainers.size() + losers.size());
    shown.insert(shown.end(), gainers.begin(), gainers.end());
    shown.insert(shown.end(), losers.begin(), losers.end());
    requestTurnover(shown);
}

void MarketPanel::requestTurnover(const std::vector<MarketRankItem>& items) {
    if ((!fundProvider_ && !tencentProvider_) || items.empty()) return;
    std::vector<StockCode> codes;
    codes.reserve(items.size());
    for (const auto& it : items) codes.push_back(it.code);
    // 安全异步：shared_ptr 按值捕获 + QPointer 守卫（面板销毁后 provider 仍存活）
    const auto em = fundProvider_;      // 东财主源
    const auto tq = tencentProvider_;   // 腾讯备源
    QPointer<MarketPanel> guard(this);
    ThreadPool::submitIO([em, tq, guard, codes] {
        auto funds = em ? em->batchQuoteFundamentals(codes)
                        : std::vector<QuoteFundamentals>{};
        if (funds.empty() && tq) funds = tq->batchQuoteFundamentals(codes);  // 东财失败回退腾讯
        QMetaObject::invokeMethod(guard,
            [guard, funds = std::move(funds)]() mutable {
                guard->applyTurnover(funds);
            }, Qt::QueuedConnection);
    });
}

void MarketPanel::applyTurnover(const std::vector<QuoteFundamentals>& funds) {
    if (!gainersModel_ || !losersModel_) return;
    for (const auto& f : funds) {
        if (!f.valid || f.turnoverRate <= 0.0) continue;
        gainersModel_->updateTurnover(f.code.fullCode(), f.turnoverRate);
        losersModel_->updateTurnover(f.code.fullCode(), f.turnoverRate);
    }
}

void MarketPanel::onGainersDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !gainersProxy_ || !gainersModel_) return;
    const auto src = gainersProxy_->mapToSource(index);  // 代理行 → 源行
    if (!src.isValid()) return;
    const auto& item = gainersModel_->itemAt(src.row());
    emit openChart(item.code, QString::fromUtf8(item.name.c_str(),
                                               static_cast<int>(item.name.size())));
}

void MarketPanel::onLosersDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !losersProxy_ || !losersModel_) return;
    const auto src = losersProxy_->mapToSource(index);  // 代理行 → 源行
    if (!src.isValid()) return;
    const auto& item = losersModel_->itemAt(src.row());
    emit openChart(item.code, QString::fromUtf8(item.name.c_str(),
                                               static_cast<int>(item.name.size())));
}

void MarketPanel::onOpenSectorChart(const StockCode& code, const QString& name) {
    // 板块指数开图：转发给 MainWindow 的轻量处理（只 loadStock，不设置右侧面板）
    emit openSectorChart(code, name);
}

void MarketPanel::onOpenConstituents(const QString& name) {
    if (name.isEmpty() || !provider_) return;
    auto* dlg = new SectorConstituentsDialog(provider_, name, this);
    connect(dlg, &SectorConstituentsDialog::openChart,
            this, &MarketPanel::onOpenSectorChart);  // 复用板块开图转发（轻量开图）
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

} // namespace st

#include "moc_market_panel.cpp"
