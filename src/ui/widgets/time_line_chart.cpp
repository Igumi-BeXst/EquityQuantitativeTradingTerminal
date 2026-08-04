#include "ui/widgets/time_line_chart.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/indicators.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace st {

namespace {
const QColor kUpColor("#e54648");
const QColor kDownColor("#2e9e5b");

constexpr double kRightAxisW = 64;
constexpr double kBottomAxisH = 26;
constexpr double kTitleH = 22;
constexpr double kMainRatio = 0.58;   // 主价格区占可绘图区
constexpr double kVolRatio = 0.16;    // 量区
}  // namespace

TimelineChart::TimelineChart(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    setMouseTracking(true);
    setMinimumSize(400, 300);
}

void TimelineChart::loadStock(const StockCode& code, const QString& name) {
    const int gen = ++loadGen_;
    code_ = code;
    name_ = name;
    loading_ = true;
    update();
    if (!provider_) { loading_ = false; return; }

    ThreadPool::submitIO([this, gen, code] {
        auto data = provider_->getIntraday(code);
        QMetaObject::invokeMethod(this, [this, gen, data = std::move(data)]() mutable {
            if (gen != loadGen_) return;
            setData(data ? *data : IntradayData{});
        }, Qt::QueuedConnection);
    });
}

void TimelineChart::setData(IntradayData newData) {
    data_ = std::move(newData);
    loading_ = false;
    mouseIndex_ = -1;
    computeAvgLine();
    computeMacd();
    computeRanges();
    update();
}

int TimelineChart::minutesFromOpen(const DateTime& t) const {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    const int mins = tm.tm_hour * 60 + tm.tm_min;
    if (mins < 13 * 60) return mins - (9 * 60 + 30);  // 上午 09:30=0
    return 120 + mins - 13 * 60;                       // 下午 13:00=120
}

double TimelineChart::xFor(int minutes) const {
    return minutes / 240.0 * (width() - kRightAxisW);
}

double TimelineChart::priceToY(double price) const {
    const double h = mainRect_.height();
    if (priceHi_ - priceLo_ < 1e-12) return mainRect_.top();
    return mainRect_.top() + (priceHi_ - price) / (priceHi_ - priceLo_) * h;
}

double TimelineChart::volToY(double v) const {
    return volRect_.bottom() - v / volHi_ * volRect_.height();
}

double TimelineChart::macdToY(double v) const {
    double mid = macdRect_.center().y();
    return mid - v / macdMaxAbs_ * (macdRect_.height() / 2.0);
}

void TimelineChart::computeAvgLine() {
    avgLine_.clear();
    // 腾讯分时数据是累计值 → 均价 = 累计额 / 累计量（正确 VWAP）
    for (const auto& pt : data_.points) {
        if (pt.volume > 0) avgLine_.push_back(pt.amount / pt.volume);
        else avgLine_.push_back(avgLine_.empty() ? 0.0 : avgLine_.back());
    }
}

void TimelineChart::computeMacd() {
    macdDif_.clear();
    macdDea_.clear();
    macdHist_.clear();
    if (data_.points.size() < 30) return;

    std::vector<double> prices;
    prices.reserve(data_.points.size());
    for (const auto& pt : data_.points) prices.push_back(pt.price);

    auto m = st::indicators::macd(prices);
    macdDif_ = std::move(m.dif);
    macdDea_ = std::move(m.dea);
    macdHist_ = std::move(m.hist);
}

