#include "ui/panels/sector_panel.h"
#include "core/thread_pool.h"
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
#include <utility>

namespace st {

namespace {

constexpr char kUpColor[] = "#e54648";    // 红涨
constexpr char kDownColor[] = "#2e9e5b";  // 绿跌

/// 成交额（元）→ 亿/万
QString amountText(double yuan) {
    if (yuan >= 1e8) return QStringLiteral("%1亿").arg(yuan / 1e8, 0, 'f', 1);
    if (yuan >= 1e4) return QStringLiteral("%1万").arg(yuan / 1e4, 0, 'f', 1);
    return QStringLiteral("%1").arg(yuan, 0, 'f', 0);
}

}  // namespace

SectorPanel::SectorPanel(QWidget* parent)
    : QWidget(parent), provider_(std::make_shared<EastMoneySectorProvider>()) {
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
    topRow->addStretch();
    updateLabel_ = new QLabel(tr("--"));
    updateLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    topRow->addWidget(updateLabel_);
    layout->addLayout(topRow);
    connect(refreshBtn_, &QPushButton::clicked, this, &SectorPanel::onRefresh);

    // 前十榜单（列表，替代全量 treemap 热力图）
    stack_ = new QStackedWidget(this);
    table_ = new QTableWidget(0, 4, stack_);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("板块"), QStringLiteral("涨跌幅"),
         QStringLiteral("领涨股"), QStringLiteral("成交额")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);
    table_->setStyleSheet(QStringLiteral(
        "QTableWidget{background:#18181a;border:none;}"
        "QTableWidget::item{padding:2px 6px;border-bottom:1px solid #242426;}"
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
    ++gen_;  // 丢弃在途旧类型响应
    busy_ = false;
    onRefresh();
}

void SectorPanel::refresh() { onRefresh(); }

void SectorPanel::onRefresh() {
    if (busy_ || !provider_) return;
    busy_ = true;
    const int gen = ++gen_;
    const SectorType type = type_;
    // 安全异步：shared_ptr 按值捕获 + QPointer 守卫回主线程
    auto provider = provider_;
    QPointer<SectorPanel> guard(this);
    ThreadPool::submitIO([provider, guard, gen, type] {
        auto boards = provider->fetchBoards(type);
        QMetaObject::invokeMethod(guard,
            [guard, gen, boards = std::move(boards)]() mutable {
                guard->busy_ = false;
                if (gen != guard->gen_) return;
                guard->applyBoards(std::move(boards));
            }, Qt::QueuedConnection);
    });
}

void SectorPanel::applyBoards(std::vector<SectorBoard> boards) {
    // 涨跌幅降序 → 仅展示前 10
    std::sort(boards.begin(), boards.end(),
              [](const SectorBoard& a, const SectorBoard& b) {
                  return a.changePct > b.changePct;
              });
    boards.resize(std::min<size_t>(boards.size(), 10));

    table_->setRowCount(static_cast<int>(boards.size()));
    for (size_t i = 0; i < boards.size(); ++i) {
        const auto& b = boards[i];
        const QString color = b.changePct >= 0.0
            ? QString::fromUtf8(kUpColor) : QString::fromUtf8(kDownColor);

        auto* nameItem = new QTableWidgetItem(QString::fromUtf8(b.name.c_str()));
        nameItem->setForeground(QColor("#dddddd"));

        auto* pctItem = new QTableWidgetItem(
            QStringLiteral("%1%").arg(b.changePct, 0, 'f', 2));
        pctItem->setForeground(QColor(color));

        QString lead = QString::fromUtf8(b.leadingStock.c_str());
        if (!b.leadingStock.empty() && b.leadingChangePct != 0.0) {
            lead += QStringLiteral(" %1%").arg(b.leadingChangePct, 0, 'f', 2);
        }
        auto* leadItem = new QTableWidgetItem(lead);
        leadItem->setForeground(QColor("#999999"));

        auto* amountItem = new QTableWidgetItem(amountText(b.amount));
        amountItem->setForeground(QColor("#999999"));

        table_->setItem(static_cast<int>(i), 0, nameItem);
        table_->setItem(static_cast<int>(i), 1, pctItem);
        table_->setItem(static_cast<int>(i), 2, leadItem);
        table_->setItem(static_cast<int>(i), 3, amountItem);
    }

    stack_->setCurrentWidget(boards.empty()
        ? static_cast<QWidget*>(emptyLabel_)
        : static_cast<QWidget*>(table_));
    const auto now = QTime::currentTime().toString("HH:mm:ss");
    updateLabel_->setText(boards.empty() ? tr("获取失败 %1").arg(now)
                                         : tr("更新 %1").arg(now));
}

} // namespace st

#include "moc_sector_panel.cpp"
