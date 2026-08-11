#include "ui/panels/sector_panel.h"
#include "ui/utils/table_csv_export.h"
#include "core/thread_pool.h"
#include "foundation/tick.h"
#include <QButtonGroup>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace st {

SectorPanel::SectorPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 顶行：行业/概念 tab + 刷新 + 更新时间
    auto* topRow = new QHBoxLayout();
    typeGroup_ = new QButtonGroup(this);
    typeGroup_->setExclusive(true);
    struct T { const char* label; SectorType type; };
    const T tabs[] = {
        {"行业板块", SectorType::Industry},
        {"概念板块", SectorType::Concept},
    };
    for (const auto& tab : tabs) {
        auto* btn = new QPushButton(QString::fromUtf8(tab.label));
        btn->setCheckable(true);
        btn->setFixedWidth(64);
        typeGroup_->addButton(btn, static_cast<int>(tab.type));
        topRow->addWidget(btn);
    }
    typeGroup_->button(static_cast<int>(SectorType::Industry))->setChecked(true);
    connect(typeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        setType(static_cast<SectorType>(id));
    });
    refreshBtn_ = new QPushButton(tr("刷新"));
    refreshBtn_->setFixedWidth(44);
    topRow->addWidget(refreshBtn_);
    auto* exportBtn = new QPushButton(tr("导出"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        st::ui::exportViewToCsv(table_, this, "sector_quotes.csv");
    });
    topRow->addWidget(exportBtn);
    topRow->addStretch();
    updateLabel_ = new QLabel(tr("--"));
    updateLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    topRow->addWidget(updateLabel_);
    layout->addLayout(topRow);
    connect(refreshBtn_, &QPushButton::clicked, this, &SectorPanel::onRefresh);

    // 板块榜单（TDX 全量源；QTableView + 模型虚拟化渲染，大表滚动只画可见行，避免卡死）
    stack_ = new QStackedWidget(this);
    model_ = new SectorListModel(this);
    table_ = new QTableView(stack_);
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setStyleSheet(QStringLiteral(
        "QTableView{background:#18181a;border:none;}"
        "QHeaderView::section{background:#222225;color:#bbbbbb;border:none;padding:4px;}"));
    stack_->addWidget(table_);
    emptyLabel_ = new QLabel(tr("暂无板块数据"), stack_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("color:#666666;"));
    stack_->addWidget(emptyLabel_);
    layout->addWidget(stack_, 1);

    // 30s 自动刷新
    timer_ = new QTimer(this);
    timer_->setInterval(30000);
    connect(timer_, &QTimer::timeout, this, &SectorPanel::onRefresh);

    onRefresh();  // 立即刷一次
}

void SectorPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (timer_ && !timer_->isActive()) {
        timer_->start();
        onRefresh();
    }
}

void SectorPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (timer_) timer_->stop();
}

void SectorPanel::setType(SectorType type) {
    if (type_ == type) return;
    type_ = type;
    // 有缓存先立即显示（免卡顿），再后台刷新保持新鲜
    const auto it = cache_.find(type);
    if (it != cache_.end()) applyRows(it->second);
    fetchType(type);
}

void SectorPanel::refresh() { onRefresh(); }

void SectorPanel::onRefresh() { fetchType(type_); }

void SectorPanel::fetchType(SectorType type) {
    if (!provider_ || fetching_[type]) return;  // 同类型在途则跳过（防重叠）
    fetching_[type] = true;
    const int seq = ++fetchSeq_;
    IDataProvider* provider = provider_;
    QPointer<SectorPanel> guard(this);
    ThreadPool::submitIO([provider, guard, seq, type] {
        auto rows = fetchRows(provider, type);
        QMetaObject::invokeMethod(guard, [guard, seq, type, rows = std::move(rows)]() mutable {
            guard->onRowsReady(type, seq, std::move(rows));
        }, Qt::QueuedConnection);
    });
}

void SectorPanel::onRowsReady(SectorType type, int seq, std::vector<SectorRow> rows) {
    fetching_[type] = false;
    if (seq <= lastSeq_[type]) return;  // 陈旧丢弃（切换/刷新竞态）
    lastSeq_[type] = seq;
    cache_[type] = std::move(rows);
    if (type == type_) applyRows(cache_[type]);  // 当前类型才显示
    // 不预取另一种类型：概念 438 个批量报价很重，后台预取会长时间占用 TDX 连接，
    // 与市场面板刷新叠加导致应用卡死。首次切到某类型时现场拉取（异步，UI 不阻塞）。
}

std::vector<SectorRow> SectorPanel::fetchRows(IDataProvider* provider,
                                                           SectorType type) {
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

void SectorPanel::applyRows(std::vector<SectorRow> rows) {
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
