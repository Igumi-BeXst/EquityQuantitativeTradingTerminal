#include "ui/widgets/chip_panel.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPointer>
#include <QPainter>
#include <QPaintEvent>
#include <cmath>
#include <algorithm>
#include <utility>

namespace st {

namespace {
constexpr char kUpColor[] = "#e54648";    // 红涨
constexpr char kDownColor[] = "#2e9e5b";  // 绿跌
constexpr char kAxisColor[] = "#6b6b6b";
constexpr char kTextColor[] = "#c8c8c8";
}  // namespace

// ============================================================
// ChipChartArea — 自绘筹码云（价格轴 Y + 横条 X 筹码量）
// ============================================================
class ChipPanel::ChipChartArea : public QWidget {
public:
    explicit ChipChartArea(QWidget* parent = nullptr) : QWidget(parent) {}

    void setData(const ChipDistResult& chip, double lastClose) {
        chip_ = chip;
        lastClose_ = lastClose;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#18181a"));
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool chipOk = chip_.success && !chip_.points.empty();
        if (!chipOk) {
            p.setPen(QColor("#666666"));
            p.drawText(rect(), Qt::AlignCenter, tr("无数据"));
            return;
        }

        // 价格范围 + 内边距（现价纳入可见范围）
        double minP = chip_.minPrice;
        double maxP = chip_.maxPrice;
        if (maxP <= minP) { minP = maxP - 1.0; }
        const double pad = (maxP - minP) * 0.05;
        minP -= pad;
        maxP += pad;
        if (lastClose_ > 0.0) {
            minP = std::min(minP, lastClose_);
            maxP = std::max(maxP, lastClose_);
        }
        const double span = std::max(1e-9, maxP - minP);

        const int axisW = 48;               // 左侧价格轴
        const int topPad = 16;              // 标题条
        const int bottomPad = 8;
        const QRect chartRect(axisW, topPad, width() - axisW, height() - topPad - bottomPad);
        const auto priceToY = [&](double price) {
            return static_cast<int>(chartRect.bottom() - (price - minP) / span * chartRect.height());
        };

        // 价格轴刻度（5 档）
        p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 8));
        p.setPen(QColor(kAxisColor));
        for (int i = 0; i <= 4; ++i) {
            const double price = minP + span * i / 4.0;
            const int y = priceToY(price);
            p.drawLine(axisW - 3, y, axisW, y);
            p.drawText(0, y - 6, axisW - 6, 12, Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(price, 'f', 2));
        }

        // 标题
        p.setPen(QColor("#888888"));
        p.drawText(chartRect.left(), 2, chartRect.width(), 12,
                   Qt::AlignHCenter, tr("筹码分布"));

        // 筹码云：每价位桶从左侧价格轴向右画横条，宽∝筹码量
        // 低于现价红（获利盘）/ 高于现价绿（套牢盘）
        double maxShares = 0.0;
        for (const auto& pt : chip_.points) maxShares = std::max(maxShares, pt.shares);
        const int maxBarW = chartRect.width() - 4;
        const int barH = std::max(1, chartRect.height() / static_cast<int>(chip_.points.size()));
        p.setPen(Qt::NoPen);
        for (const auto& pt : chip_.points) {
            const int y = priceToY(pt.price);
            const int bw = static_cast<int>(pt.shares / maxShares * maxBarW);
            if (bw <= 0) continue;
            const bool profit = lastClose_ <= 0.0 || pt.price <= lastClose_;
            p.setBrush(QColor(profit ? kUpColor : kDownColor));
            p.drawRect(chartRect.left(), y - barH / 2, bw, barH);
        }

        // 现价虚线 + 标价
        if (lastClose_ > 0.0) {
            const int y = priceToY(lastClose_);
            p.setPen(QPen(QColor("#d0d0d0"), 1, Qt::DashLine));
            p.drawLine(chartRect.left(), y, chartRect.right(), y);
            p.setPen(QColor(kTextColor));
            p.drawText(chartRect.right() - 62, y - 12, 62, 12,
                       Qt::AlignRight, QString::number(lastClose_, 'f', 2));
        }
    }

private:
    ChipDistResult chip_;
    double lastClose_ = 0.0;
};

