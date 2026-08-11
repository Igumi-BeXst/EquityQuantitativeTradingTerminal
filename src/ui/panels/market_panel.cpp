#include "ui/panels/market_panel.h"
#include "ui/utils/table_csv_export.h"
#include "ui/models/market_rank_model.h"
#include "data/idata_provider.h"
#include "data/akshare_provider.h"
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
#include <algorithm>

namespace st {

namespace {
constexpr int kTopN = 30;
constexpr int kRefreshMs = 30000;  // 全 A 股池（~5000 只）批量拉取较重，30s 刷新
}  // namespace

MarketPanel::MarketPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      fundProvider_(std::make_shared<AKShareProvider>()) {
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
        QAbstractItemView* view = (tabs_->currentIndex() == 0)
            ? static_cast<QAbstractItemView*>(gainersView_)
            : static_cast<QAbstractItemView*>(losersView_);
        st::ui::exportViewToCsv(view, this, "market_ranking.csv");
    });
    topRow->addWidget(exportBtn);
    topRow->addStretch();
    layout->addLayout(topRow);
    connect(refreshBtn_, &QPushButton::clicked, this, &MarketPanel::refresh);

    // Tab: 涨幅榜 / 跌幅榜 / 市场宽度
    tabs_ = new QTabWidget(this);

    gainersModel_ = new MarketRankModel(this);
    gainersView_ = new QTableView();
    gainersView_->setModel(gainersModel_);
    gainersView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    gainersView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    gainersView_->horizontalHeader()->setStretchLastSection(true);
    gainersView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tabs_->addTab(gainersView_, tr("涨幅榜"));
    connect(gainersView_, &QTableView::doubleClicked,
            this, &MarketPanel::onGainersDoubleClicked);

    losersModel_ = new MarketRankModel(this);
    losersView_ = new QTableView();
    losersView_->setModel(losersModel_);
    losersView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    losersView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    losersView_->horizontalHeader()->setStretchLastSection(true);
    losersView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tabs_->addTab(losersView_, tr("跌幅榜"));
    connect(losersView_, &QTableView::doubleClicked,
            this, &MarketPanel::onLosersDoubleClicked);

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
    tabs_->addTab(breadthBox, tr("市场宽度"));

    layout->addWidget(tabs_, 1);

    // 定时刷新
    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &MarketPanel::refresh);
    timer_->start();
    refresh();  // 立即刷一次（先精选池）

    // 异步加载全 A 股池（TDX 全列表过滤可交易股，覆盖完整市场），完成后立即刷新
    if (provider_) {
        // 安全异步：捕获构造参数 provider + QPointer 守卫投递回主线程（面板销毁后自动跳过）
        QPointer<MarketPanel> guard(this);
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
                guard->refresh();  // 用全市场池立即刷一次
            }, Qt::QueuedConnection);
        });
    }
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
}

void MarketPanel::onQuotesReady(const std::vector<Quote>& quotes) {
    std::vector<MarketRankItem> items;
    items.reserve(quotes.size());
    int advancing = 0, declining = 0, flat = 0;
    for (const auto& q : quotes) {
        // 排除停牌/无行情（TDX 对停牌股返回 price=0 → change 会误算成 -100%）
        if (q.lastPrice <= 0 || q.preClose <= 0) continue;
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
    if (!fundProvider_ || items.empty()) return;
    std::vector<StockCode> codes;
    codes.reserve(items.size());
    for (const auto& it : items) codes.push_back(it.code);
    // 安全异步：shared_ptr 按值捕获 + QPointer 守卫（面板销毁后 provider 仍存活）
    const auto provider = fundProvider_;
    QPointer<MarketPanel> guard(this);
    ThreadPool::submitIO([provider, guard, codes] {
        auto funds = provider->batchQuoteFundamentals(codes);
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
    if (!index.isValid()) return;
    const auto& item = gainersModel_->itemAt(index.row());
    emit openChart(item.code, QString::fromUtf8(item.name.c_str(),
                                               static_cast<int>(item.name.size())));
}

void MarketPanel::onLosersDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    const auto& item = losersModel_->itemAt(index.row());
    emit openChart(item.code, QString::fromUtf8(item.name.c_str(),
                                               static_cast<int>(item.name.size())));
}

} // namespace st

#include "moc_market_panel.cpp"
