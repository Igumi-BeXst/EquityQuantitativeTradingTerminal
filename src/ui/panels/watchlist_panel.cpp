#include "ui/panels/watchlist_panel.h"
#include "core/app_paths.h"
#include "core/thread_pool.h"
#include "foundation/utils/watchlist_store.h"
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace st {

namespace {
constexpr int kRefreshMs = 10000;  // 10s 刷新（交互优先级）
}  // namespace

WatchlistPanel::WatchlistPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      path_(AppPaths::configDir() + "/watchlist.json") {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    stack_ = new QStackedWidget(this);
    model_ = new WatchlistModel(this);
    table_ = new QTableView(stack_);
    table_->setModel(model_);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);  // 名称/现价/涨跌幅三列均分
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    stack_->addWidget(table_);
    emptyLabel_ = new QLabel(tr("暂无自选，在图表周期栏点「加入自选」添加"), stack_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("color:#666666;"));
    emptyLabel_->setWordWrap(true);
    stack_->addWidget(emptyLabel_);
    layout->addWidget(stack_, 1);

    connect(table_, &QTableView::doubleClicked, this, &WatchlistPanel::onDoubleClicked);
    connect(table_, &QWidget::customContextMenuRequested,
            this, &WatchlistPanel::onContextMenu);

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &WatchlistPanel::refresh);
    timer_->start();

    load();      // 从 watchlist.json 恢复
    refresh();   // 立即拉一次行情
}

void WatchlistPanel::load() {
    auto codes = WatchlistStore::load(path_);
    items_.clear();
    items_.reserve(codes.size());
    for (auto& c : codes) {
        WatchItem it;
        it.code = std::move(c);
        it.name = QString::fromStdString(it.code.displayCode());  // 名称先占位，行情后回填
        items_.push_back(std::move(it));
    }
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(items_.empty()
        ? static_cast<QWidget*>(emptyLabel_) : static_cast<QWidget*>(table_));
}

void WatchlistPanel::save() {
    std::vector<StockCode> codes;
    codes.reserve(items_.size());
    for (const auto& it : items_) codes.push_back(it.code);
    WatchlistStore::save(path_, codes);
}

bool WatchlistPanel::contains(const StockCode& code) const {
    const std::string key = code.fullCode();
    for (const auto& it : items_) if (it.code.fullCode() == key) return true;
    return false;
}

void WatchlistPanel::add(const StockCode& code, const QString& name) {
    if (contains(code)) return;
    WatchItem it;
    it.code = code;
    it.name = name.isEmpty() ? QString::fromStdString(code.displayCode()) : name;
    items_.push_back(std::move(it));
    save();
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(static_cast<QWidget*>(table_));
    emit watchlistChanged(code);
    refresh();   // 新加入立即拉行情
}

void WatchlistPanel::remove(const StockCode& code) {
    const std::string key = code.fullCode();
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                 [&key](const WatchItem& it) { return it.code.fullCode() == key; }),
                 items_.end());
    save();
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(items_.empty()
        ? static_cast<QWidget*>(emptyLabel_) : static_cast<QWidget*>(table_));
    emit watchlistChanged(code);
}

void WatchlistPanel::refresh() {
    if (!provider_ || items_.empty() || fetching_) return;
    fetching_ = true;
    const int seq = ++fetchSeq_;
    std::vector<StockCode> codes;
    codes.reserve(items_.size());
    for (const auto& it : items_) codes.push_back(it.code);
    IDataProvider* provider = provider_;
    QPointer<WatchlistPanel> guard(this);
    ThreadPool::submitIO([provider, guard, seq, codes] {
        auto quotes = provider->batchQuoteInteractive(codes);
        QMetaObject::invokeMethod(guard, [guard, seq, quotes = std::move(quotes)]() mutable {
            guard->onQuotesReady(seq, std::move(quotes));
        }, Qt::QueuedConnection);
    });
}

void WatchlistPanel::onQuotesReady(int seq, std::vector<Quote> quotes) {
    fetching_ = false;
    if (seq != fetchSeq_) return;  // 陈旧丢弃
    std::unordered_map<std::string, const Quote*> qmap;
    qmap.reserve(quotes.size());
    for (const auto& q : quotes) qmap[q.code.fullCode()] = &q;
    for (auto& it : items_) {
        const auto qit = qmap.find(it.code.fullCode());
        if (qit != qmap.end()) {
            it.price = qit->second->lastPrice;
            it.changePct = qit->second->change;
        }
    }
    if (model_) model_->setItems(items_);
}

void WatchlistPanel::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !model_) return;
    const auto& it = model_->itemAt(index.row());
    if (it.code.isValid()) emit openChart(it.code, it.name);
}

void WatchlistPanel::onContextMenu(const QPoint& pos) {
    const QModelIndex idx = table_->indexAt(pos);
    if (!idx.isValid()) return;
    const auto& it = model_->itemAt(idx.row());
    if (!it.code.isValid()) return;
    QMenu menu(this);
    QAction* removeAct = menu.addAction(tr("移除自选 %1").arg(it.name));
    QAction* refreshAct = menu.addAction(tr("刷新"));
    QAction* chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
    if (chosen == removeAct) remove(it.code);
    else if (chosen == refreshAct) refresh();
}

} // namespace st

#include "moc_watchlist_panel.cpp"
