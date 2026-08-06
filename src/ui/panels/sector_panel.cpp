#include "ui/panels/sector_panel.h"
#include "core/thread_pool.h"
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPointer>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTime>
#include <cmath>
#include <algorithm>
#include <utility>

namespace st {

namespace {

constexpr char kAxisColor[] = "#6b6b6b";
constexpr char kUpColor[] = "#e54648";    // 红涨
constexpr char kDownColor[] = "#2e9e5b";  // 绿跌

/// 按比例插值两色
QColor lerpColor(const QColor& a, const QColor& b, double t) {
    return QColor(
        static_cast<int>(a.red() + (b.red() - a.red()) * t),
        static_cast<int>(a.green() + (b.green() - a.green()) * t),
        static_cast<int>(a.blue() + (b.blue() - a.blue()) * t));
}

/// 成交额（元）→ 亿/万
QString amountText(double yuan) {
    if (yuan >= 1e8) return QStringLiteral("%1亿").arg(yuan / 1e8, 0, 'f', 1);
    if (yuan >= 1e4) return QStringLiteral("%1万").arg(yuan / 1e4, 0, 'f', 1);
    return QStringLiteral("%1").arg(yuan, 0, 'f', 0);
}

}  // namespace

// ============================================================
// SectorHeatmap — Squarified Treemap 热力图自绘
// ============================================================
class SectorPanel::SectorHeatmap : public QWidget {
public:
    explicit SectorHeatmap(SectorPanel* host, QWidget* parent = nullptr)
        : QWidget(parent), host_(host) {
        setMouseTracking(true);
    }

    void setBoards(std::vector<SectorBoard> boards) {
        boards_ = std::move(boards);
        hoverIndex_ = -1;
        layoutRects();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#18181a"));
        if (boards_.empty()) {
            p.setPen(QColor("#666666"));
            p.drawText(rect(), Qt::AlignCenter, tr("暂无板块数据"));
            return;
        }

        double maxAbs = 0.0;
        for (const auto& b : boards_) maxAbs = std::max(maxAbs, std::abs(b.changePct));
        const double scaleMax = std::max(maxAbs, 0.5);  // 至少 0.5% 才有色

        for (size_t i = 0; i < boards_.size() && i < rects_.size(); ++i) {
            const auto& t = rects_[i];
            const QRectF r(t.x, t.y, t.w, t.h);
            if (r.width() < 2 || r.height() < 2) continue;
            p.fillRect(r.adjusted(1, 1, -1, -1),
                       colorFor(boards_[i].changePct, scaleMax));

            const auto& b = boards_[i];
            const QString pct = QStringLiteral("%1%").arg(b.changePct, 0, 'f', 2);
            p.setPen(QColor("#ffffff"));
            if (r.width() >= 46 && r.height() >= 26) {
                QFont f = p.font();
                f.setPointSizeF(std::clamp(r.height() * 0.16, 7.0, 11.0));
                p.setFont(f);
                p.drawText(r.adjusted(4, 3, -4, -2), Qt::AlignHCenter | Qt::AlignVCenter,
                           QString::fromUtf8(b.name.c_str()) + " " + pct);
            } else if (r.width() >= 24 && r.height() >= 14) {
                p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 7));
                p.drawText(r, Qt::AlignCenter, pct);
            }
        }

        // 悬停高亮边框
        if (hoverIndex_ >= 0 && hoverIndex_ < static_cast<int>(rects_.size())) {
            const auto& t = rects_[static_cast<size_t>(hoverIndex_)];
            p.setPen(QPen(QColor("#ffffff"), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(t.x, t.y, t.w, t.h).adjusted(1, 1, -1, -1));
        }
    }

    void resizeEvent(QResizeEvent*) override {
        layoutRects();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const int idx = indexAt(event->pos());
        if (idx != hoverIndex_) {
            hoverIndex_ = idx;
            update();
        }
        if (host_) host_->onTileHover(idx);
    }

