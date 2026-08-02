#include "ui/widgets/kline_chart.h"
#include "data/tencent_provider.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/indicators.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMetaObject>
#include <algorithm>
#include <cmath>

namespace st {

namespace {
// A股红涨绿跌，独立于 QSS 主题
const QColor kUpColor("#e54648");
const QColor kDownColor("#2e9e5b");

// 布局常量
constexpr double kRightAxisW = 64;
constexpr double kBottomAxisH = 26;
constexpr double kTitleH = 22;
constexpr double kPaneGap = 2;

QString periodLabel(BarPeriod p) {
    switch (p) {
        case BarPeriod::Daily:   return QStringLiteral("日线");
        case BarPeriod::Weekly:  return QStringLiteral("周线");
        case BarPeriod::Monthly: return QStringLiteral("月线");
        case BarPeriod::Minute5: return QStringLiteral("5分");
        case BarPeriod::Minute15:return QStringLiteral("15分");
        case BarPeriod::Minute30:return QStringLiteral("30分");
        case BarPeriod::Minute60:return QStringLiteral("60分");
        default:                 return QStringLiteral("日线");
    }
}
}  // namespace

KLineChart::KLineChart(TencentProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    setMouseTracking(true);
    setMinimumSize(400, 300);
}

// ============================================================
// 数据
// ============================================================
void KLineChart::loadStock(const StockCode& code, const QString& name) {
    const int gen = ++loadGen_;
    code_ = code;
    name_ = name;
    loading_ = true;
    update();
    if (!provider_) { loading_ = false; return; }

    ThreadPool::submitIO([this, gen, code] {
        auto bars = provider_->getBars(code, period_, DateTime{}, utils::now());
        QMetaObject::invokeMethod(this, [this, gen, bars = std::move(bars)]() mutable {
            if (gen != loadGen_) return;  // 旧请求回写丢弃
            setData(bars);
        }, Qt::QueuedConnection);
    });
}

void KLineChart::setPeriod(BarPeriod period) {
    if (period == period_) return;
    period_ = period;
    if (code_.isValid()) loadStock(code_, name_);
    emit periodChanged(period_);
}

void KLineChart::setData(const std::vector<Bar>& bars) {
    bars_ = bars;
    loading_ = false;
    firstVisible_ = std::max(0, static_cast<int>(bars_.size()) - visibleCount_);
    mouseIndex_ = -1;
    recomputeIndicators();
    update();
}

void KLineChart::recomputeIndicators() {
    ind_ = IndicatorSet{};
    if (bars_.size() < 2) return;

    std::vector<double> closes(bars_.size());
    for (size_t i = 0; i < bars_.size(); ++i) closes[i] = bars_[i].close;

    using namespace st::indicators;
    ind_.ma5  = sma(closes, 5);
    ind_.ma10 = sma(closes, 10);
    ind_.ma20 = sma(closes, 20);
    ind_.ma60 = sma(closes, 60);

    auto b = boll(closes, 20, 2.0);
    ind_.bollMid = std::move(b.mid);
    ind_.bollUpper = std::move(b.upper);
    ind_.bollLower = std::move(b.lower);

    auto m = macd(closes);
    ind_.macdDif = std::move(m.dif);
    ind_.macdDea = std::move(m.dea);
    ind_.macdHist = std::move(m.hist);

    ind_.rsi6 = rsi(closes, 6);
    ind_.rsi12 = rsi(closes, 12);
    ind_.rsi24 = rsi(closes, 24);

    ind_.valid = true;
}

// ============================================================
// 坐标与范围
// ============================================================
double KLineChart::barCenterX(int index) const {
    return mainRect_.left() + (index - firstVisible_ + 0.5) * bodyWidth();
}

double KLineChart::priceToY(double price) const {
    if (priceHi_ - priceLo_ < 1e-12) return mainRect_.top();
    return mainRect_.top() + (priceHi_ - price) / (priceHi_ - priceLo_) * mainRect_.height();
}

double KLineChart::volToY(double v) const {
    return volRect_.bottom() - v / volHi_ * volRect_.height();
}

double KLineChart::macdToY(double v) const {
    double mid = macdRect_.center().y();
    return mid - v / macdMaxAbs_ * (macdRect_.height() / 2.0);
}

double KLineChart::rsiToY(double v) const {
    return rsiRect_.top() + (100.0 - v) / 100.0 * rsiRect_.height();
}

void KLineChart::computeVisibleRange() {
    if (bars_.empty()) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    double hi = -1e18, lo = 1e18;
    for (int i = start; i < end; ++i) {
        hi = std::max(hi, bars_[static_cast<size_t>(i)].high);
        lo = std::min(lo, bars_[static_cast<size_t>(i)].low);
    }
    if (ind_.valid) {
        for (int i = start; i < end; ++i) {
            for (double v : {ind_.ma5[static_cast<size_t>(i)],
                             ind_.ma10[static_cast<size_t>(i)],
                             ind_.ma20[static_cast<size_t>(i)],
                             ind_.ma60[static_cast<size_t>(i)],
                             ind_.bollUpper[static_cast<size_t>(i)],
                             ind_.bollLower[static_cast<size_t>(i)]}) {
                if (std::isfinite(v)) { hi = std::max(hi, v); lo = std::min(lo, v); }
            }
        }
    }
    double pad = (hi - lo) * 0.05;
    if (pad < 1e-9) pad = 1.0;
    priceHi_ = hi + pad;
    priceLo_ = lo - pad;

    volHi_ = 0;
    for (int i = start; i < end; ++i) {
        volHi_ = std::max(volHi_, static_cast<double>(bars_[static_cast<size_t>(i)].volume));
    }
    if (volHi_ <= 0) volHi_ = 1;

    macdMaxAbs_ = 0;
    if (ind_.valid) {
        for (int i = start; i < end; ++i) {
            for (double v : {ind_.macdDif[static_cast<size_t>(i)],
                             ind_.macdDea[static_cast<size_t>(i)],
                             ind_.macdHist[static_cast<size_t>(i)]}) {
                if (std::isfinite(v)) macdMaxAbs_ = std::max(macdMaxAbs_, std::abs(v));
            }
        }
    }
    if (macdMaxAbs_ <= 0) macdMaxAbs_ = 1;
}

// ============================================================
// 绘制
// ============================================================
void KLineChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));  // 图表底色（两主题通用深色）

    if (bars_.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter, loading_ ? tr("加载中…") : tr("无数据"));
        return;
    }

    // 布局
    const double plotW = width() - kRightAxisW;
    const double plotH = height() - kTitleH - kBottomAxisH;
    const QRectF plot(0, kTitleH, plotW, plotH);
    const double mainH = plotH * 0.55;
    const double volH = plotH * 0.15;
    const double macdH = plotH * 0.15;
    const double rsiH = plotH - mainH - volH - macdH - 3 * kPaneGap;
    mainRect_ = QRectF(plot.left(), plot.top(), plot.width(), mainH);
    volRect_  = QRectF(plot.left(), plot.top() + mainH + kPaneGap, plot.width(), volH);
    macdRect_ = QRectF(plot.left(), plot.top() + mainH + volH + 2 * kPaneGap, plot.width(), macdH);
    rsiRect_  = QRectF(plot.left(), plot.top() + mainH + volH + macdH + 3 * kPaneGap,
                       plot.width(), rsiH);

    computeVisibleRange();
    drawTitle(p);
    drawCandles(p);
    drawOverlayLines(p);
    drawVolume(p);
    drawMacd(p);
    drawRsi(p);
    drawAxes(p);
    drawCrosshair(p);
}