void TimelineChart::computeRanges() {
    if (data_.points.empty()) return;

    // 主图价格范围
    double hi = data_.preClose, lo = data_.preClose;
    for (const auto& pt : data_.points) {
        hi = std::max(hi, pt.price);
        lo = std::min(lo, pt.price);
    }
    const double pad = (hi - lo) * 0.05 + 0.01;
    priceHi_ = hi + pad;
    priceLo_ = lo - pad;

    // 量程: 每分钟增量（腾讯分时量为累计值）
    volHi_ = 0;
    for (size_t i = 0; i < data_.points.size(); ++i) {
        const double cum = static_cast<double>(data_.points[i].volume);
        const double prev = i > 0 ? static_cast<double>(data_.points[i - 1].volume) : 0.0;
        volHi_ = std::max(volHi_, cum - prev);
    }
    if (volHi_ <= 0) volHi_ = 1;

    // MACD 量程
    macdMaxAbs_ = 0;
    for (size_t i = 0; i < macdDif_.size(); ++i) {
        for (double v : {macdDif_[i], macdDea_[i], macdHist_[i]}) {
            if (std::isfinite(v)) macdMaxAbs_ = std::max(macdMaxAbs_, std::abs(v));
        }
    }
    if (macdMaxAbs_ <= 0) macdMaxAbs_ = 1;
}

// ============================================================
// 绘制
// ============================================================
void TimelineChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));

    if (data_.points.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter, loading_ ? tr("加载中…") : tr("无数据"));
        return;
    }

    // 布局: 主图 / 量 / 分时MACD
    const double plotW = width() - kRightAxisW;
    const double plotH = height() - kTitleH - kBottomAxisH;
    const QRectF plot(0, kTitleH, plotW, plotH);
    mainRect_ = QRectF(plot.left(), plot.top(), plot.width(), plotH * kMainRatio);
    volRect_ = QRectF(plot.left(), plot.top() + mainRect_.height() + 8,
                      plot.width(), plotH * kVolRatio);
    macdRect_ = QRectF(plot.left(), volRect_.bottom() + 8,
                       plot.width(), plot.bottom() - volRect_.bottom() - 8);

    drawTitle(p);
    drawGridAndAxis(p);
    drawPriceLines(p);
    drawVolume(p);
    drawMacd(p);
    drawCrosshair(p);
}

void TimelineChart::drawTitle(QPainter& p) {
    p.setPen(QColor("#d4d4d4"));
    QFont f = p.font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);

    const auto& last = data_.points.back();
    const double change = data_.preClose > 0
        ? (last.price - data_.preClose) / data_.preClose * 100.0 : 0.0;
    QColor c = change >= 0 ? kUpColor : kDownColor;

    QString text = name_ + "  " + QString::fromStdString(code_.displayCode())
        + tr("  [分时]  ") + QString::number(last.price, 'f', 2);
    p.setPen(c);
    text += QStringLiteral("  %1%").arg(change, 0, 'f', 2);
    p.drawText(QRectF(4, 2, width() - 8, kTitleH - 2),
               Qt::AlignLeft | Qt::AlignVCenter, text);
}