    void leaveEvent(QEvent*) override {
        if (hoverIndex_ != -1) {
            hoverIndex_ = -1;
            update();
        }
        if (host_) host_->onTileHover(-1);
    }

private:
    QColor colorFor(double changePct, double scaleMax) const {
        if (std::abs(changePct) < 0.05) return QColor("#3a3a3c");  // 平盘灰
        const double t = std::clamp(std::abs(changePct) / scaleMax, 0.0, 1.0);
        if (changePct > 0.0) {
            return lerpColor(QColor(0x7a, 0x2b, 0x2b), QColor(kUpColor), t);
        }
        return lerpColor(QColor(0x2e, 0x5b, 0x3a), QColor(kDownColor), t);
    }

    void layoutRects() {
        rects_.clear();
        if (boards_.empty() || width() <= 0 || height() <= 0) return;
        std::vector<double> weights;
        weights.reserve(boards_.size());
        double sum = 0.0;
        for (const auto& b : boards_) {
            weights.push_back(b.amount > 0.0 ? b.amount : 0.0);
            sum += b.amount;
        }
        if (sum <= 0.0) {  // 无成交额 → 等面积
            for (auto& w : weights) w = 1.0;
        }
        rects_ = Treemap::layout(static_cast<double>(width()),
                                 static_cast<double>(height()), weights);
    }

    int indexAt(const QPoint& pos) const {
        for (size_t i = 0; i < rects_.size(); ++i) {
            const auto& t = rects_[i];
            if (pos.x() >= t.x && pos.x() < t.x + t.w &&
                pos.y() >= t.y && pos.y() < t.y + t.h) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    SectorPanel* host_ = nullptr;
    std::vector<SectorBoard> boards_;
    std::vector<TreemapRect> rects_;
    int hoverIndex_ = -1;
};

// ============================================================
// SectorPanel
// ============================================================
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

    // 热力图
    heatmap_ = new SectorHeatmap(this);
    heatmap_->setMinimumHeight(200);
    layout->addWidget(heatmap_, 1);

    // 底部详情
    detailLabel_ = new QLabel(tr("悬停查看板块详情"));
    detailLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    layout->addWidget(detailLabel_);

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
    boards_ = std::move(boards);
    heatmap_->setBoards(boards_);
    const auto now = QTime::currentTime().toString("HH:mm:ss");
    if (boards_.empty()) {
        // 数据源限流/暂不可用 → 明确提示，30s 定时器会自动重试
        updateLabel_->setText(tr("获取失败 %1").arg(now));
        detailLabel_->setText(tr("板块数据源暂不可用，将自动重试"));
    } else {
        updateLabel_->setText(tr("更新 %1").arg(now));
        detailLabel_->setText(tr("悬停查看板块详情"));
    }
}

void SectorPanel::onTileHover(int index) {
    if (index < 0 || index >= static_cast<int>(boards_.size())) {
        detailLabel_->setText(tr("悬停查看板块详情"));
        return;
    }
    const auto& b = boards_[static_cast<size_t>(index)];
    const QString color = b.changePct >= 0.0 ? kUpColor : kDownColor;
    detailLabel_->setText(QStringLiteral(
        "%1 %2%  |  领涨 %3 %4%  |  涨%5 跌%6 平%7  |  成交 %8")
        .arg(QString::fromUtf8(b.name.c_str()))
        .arg(b.changePct, 0, 'f', 2)
        .arg(QString::fromUtf8(b.leadingStock.c_str()))
        .arg(b.leadingChangePct, 0, 'f', 2)
        .arg(b.upCount).arg(b.downCount).arg(b.flatCount)
        .arg(amountText(b.amount)));
    detailLabel_->setStyleSheet(color.isEmpty()
        ? QString() : QStringLiteral("color:%1;").arg(color));
}

} // namespace st

#include "moc_sector_panel.cpp"
