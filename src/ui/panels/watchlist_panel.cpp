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
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);  // 代码/名称/现价/涨跌幅四列均分
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

    load();      // 恢复 + 缺失名称即时查（缓存）→ 未命中再异步补齐
    refresh();   // 立即拉一次行情
}

void WatchlistPanel::load() {
    auto entries = WatchlistStore::load(path_);
    items_.clear();
    items_.reserve(entries.size());
    bool missing = false;
    for (auto& e : entries) {
        WatchItem it;
        it.code = std::move(e.code);
        if (!e.name.empty()) {
            // 已持久化名称 → 即时显示（无占位窗口）
            it.name = QString::fromUtf8(e.name.c_str(), static_cast<int>(e.name.size()));
        } else if (provider_) {
            // 无缓存名称 → 即时同步查（TDX 股票列表缓存命中即快；冷缓存一次拉取）
            if (auto info = provider_->getStockInfo(it.code)) {
                it.name = QString::fromUtf8(info->name.c_str(),
                                            static_cast<int>(info->name.size()));
            }
        }
        if (it.name.isEmpty()) {
            it.name = QString::fromStdString(it.code.displayCode());  // 兜底占位
            missing = true;
        }
        items_.push_back(std::move(it));
    }
    if (model_) model_->setItems(items_);
    stack_->setCurrentWidget(items_.empty()
        ? static_cast<QWidget*>(emptyLabel_) : static_cast<QWidget*>(table_));
    if (missing) resolveNames();  // 仍有缺失 → 异步补齐 + 持久化
}

void WatchlistPanel::save() {
    std::vector<WatchlistStore::Entry> entries;
    entries.reserve(items_.size());
    for (const auto& it : items_) {
        WatchlistStore::Entry e;
        e.code = it.code;
        e.name = it.name.toUtf8().constData();  // 持久化当前名称（重启后即时显示）
        entries.push_back(std::move(e));
    }
    WatchlistStore::save(path_, entries);
}

void WatchlistPanel::resolveNames() {
    if (!provider_ || items_.empty() || namesFetching_) return;
    namesFetching_ = true;
    const int seq = ++nameSeq_;
    IDataProvider* provider = provider_;
    QPointer<WatchlistPanel> guard(this);
    ThreadPool::submitIO([provider, guard, seq] {
        // 名称 map：TDX 股票列表缓存（SH+SZ），全量构建一次
        std::unordered_map<std::string, std::string> names;
        for (const auto m : {Market::SH, Market::SZ}) {
            for (const auto& s : provider->getStockList(m)) {
                if (s.code.isValid()) names[s.code.fullCode()] = s.name;
            }
        }
        QMetaObject::invokeMethod(guard, [guard, seq, names = std::move(names)]() mutable {
            guard->onNamesReady(seq, std::move(names));
        }, Qt::QueuedConnection);
    });
}

void WatchlistPanel::onNamesReady(int seq,
                                  std::unordered_map<std::string, std::string> names) {
    namesFetching_ = false;
    if (seq != nameSeq_) return;  // 陈旧丢弃
    for (auto& it : items_) {
        const auto nit = names.find(it.code.fullCode());
        if (nit != names.end()) it.name = QString::fromStdString(nit->second);
    }
    if (model_) model_->setItems(items_);
    save();  // 持久化已解析名称（重启后即时显示，避免占位窗口）
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
    bool needsNames = false;
    for (auto& it : items_) {
        const auto qit = qmap.find(it.code.fullCode());
        if (qit != qmap.end()) {
            it.price = qit->second->lastPrice;
            it.changePct = qit->second->change;
        }
        // 名称仍为占位代码（首次解析时 TDX 缓存可能未就绪）→ 下次行情后重试
        if (it.name == QString::fromStdString(it.code.displayCode())) needsNames = true;
    }
    if (model_) model_->setItems(items_);
    if (needsNames && !namesFetching_) resolveNames();
}

void WatchlistPanel::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || !model_) return;
    const auto& it = model_->itemAt(index.row());
    if (!it.code.isValid()) return;
    // 名称仍为占位代码（异步解析未完成）→ 双击时即时查真实名称（TDX 股票列表缓存命中即快）
    QString name = it.name;
    if (name == QString::fromStdString(it.code.displayCode()) && provider_) {
        if (auto info = provider_->getStockInfo(it.code)) {
            name = QString::fromUtf8(info->name.c_str(), static_cast<int>(info->name.size()));
            if (!name.isEmpty()) emit openChart(it.code, name);
            return;
        }
    }
    emit openChart(it.code, name);
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
