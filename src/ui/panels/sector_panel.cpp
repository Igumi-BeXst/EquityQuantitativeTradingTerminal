#include "ui/panels/sector_panel.h"
#include "core/thread_pool.h"
#include "foundation/tick.h"
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QStackedWidget>
#include <QTableView>
#include <QTime>
#include <QVBoxLayout>
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace st {

SectorListPage::SectorListPage(IDataProvider* provider, SectorType type, QWidget* parent)
    : QWidget(parent), provider_(provider), type_(type) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 顶行：更新时间（右上角小字）
    auto* topRow = new QHBoxLayout();
    topRow->addStretch();
    updateLabel_ = new QLabel(tr("--"));
    updateLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    topRow->addWidget(updateLabel_);
    layout->addLayout(topRow);

    // 板块榜单（QTableView + SectorListModel 虚拟化渲染；模板同步涨跌幅榜——不设自定义 stylesheet）
    stack_ = new QStackedWidget(this);
    model_ = new SectorListModel(this);
    table_ = new QTableView(stack_);
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    // 注意：不设置自定义背景色（跟随应用主题，与涨跌幅榜一致）
    stack_->addWidget(table_);
    emptyLabel_ = new QLabel(tr("暂无板块数据"), stack_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("color:#666666;"));
    stack_->addWidget(emptyLabel_);
    layout->addWidget(stack_, 1);

    // 双击板块行 → 打开板块指数 K 线（与指数条行为一致；右侧盘口面板不设置）
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || !model_) return;
        const auto& r = model_->rowAt(idx.row());
        if (r.code.isValid()) emit openSectorChart(r.code, r.name);
    });

    // 构造时不自动拉取：刷新时机统一由 MarketPanel 错峰时钟调度（Task 3）
}

void SectorListPage::refresh() { fetch(); }

void SectorListPage::fetch() {
    if (!provider_ || fetching_) return;  // 在途则跳过（防重叠）
    fetching_ = true;
    const int seq = ++fetchSeq_;
    IDataProvider* provider = provider_;
    QPointer<SectorListPage> guard(this);
    ThreadPool::submitIO([provider, guard, seq, type = type_] {
        auto rows = fetchRows(provider, type);
        QMetaObject::invokeMethod(guard, [guard, seq, rows = std::move(rows)]() mutable {
            guard->onRowsReady(seq, std::move(rows));
        }, Qt::QueuedConnection);
    });
}

void SectorListPage::onRowsReady(int seq, std::vector<SectorRow> rows) {
    fetching_ = false;
    if (seq <= lastSeq_) return;  // 陈旧丢弃
    lastSeq_ = seq;
    cache_ = std::move(rows);
    applyRows(cache_);
}

std::vector<SectorRow> SectorListPage::fetchRows(IDataProvider* provider, SectorType type) {
    std::vector<SectorRow> rows;
    if (!provider) return rows;
    // 通达信板块指数全量（与叠加对话框同源同过滤：行业 8803xx-8804xx，概念 8805xx+）
    auto isType = [type](const std::string& c) {
        if (c.size() < 6) return false;
        if (type == SectorType::Industry) {
            return c.compare(0, 3, "880") == 0 && c >= "880300" && c < "880500";
        }
        return c.compare(0, 3, "880") == 0 && c >= "880500";
    };
    const auto sectors = provider->getSectorIndices();
    std::vector<StockCode> codes;
    std::vector<QString> names;
    codes.reserve(sectors.size());
    names.reserve(sectors.size());
    for (const auto& s : sectors) {
        if (!isType(s.code.code())) continue;
        codes.push_back(s.code);
        names.push_back(QString::fromUtf8(s.name.c_str()));
    }
    auto quotes = provider->batchQuoteInteractive(codes);
    std::unordered_map<std::string, const Quote*> qmap;
    qmap.reserve(quotes.size());
    for (const auto& q : quotes) qmap[q.code.displayCode()] = &q;

    rows.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        SectorRow r;
        r.code = codes[i];      // 板块指数代码（双击开图用）
        r.name = names[i];
        const auto it = qmap.find(codes[i].displayCode());
        if (it != qmap.end() && it->second->lastPrice > 0) {
            r.changePct = it->second->change;
            r.amount = it->second->amount;
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

void SectorListPage::applyRows(std::vector<SectorRow> rows) {
    const bool hasData = !rows.empty();  // 先判空（下面会 move 走）
    // 涨跌幅降序（全量展示，可滚动）
    std::sort(rows.begin(), rows.end(),
              [](const SectorRow& a, const SectorRow& b) {
                  return a.changePct > b.changePct;
              });
    if (model_) model_->setRows(std::move(rows));  // 模型一次 reset，虚拟化渲染

    stack_->setCurrentWidget(hasData
        ? static_cast<QWidget*>(table_)
        : static_cast<QWidget*>(emptyLabel_));
    const auto now = QTime::currentTime().toString("HH:mm:ss");
    updateLabel_->setText(hasData ? tr("更新 %1").arg(now)
                                  : tr("获取失败 %1").arg(now));
}

} // namespace st

#include "moc_sector_panel.cpp"
