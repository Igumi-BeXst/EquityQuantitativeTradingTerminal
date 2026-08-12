#include "ui/widgets/sector_constituents_dialog.h"
#include "data/eastmoney_sector_constituents.h"
#include "core/thread_pool.h"
#include "ui/models/market_rank_model.h"
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace st {

namespace {
constexpr int kTopN = 200;  // 成分股最多显示前 200（东财分页，防超长列表）
}  // namespace

SectorConstituentsDialog::SectorConstituentsDialog(IDataProvider* provider,
                                                   const QString& boardName, QWidget* parent)
    : QDialog(parent), provider_(provider), boardName_(boardName) {
    setWindowTitle(tr("成分股 — %1").arg(boardName));
    setMinimumSize(520, 480);

    auto* layout = new QVBoxLayout(this);
    title_ = new QLabel(tr("加载 %1 成分股…").arg(boardName), this);
    title_->setStyleSheet(QStringLiteral("color:#888888;"));
    layout->addWidget(title_);

    model_ = new MarketRankModel(this);
    table_ = new QTableView(this);
    table_->setModel(model_);
    table_->setColumnHidden(4, true);  // 隐藏换手率列（TDX 行情无此数据）
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setShowGrid(false);
    layout->addWidget(table_, 1);

    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || !model_) return;
        const auto& item = model_->itemAt(idx.row());
        if (item.code.isValid()) emit openChart(item.code, QString::fromStdString(item.name));
    });

    fetchConstituents();
}

void SectorConstituentsDialog::fetchConstituents() {
    if (!provider_ || fetching_) return;
    fetching_ = true;
    const QString board = boardName_;
    IDataProvider* provider = provider_;
    QPointer<SectorConstituentsDialog> guard(this);
    ThreadPool::submitIO([provider, guard, board] {
        EastMoneySectorConstituents em;
        auto codes = em.fetchConstituents(board.toStdString());
        // 名称 map：TDX 股票列表缓存（SH+SZ），供成分股中文名
        std::unordered_map<std::string, std::string> names;
        for (const auto m : {Market::SH, Market::SZ}) {
            for (const auto& s : provider->getStockList(m)) {
                if (!s.code.isValid()) continue;
                names[s.code.fullCode()] = s.name;
            }
        }
        QMetaObject::invokeMethod(guard, [guard, codes = std::move(codes),
                                          names = std::move(names)]() mutable {
            guard->fetching_ = false;
            guard->names_ = std::move(names);
            guard->onCodesReady(std::move(codes));
        }, Qt::QueuedConnection);
    });
}

void SectorConstituentsDialog::onCodesReady(std::vector<StockCode> codes) {
    codes_ = std::move(codes);
    if (codes_.empty()) {
        setWindowTitle(tr("成分股 — %1（未找到）").arg(boardName_));
        return;
    }
    fetchQuotes(codes_);
}

void SectorConstituentsDialog::fetchQuotes(const std::vector<StockCode>& codes) {
    if (!provider_ || codes.empty()) return;
    std::vector<StockCode> batch(codes.begin(), codes.begin() +
        std::min(kTopN, static_cast<int>(codes.size())));
    const int seq = ++fetchSeq_;
    IDataProvider* provider = provider_;
    QPointer<SectorConstituentsDialog> guard(this);
    ThreadPool::submitIO([provider, guard, seq, batch] {
        auto quotes = provider->batchQuoteInteractive(batch);
        QMetaObject::invokeMethod(guard, [guard, seq, quotes = std::move(quotes)]() mutable {
            if (seq != guard->fetchSeq_) return;  // 陈旧丢弃
            guard->onQuotesReady(std::move(quotes));
        }, Qt::QueuedConnection);
    });
}

void SectorConstituentsDialog::onQuotesReady(std::vector<Quote> quotes) {
    std::vector<MarketRankItem> items;
    items.reserve(quotes.size());
    for (const auto& q : quotes) {
        if (q.lastPrice <= 0 || q.preClose <= 0) continue;  // 停牌跳过
        MarketRankItem it;
        it.code = q.code;
        auto nit = names_.find(q.code.fullCode());
        it.name = nit != names_.end() ? nit->second : q.code.displayCode();
        it.price = q.lastPrice;
        it.changePct = q.change;
        items.push_back(std::move(it));
    }
    if (model_) model_->setItems(items);
    if (title_) title_->hide();
    setWindowTitle(tr("成分股 — %1（%2）").arg(boardName_).arg(items.size()));
}

} // namespace st

#include "moc_sector_constituents_dialog.cpp"