void KLineChart::drawCandles(QPainter& p) {
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double bodyHalf = std::min(0.35 * bodyWidth(), 6.0);

    QPainterPath upWick, downWick, upBody, downBody;
    for (int i = start; i < end; ++i) {
        const auto& b = bars_[static_cast<size_t>(i)];
        const bool rising = b.close >= b.open;
        const double cx = barCenterX(i);
        const double hiY = priceToY(b.high);
        const double loY = priceToY(b.low);
        const double openY = priceToY(b.open);
        const double closeY = priceToY(b.close);

        auto& wick = rising ? upWick : downWick;
        auto& body = rising ? upBody : downBody;
        wick.moveTo(cx, hiY);
        wick.lineTo(cx, loY);
        const double top = std::min(openY, closeY);
        const double bh = std::max(std::abs(closeY - openY), 1.0);
        body.addRect(QRectF(cx - bodyHalf, top, 2 * bodyHalf, bh));
    }
    p.setPen(QPen(kUpColor, 1));
    p.setBrush(kUpColor);
    p.drawPath(upWick);
    p.fillPath(upBody, kUpColor);
    p.setPen(QPen(kDownColor, 1));
    p.setBrush(kDownColor);
    p.drawPath(downWick);
    p.fillPath(downBody, kDownColor);
}