// ============================================================
// ChipPanel
// ============================================================
ChipPanel::ChipPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      fundProvider_(std::make_shared<AKShareProvider>()) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 顶行：标题 + 现价 + 刷新
    auto* topRow = new QHBoxLayout();
    titleLabel_ = new QLabel(tr("--"));
    titleLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    priceLabel_ = new QLabel(tr("--"));
    priceLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    refreshBtn_ = new QPushButton(tr("刷新"));
    refreshBtn_->setFixedWidth(44);
    topRow->addWidget(titleLabel_);
    topRow->addStretch();
    topRow->addWidget(priceLabel_);
    topRow->addWidget(refreshBtn_);
    layout->addLayout(topRow);
    connect(refreshBtn_, &QPushButton::clicked, this, &ChipPanel::onRefresh);

    // 筹码截至日期行（默认最新一天；K线十字光标悬停按日期查询）
    auto* dateRow = new QHBoxLayout();
    dateLabel_ = new QLabel(tr("筹码截至 --"));
    dateLabel_->setStyleSheet(QStringLiteral("color:#888888;"));
    dateRow->addWidget(dateLabel_);
    dateRow->addStretch();
    auto* hint = new QLabel(tr("K线图上移动鼠标按日期查询"));
    hint->setStyleSheet(QStringLiteral("color:#555555;"));
    dateRow->addWidget(hint);
    layout->addLayout(dateRow);

    // 自绘区
    chart_ = new ChipChartArea(this);
    chart_->setMinimumHeight(200);
    layout->addWidget(chart_, 1);

    // 统计网格（3 行 × 2 列，中间 16px 间隔）
    static const char* kStatLabels[] = {
        "平均成本", "获利盘", "集中度", "90%成本区间", "70%成本区间",
    };
    constexpr int kStatCount = 5;
    auto* grid = new QGridLayout();
    grid->setContentsMargins(2, 2, 2, 2);
    grid->setSpacing(2);
    statLabels_.resize(kStatCount);
    for (int i = 0; i < kStatCount; ++i) {
        auto* label = new QLabel(QString::fromUtf8(kStatLabels[i]));
        label->setStyleSheet(QStringLiteral("color:#888888;"));
        auto* value = new QLabel(tr("—"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const int col = (i % 2) * 3;
        grid->addWidget(label, i / 2, col);
        grid->addWidget(value, i / 2, col + 1);
        statLabels_[static_cast<size_t>(i)] = value;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);
    grid->setColumnMinimumWidth(2, 16);
    layout->addLayout(grid);
}

void ChipPanel::setStock(const StockCode& code, const QString& name) {
    code_ = code;
    name_ = name;
    bars_.clear();
    chip_ = ChipDistResult{};
    lastClose_ = 0.0;
    floatShares_ = 0.0;
    asOfDate_.reset();  // 默认最新一天
    pending_ = false;
    ++gen_;        // 丢弃在途任务
    busy_ = false; // 允许新拉取启动（旧任务结果由 gen_ 丢弃）
    titleLabel_->setText(name_.isEmpty()
        ? QString::fromStdString(code.displayCode())
        : name_ + " " + QString::fromStdString(code.displayCode()));
    priceLabel_->setText(tr("加载中…"));
    dateLabel_->setText(tr("筹码截至 --"));
    resetStats();
    chart_->setData(chip_, lastClose_);
    if (!code_.code().empty()) requestData();
}

void ChipPanel::onRefresh() {
    if (!code_.code().empty()) {
        busy_ = false;  // 允许重拉（在途旧任务由 gen_ 丢弃）
        requestData();
    }
}

void ChipPanel::setAsOfDate(const std::optional<DateTime>& date) {
    if (bars_.empty()) return;  // 数据未加载，忽略（加载完成后自然显示最新）
    asOfDate_ = date;
    if (busy_) {
        pending_ = true;  // 计算在飞 → 完成后补算最新日期
        return;
    }
    computeAndApply();
}

void ChipPanel::requestData() {
    if (busy_ || code_.code().empty()) return;
    busy_ = true;
    const int gen = ++gen_;
    const StockCode c = code_;
    const auto end = utils::now();
    const auto start = utils::parseDate("2005-01-01");  // 覆盖周/月线更早日期
    // 安全异步：provider 裸指针 + fundProvider shared_ptr 均按值捕获，QPointer 守卫回主线程
    IDataProvider* provider = provider_;
    auto fundProvider = fundProvider_;
    QPointer<ChipPanel> guard(this);
    ThreadPool::submitIO([provider, fundProvider, guard, gen, c, start, end] {
        auto bars = provider->getBars(c, BarPeriod::Daily, start, end);
        auto fund = fundProvider->getQuoteFundamentals(c);
        const double floatShares = (fund && fund->valid && fund->floatShares > 0.0)
            ? fund->floatShares : 0.0;
        QMetaObject::invokeMethod(guard,
            [guard, gen, bars = std::move(bars), floatShares,
             fundProvider = std::move(fundProvider)]() mutable {
                guard->busy_ = false;
                if (gen != guard->gen_) return;
                guard->floatShares_ = floatShares;
                guard->onDataLoaded(std::move(bars));
            }, Qt::QueuedConnection);
    });
}

void ChipPanel::onDataLoaded(std::vector<Bar> bars) {
    bars_ = std::move(bars);
    computeAndApply();
}

void ChipPanel::computeAndApply() {
    if (busy_) return;
    busy_ = true;
    pending_ = false;
    const int gen = gen_;
    // 安全异步：捕获数据副本（按值）+ QPointer 守卫
    std::vector<Bar> bars = bars_;
    const double floatShares = floatShares_;
    const std::optional<DateTime> asOf = asOfDate_;
    QPointer<ChipPanel> guard(this);
    ThreadPool::submitWorker([guard, gen, bars = std::move(bars),
                              floatShares, asOf]() mutable {
        // as-of 索引：所有时间 <= 目标日期的日K（nullopt = 最新一根）
        int idx = static_cast<int>(bars.size()) - 1;
        if (asOf) {
            idx = -1;
            for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
                if (bars[static_cast<size_t>(i)].time <= *asOf) {
                    idx = i;
                } else {
                    break;
                }
            }
            if (idx < 0) idx = 0;  // 查询早于最早数据 → 取最早一根
        }
        std::vector<Bar> win(bars.begin(), bars.begin() + idx + 1);
        auto chip = ChipDistribution::compute(win, floatShares);
        const DateTime asOfDate = win.empty() ? DateTime{} : win.back().time;
        const double lastClose = win.empty() ? 0.0 : win.back().close;
        QMetaObject::invokeMethod(guard,
            [guard, gen, chip = std::move(chip), asOfDate, lastClose]() mutable {
                guard->busy_ = false;
                if (gen != guard->gen_) return;
                guard->chip_ = std::move(chip);
                guard->lastClose_ = lastClose;
                guard->applyResult(asOfDate);
                if (guard->pending_) guard->computeAndApply();  // 期间有新日期请求 → 补算
            }, Qt::QueuedConnection);
    });
}