void TimelineChart::drawGridAndAxis(QPainter& p) {
    p.setPen(QColor("#888888"));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);

    const double w = width() - kRightAxisW;

    // 竖分隔: 10:30 / 11:30(午休虚线) / 14:00
    for (int m : {60, 120, 180}) {
        double x = xFor(m);
        p.setPen(QPen(QColor(255, 255, 255, m == 120 ? 50 : 25), 1,
                      m == 120 ? Qt::DashLine : Qt::SolidLine));
        p.drawLine(QPointF(x, kTitleH), QPointF(x, height() - kBottomAxisH));
    }

    // X 轴标签
    p.setPen(QColor("#888888"));
    const double axisY = height() - 20;
    const QPointF labels[] = {{0.0, axisY}, {xFor(60), axisY},
                              {xFor(120) - 20, axisY}, {xFor(180), axisY},
                              {w - 40, axisY}};
    const char* texts[] = {"09:30", "10:30", "11:30/13:00", "14:00", "15:00"};
    for (int i = 0; i < 5; ++i) {
        p.drawText(QRectF(labels[i].x(), labels[i].y(), 60, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(texts[i]));
    }

    // 昨收线
    if (data_.preClose > 0) {
        double y = priceToY(data_.preClose);
        p.setPen(QPen(QColor("#aaaaaa"), 1, Qt::DashLine));
        p.drawLine(QPointF(0, y), QPointF(w, y));
        p.setPen(QColor("#aaaaaa"));
        p.drawText(QRectF(w + 2, y - 8, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("昨收 %1").arg(data_.preClose, 0, 'f', 2));
    }
    // 量轴
    p.drawText(QRectF(w + 2, volRect_.top() + 2, kRightAxisW - 4, 14),
               Qt::AlignLeft | Qt::AlignVCenter, QString::number(volHi_));
    // MACD 轴
    p.drawText(QRectF(w + 2, macdRect_.top() + 2, kRightAxisW - 4, 14),
               Qt::AlignLeft | Qt::AlignVCenter, QString::number(macdMaxAbs_));
}

void TimelineChart::drawPriceLines(QPainter& p) {
    const double w = width() - kRightAxisW;
    // 价格线
    QPainterPath pricePath, avgPath;
    for (size_t i = 0; i < data_.points.size(); ++i) {
        const auto& pt = data_.points[i];
        double x = xFor(minutesFromOpen(pt.time));
        double y = priceToY(pt.price);
        if (i == 0) pricePath.moveTo(x, y);
        else pricePath.lineTo(x, y);

        if (i < avgLine_.size() && avgLine_[i] > 0) {
            double ay = priceToY(avgLine_[i]);
            if (i == 0) avgPath.moveTo(x, ay);
            else avgPath.lineTo(x, ay);
        }
    }
    QPen pricePen(QColor("#4fc3f7"), 1.2);
    p.setPen(pricePen);
    p.drawPath(pricePath);
    QPen avgPen(QColor("#ffa726"), 1.2);
    p.setPen(avgPen);
    p.drawPath(avgPath);

    // 均价线图例
    p.setPen(QColor("#888888"));
    p.drawText(QRectF(w - 110, kTitleH + 2, 106, 14), Qt::AlignRight,
               tr("均价 %1").arg(avgLine_.empty() ? 0.0 : avgLine_.back(), 0, 'f', 2));
}

void TimelineChart::drawVolume(QPainter& p) {
    const double w = width() - kRightAxisW;
    const double bw = w / 240.0;

    QPainterPath upPath, downPath;
    for (size_t i = 0; i < data_.points.size(); ++i) {
        const auto& pt = data_.points[i];
        int m = minutesFromOpen(pt.time);
        if (m < 0 || m > 239) continue;
        // 分时量 = 每分钟增量（腾讯原始为累计值）
        const double cum = static_cast<double>(pt.volume);
        const double prev = i > 0 ? static_cast<double>(data_.points[i - 1].volume) : 0.0;
        const double barVol = cum - prev;
        if (barVol <= 0) continue;
        double x = xFor(m);
        double h = barVol / volHi_ * volRect_.height();
        auto& path = pt.price >= data_.preClose ? upPath : downPath;
        path.addRect(QRectF(x - 0.35 * bw, volRect_.bottom() - h, 0.7 * bw, h));
    }
    p.fillPath(upPath, kUpColor);
    p.fillPath(downPath, kDownColor);

    // 图例
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor("#d4d4d4"));
    p.drawText(QPointF(volRect_.left() + 6, volRect_.top() + 12), tr("分时量"));
}