void KLineChart::drawVolume(QPainter& p) {
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double bw = bodyWidth();

    QPainterPath upPath, downPath;
    for (int i = start; i < end; ++i) {
        const auto& b = bars_[static_cast<size_t>(i)];
        const bool rising = b.close >= b.open;
        const double cx = barCenterX(i);
        const double v = static_cast<double>(b.volume);
        if (v <= 0) continue;
        const double h = volRect_.bottom() - volToY(v);
        auto& path = rising ? upPath : downPath;
        path.addRect(QRectF(cx - 0.4 * bw, volRect_.bottom() - h, 0.8 * bw, h));
    }
    p.fillPath(upPath, kUpColor);
    p.fillPath(downPath, kDownColor);
}

void KLineChart::drawOverlayLines(QPainter& p) {
    if (!ind_.valid) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    auto drawLine = [&](const std::vector<double>& data, const QColor& color,
                        Qt::PenStyle style = Qt::SolidLine) {
        QPainterPath path;
        bool started = false;
        for (int i = start; i < end; ++i) {
            const double v = data[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { started = false; continue; }
            if (!started) { path.moveTo(barCenterX(i), priceToY(v)); started = true; }
            else { path.lineTo(barCenterX(i), priceToY(v)); }
        }
        QPen pen(color);
        pen.setStyle(style);
        p.setPen(pen);
        p.drawPath(path);
    };

    drawLine(ind_.ma5, QColor("#ffd54f"));
    drawLine(ind_.ma10, QColor("#ce93d8"));
    drawLine(ind_.ma20, QColor("#4dd0e1"));
    drawLine(ind_.ma60, QColor("#90a4ae"));
    drawLine(ind_.bollMid, QColor("#f5f5f5"));
    drawLine(ind_.bollUpper, QColor("#ff8a65"), Qt::DashLine);
    drawLine(ind_.bollLower, QColor("#ff8a65"), Qt::DashLine);
}

void KLineChart::drawMacd(QPainter& p) {
    if (!ind_.valid) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double bw = bodyWidth();

    // 0 轴
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(macdRect_.left(), macdRect_.center().y()),
               QPointF(macdRect_.right(), macdRect_.center().y()));

    // 柱
    QPainterPath upHist, downHist;
    for (int i = start; i < end; ++i) {
        const double h = ind_.macdHist[static_cast<size_t>(i)];
        if (!std::isfinite(h)) continue;
        const double midY = macdRect_.center().y();
        const double y = macdToY(h);
        auto& path = h >= 0 ? upHist : downHist;
        path.addRect(QRectF(barCenterX(i) - 0.4 * bw, std::min(y, midY), 0.8 * bw,
                            std::max(std::abs(y - midY), 1.0)));
    }
    p.fillPath(upHist, kUpColor);
    p.fillPath(downHist, kDownColor);

    // DIF / DEA 线
    auto drawLine = [&](const std::vector<double>& data, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (int i = start; i < end; ++i) {
            const double v = data[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { started = false; continue; }
            if (!started) { path.moveTo(barCenterX(i), macdToY(v)); started = true; }
            else { path.lineTo(barCenterX(i), macdToY(v)); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };
    drawLine(ind_.macdDif, QColor("#ffffff"));
    drawLine(ind_.macdDea, QColor("#ffd54f"));
}

void KLineChart::drawRsi(QPainter& p) {
    if (!ind_.valid) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    // 30/70 参考线
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(rsiRect_.left(), rsiToY(30)), QPointF(rsiRect_.right(), rsiToY(30)));
    p.drawLine(QPointF(rsiRect_.left(), rsiToY(70)), QPointF(rsiRect_.right(), rsiToY(70)));

    auto drawLine = [&](const std::vector<double>& data, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (int i = start; i < end; ++i) {
            const double v = data[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { started = false; continue; }
            if (!started) { path.moveTo(barCenterX(i), rsiToY(v)); started = true; }
            else { path.lineTo(barCenterX(i), rsiToY(v)); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };
    drawLine(ind_.rsi6, QColor("#ffd54f"));
    drawLine(ind_.rsi12, QColor("#4dd0e1"));
    drawLine(ind_.rsi24, QColor("#ce93d8"));
}

void KLineChart::drawAxes(QPainter& p) {
    p.setPen(QColor("#888888"));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);

    // 主图价格轴
    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        double price = priceLo_ + (priceHi_ - priceLo_) * i / ticks;
        double y = mainRect_.top() + mainRect_.height() * i / ticks;
        // 网格线
        p.setPen(QPen(QColor(255, 255, 255, 18), 1));
        p.drawLine(QPointF(mainRect_.left(), y), QPointF(mainRect_.right(), y));
        p.setPen(QColor("#888888"));
        p.drawText(QRectF(mainRect_.right() + 2, y - 8, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(price, 'f', 2));
    }

    // 量/MACD/RSI 轴标签
    auto drawAxisLabel = [&](const QRectF& r, double v) {
        p.drawText(QRectF(r.right() + 2, r.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(v, 'f', v >= 100 ? 0 : 2));
    };
    drawAxisLabel(volRect_, volHi_);
    drawAxisLabel(macdRect_, macdMaxAbs_);
    p.drawText(QRectF(rsiRect_.right() + 2, rsiRect_.top() + 2, kRightAxisW - 4, 14),
               Qt::AlignLeft | Qt::AlignVCenter, "100");

    // 时间轴
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double axisY = height() - kBottomAxisH;
    p.setPen(QColor("#888888"));
    const auto& firstBar = bars_[static_cast<size_t>(start)];
    const auto& lastBar = bars_[static_cast<size_t>(end - 1)];
    const auto& midBar = bars_[static_cast<size_t>(start + (end - start) / 2)];
    const auto fmt = [&](const Bar& b) {
        return QString::fromStdString(utils::toDateString(b.time));
    };
    p.drawText(QRectF(mainRect_.left(), axisY + 4, 90, 16), Qt::AlignLeft, fmt(firstBar));
    p.drawText(QRectF(mainRect_.center().x() - 45, axisY + 4, 90, 16), Qt::AlignCenter, fmt(midBar));
    p.drawText(QRectF(mainRect_.right() - 90, axisY + 4, 90, 16), Qt::AlignRight, fmt(lastBar));
}

void KLineChart::drawTitle(QPainter& p) {
    p.setPen(QColor("#d4d4d4"));
    QFont f = p.font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);

    QString text = name_ + "  " + QString::fromStdString(code_.displayCode());
    if (!bars_.empty()) {
        const auto& b = bars_.back();
        // 涨跌幅 vs 前一根收盘（Bar 无 preClose 字段，图表用前收作昨收）
        const double prevClose = bars_.size() >= 2 ? bars_[bars_.size() - 2].close : 0.0;
        const double change = prevClose > 0 ? (b.close - prevClose) / prevClose * 100.0 : 0.0;
        QColor c = change >= 0 ? kUpColor : kDownColor;
        text += QStringLiteral("  [%1]  %2").arg(periodLabel(period_),
                                                 QString::number(b.close, 'f', 2));
        p.setPen(c);
        text += QStringLiteral("  %1%").arg(change, 0, 'f', 2);
    }
    if (loading_) text += tr("  (加载中…)");
    p.drawText(QRectF(4, 2, width() - 8, kTitleH - 2), Qt::AlignLeft | Qt::AlignVCenter, text);
}

void KLineChart::drawCrosshair(QPainter& p) {
    if (mouseIndex_ < 0 || mouseIndex_ >= static_cast<int>(bars_.size())) return;
    const double cx = barCenterX(mouseIndex_);

    p.setPen(QPen(QColor(200, 200, 200, 160), 1, Qt::DashLine));
    p.drawLine(QPointF(cx, mainRect_.top()), QPointF(cx, rsiRect_.bottom()));
    p.drawLine(QPointF(mainRect_.left(), mouseY_), QPointF(mainRect_.right(), mouseY_));

    // 浮框
    const auto& b = bars_[static_cast<size_t>(mouseIndex_)];
    const double prevClose = mouseIndex_ >= 1
        ? bars_[static_cast<size_t>(mouseIndex_ - 1)].close : 0.0;
    const double change = prevClose > 0 ? (b.close - prevClose) / prevClose * 100.0 : 0.0;
    QString txt = QStringLiteral("%1\n开:%2 高:%3\n低:%4 收:%5\n涨跌幅:%6% 量:%7")
        .arg(QString::fromStdString(utils::toDateString(b.time)))
        .arg(b.open, 0, 'f', 2).arg(b.high, 0, 'f', 2)
        .arg(b.low, 0, 'f', 2).arg(b.close, 0, 'f', 2)
        .arg(change, 0, 'f', 2)
        .arg(static_cast<qlonglong>(b.volume));
    if (ind_.valid) {
        txt += QStringLiteral("\nMA5:%1 MA10:%2 MA20:%3")
                   .arg(ind_.ma5[static_cast<size_t>(mouseIndex_)], 0, 'f', 2)
                   .arg(ind_.ma10[static_cast<size_t>(mouseIndex_)], 0, 'f', 2)
                   .arg(ind_.ma20[static_cast<size_t>(mouseIndex_)], 0, 'f', 2);
    }

    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    QRect box = fm.boundingRect(QRect(0, 0, 200, 200), Qt::AlignLeft | Qt::AlignTop, txt);
    box.adjust(-6, -4, 10, 4);
    box.moveTo(std::min(cx + 8, static_cast<double>(width() - box.width() - 4)),
               std::max(0.0, std::min(mouseY_ - box.height() / 2,
                                      static_cast<double>(height() - box.height() - 4))));
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(QColor("#e8e8e8"));
    p.drawRect(box);
    p.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop, txt);
}

// ============================================================
// 交互
// ============================================================
void KLineChart::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && !bars_.empty()) {
        const int dx = dragStartX_ - event->pos().x();
        const int maxFirst = std::max(0, static_cast<int>(bars_.size()) - visibleCount_);
        firstVisible_ = std::clamp(dragStartFirst_ + static_cast<int>(dx / bodyWidth()),
                                   0, maxFirst);
    }
    mouseY_ = event->pos().y();
    if (!bars_.empty()) {
        int idx = firstVisible_ + static_cast<int>(
            (event->pos().x() - mainRect_.left()) / bodyWidth());
        idx = std::clamp(idx, firstVisible_,
                         std::min(firstVisible_ + visibleCount_ - 1,
                                  static_cast<int>(bars_.size()) - 1));
        mouseIndex_ = idx;
    }
    update();
}

void KLineChart::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !bars_.empty()) {
        dragging_ = true;
        dragStartX_ = event->pos().x();
        dragStartFirst_ = firstVisible_;
        setCursor(Qt::ClosedHandCursor);
    }
}

void KLineChart::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        unsetCursor();
    }
}

void KLineChart::wheelEvent(QWheelEvent* event) {
    if (bars_.empty()) return;
    const double factor = event->angleDelta().y() > 0 ? 0.9 : 1.1;
    const int newCount = std::clamp(static_cast<int>(std::round(visibleCount_ * factor)),
                                    20, 800);
    const double ratio = std::clamp(
        (event->position().x() - mainRect_.left()) / std::max(mainRect_.width(), 1.0), 0.0, 1.0);
    const int anchorIdx = firstVisible_ + static_cast<int>(ratio * visibleCount_);
    const int maxFirst = std::max(0, static_cast<int>(bars_.size()) - newCount);
    firstVisible_ = std::clamp(anchorIdx - static_cast<int>(ratio * newCount), 0, maxFirst);
    visibleCount_ = newCount;
    update();
    event->accept();
}

void KLineChart::leaveEvent(QEvent*) {
    mouseIndex_ = -1;
    update();
}

} // namespace st

#include "moc_kline_chart.cpp"