void ChipPanel::applyResult(const DateTime& asOfDate) {
    if (lastClose_ > 0.0) {
        priceLabel_->setText(QStringLiteral("现价 %1").arg(lastClose_, 0, 'f', 2));
    } else {
        priceLabel_->setText(tr("无行情"));
    }
    dateLabel_->setText(asOfDate == DateTime{}
        ? tr("筹码截至 --")
        : tr("筹码截至 %1").arg(QString::fromStdString(utils::toDateString(asOfDate))));

    const auto num = [](double v) -> QString {
        return v > 0.0 ? QString::number(v, 'f', 2) : QStringLiteral("—");
    };
    const auto pct = [](double v, int prec) -> QString {
        return QStringLiteral("%1%").arg(v, 0, 'f', prec);
    };
    auto setStat = [this](int idx, const QString& text, const QString& color = QString()) {
        statLabels_[static_cast<size_t>(idx)]->setText(text);
        statLabels_[static_cast<size_t>(idx)]->setStyleSheet(
            color.isEmpty() ? QString() : QStringLiteral("color:%1;").arg(color));
    };
    const auto costRange = [](double lo, double hi) -> QString {
        if (lo <= 0.0 || hi <= 0.0) return QStringLiteral("—");
        return QStringLiteral("%1 ~ %2").arg(lo, 0, 'f', 2).arg(hi, 0, 'f', 2);
    };

    const bool chipOk = chip_.success;
    setStat(0, chipOk ? num(chip_.avgCost) : tr("—"));
    setStat(1, chipOk ? pct(chip_.profitRatio * 100.0, 1) : tr("—"));
    setStat(2, chipOk ? pct(chip_.concentration * 100.0, 1) : tr("—"));
    setStat(3, chipOk ? costRange(chip_.costLow, chip_.costHigh) : tr("—"));
    setStat(4, chipOk ? costRange(chip_.costLow70, chip_.costHigh70) : tr("—"));

    chart_->setData(chip_, lastClose_);
}

void ChipPanel::resetStats() {
    for (auto* v : statLabels_) {
        if (v) {
            v->setText(tr("—"));
            v->setStyleSheet(QString());
        }
    }
}

} // namespace st

#include "moc_chip_panel.cpp"