void TimelineChart::drawMacd(QPainter& p) {
    if (macdDif_.empty()) return;

    // 0 轴
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(macdRect_.left(), macdRect_.center().y()),
               QPointF(macdRect_.right(), macdRect_.center().y()));

    // 柱
    QPainterPath upHist, downHist;
    const double bw = (width() - kRightAxisW) / 240.0;
    for (size_t i = 0; i < macdHist_.size() && i < data_.points.size(); ++i) {
        if (!std::isfinite(macdHist_[i])) continue;
        const int m = minutesFromOpen(data_.points[i].time);
        if (m < 0 || m > 239) continue;
        const double midY = macdRect_.center().y();
        const double y = macdToY(macdHist_[i]);
        auto& path = macdHist_[i] >= 0 ? upHist : downHist;
        path.addRect(QRectF(xFor(m) - 0.35 * bw, std::min(y, midY), 0.7 * bw,
                            std::max(std::abs(y - midY), 1.0)));
    }
    p.fillPath(upHist, kUpColor);
    p.fillPath(downHist, kDownColor);

    // DIF/DEA 线
    auto drawLine = [&](const std::vector<double>& series, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (size_t i = 0; i < series.size() && i < data_.points.size(); ++i) {
            if (!std::isfinite(series[i])) { started = false; continue; }
            const int m = minutesFromOpen(data_.points[i].time);
            if (m < 0 || m > 239) { started = false; continue; }
            const double x = xFor(m);
            if (!started) { path.moveTo(x, macdToY(series[i])); started = true; }
            else { path.lineTo(x, macdToY(series[i])); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };
    drawLine(macdDif_, QColor("#ffffff"));
    drawLine(macdDea_, QColor("#ffd54f"));

    // 图例
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    const int lastIdx = static_cast<int>(macdDif_.size()) - 1;
    p.setPen(QColor("#d4d4d4"));
    p.drawText(QPointF(macdRect_.left() + 6, macdRect_.top() + 12),
               QStringLiteral("分时MACD  DIF %1  DEA %2")
                   .arg(macdDif_[static_cast<size_t>(lastIdx)], 0, 'f', 2)
                   .arg(macdDea_[static_cast<size_t>(lastIdx)], 0, 'f', 2));
}

void TimelineChart::drawCrosshair(QPainter& p) {
    if (mouseIndex_ < 0 || mouseIndex_ >= static_cast<int>(data_.points.size())) return;
    const auto& pt = data_.points[static_cast<size_t>(mouseIndex_)];
    double x = xFor(minutesFromOpen(pt.time));

    const double volBottom = height() - kBottomAxisH;
    p.setPen(QPen(QColor(200, 200, 200, 160), 1, Qt::DashLine));
    p.drawLine(QPointF(x, kTitleH), QPointF(x, volBottom));
    p.drawLine(QPointF(0, mouseY_), QPointF(width() - kRightAxisW, mouseY_));

    const double change = data_.preClose > 0
        ? (pt.price - data_.preClose) / data_.preClose * 100.0 : 0.0;
    const double avg = mouseIndex_ < static_cast<int>(avgLine_.size())
        ? avgLine_[static_cast<size_t>(mouseIndex_)] : 0.0;
    QString txt = QStringLiteral("%1\n价格:%2\n均价:%3\n涨跌幅:%4% 量:%5")
        .arg(QString::fromStdString(utils::toDateTimeString(pt.time)))
        .arg(pt.price, 0, 'f', 2)
        .arg(avg, 0, 'f', 2)
        .arg(change, 0, 'f', 2)
        .arg(static_cast<qlonglong>(pt.volume));

    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    QRect box = fm.boundingRect(QRect(0, 0, 200, 200), Qt::AlignLeft | Qt::AlignTop, txt);
    box.adjust(-6, -4, 10, 4);
    box.moveTo(std::min(x + 8, static_cast<double>(width() - box.width() - 4)),
               std::max(kTitleH, static_cast<double>(kTitleH + 4)));
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(QColor("#e8e8e8"));
    p.drawRect(box);
    p.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop, txt);
}

void TimelineChart::mouseMoveEvent(QMouseEvent* event) {
    mouseY_ = event->pos().y();
    if (!data_.points.empty()) {
        int idx = 0;
        double bestDist = 1e18;
        for (size_t i = 0; i < data_.points.size(); ++i) {
            double d = std::abs(xFor(minutesFromOpen(data_.points[i].time)) - event->pos().x());
            if (d < bestDist) { bestDist = d; idx = static_cast<int>(i); }
        }
        mouseIndex_ = idx;
    }
    update();
}

void TimelineChart::leaveEvent(QEvent*) {
    mouseIndex_ = -1;
    update();
}

} // namespace st

#include "moc_time_line_chart.cpp"
