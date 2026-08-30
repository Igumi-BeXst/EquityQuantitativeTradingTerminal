#include "ui/widgets/equity_curve_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace st {

namespace {
constexpr double kPadLeft = 48;
constexpr double kPadRight = 8;
constexpr double kPadTop = 8;
constexpr double kPadBottom = 22;
constexpr double kAxisTickLen = 4;
}  // namespace

EquityCurveWidget::EquityCurveWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setMouseTracking(true);
}

void EquityCurveWidget::setSeries(const std::vector<EquitySeries>& seriesIn) {
    series_ = seriesIn;
    xLabels_.clear();
    update();
}

void EquityCurveWidget::setData(const std::vector<double>& equity) {
    series_.clear();
    if (!equity.empty()) {
        series_.push_back({tr("策略"), QColor("#4caf50"), equity});
    }
    xLabels_.clear();
    update();
}

void EquityCurveWidget::setXLabels(const std::vector<QString>& labels) {
    xLabels_ = labels;
    update();
}

void EquityCurveWidget::setIncludeZero(bool includeZero) {
    includeZero_ = includeZero;
    update();
}

void EquityCurveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));

    std::vector<EquitySeries> vis;
    for (const auto& s : series_) {
        if (!s.data.empty()) vis.push_back(s);
    }
    if (vis.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter, tr("无数据"));
        return;
    }

    double hi = 1.0, lo = 1.0;
    for (const auto& s : vis) {
        for (double v : s.data) {
            hi = std::max(hi, v);
            lo = std::min(lo, v);
        }
    }
    if (includeZero_) lo = std::min(lo, 0.0);
    double pad = (hi - lo) * 0.08;
    if (pad < 0.005) pad = 0.01;
    hi += pad;
    lo = lo - pad;

    const QRectF plot(kPadLeft, kPadTop, width() - kPadLeft - kPadRight,
                      height() - kPadTop - kPadBottom);
    if (plot.width() <= 0.0 || plot.height() <= 0.0) return;
    const auto yFor = [&](double v) {
        return plot.bottom() - (v - lo) / (hi - lo) * plot.height();
    };
    const auto xForSeries = [&](const EquitySeries& s, int i) {
        return plot.left() +
               static_cast<double>(i) /
                   std::max(1, static_cast<int>(s.data.size()) - 1) * plot.width();
    };
    const auto& first = vis.front();

    // ---- Y 轴刻度 ----
    p.setPen(QPen(QColor(255, 255, 255, 50), 1));
    p.drawLine(QPointF(plot.left() - kAxisTickLen, plot.top()),
               QPointF(plot.left() - kAxisTickLen, plot.bottom()));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    QFontMetrics fm(f);
    const int tickCount = 4;
    for (int i = 0; i <= tickCount; ++i) {
        const double v = lo + (hi - lo) * static_cast<double>(i) / tickCount;
        const double y = yFor(v);
        p.setPen(QPen(QColor(255, 255, 255, 70), 1));
        p.drawLine(QPointF(plot.left() - kAxisTickLen, y),
                   QPointF(plot.left(), y));
        p.setPen(QColor("#a0a0a0"));
        p.drawText(QRectF(0, y - 7, plot.left() - kAxisTickLen - 2, 14),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', 3));
    }
    (void)fm;

    // ---- 0 轴参考线（只有 0 落在可视范围内才绘制） ----
    if (lo <= 0.0 && hi >= 0.0) {
        const double y0 = yFor(0.0);
        p.setPen(QPen(QColor(255, 80, 80, 160), 1, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), y0), QPointF(plot.right(), y0));
        p.setPen(QColor("#ff8080"));
        p.drawText(QRectF(plot.left() + 2, y0 - 14, 40, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, tr("0"));
    }

    // ---- 1.0 初始净值虚线 ----
    p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
    p.drawLine(QPointF(plot.left(), yFor(1.0)), QPointF(plot.right(), yFor(1.0)));

    // ---- 各序列折线 + 渐变填充 ----
    for (const auto& s : vis) {
        QPainterPath path;
        path.moveTo(xForSeries(s, 0), yFor(s.data[0]));
        for (size_t i = 1; i < s.data.size(); ++i) {
            path.lineTo(xForSeries(s, static_cast<int>(i)), yFor(s.data[i]));
        }
        QColor c = s.color;
        c.setAlpha(45);
        QColor c0 = s.color;
        c0.setAlpha(0);
        QLinearGradient grad(0, plot.top(), 0, plot.bottom());
        grad.setColorAt(0, c);
        grad.setColorAt(1, c0);
        QPainterPath fill = path;
        fill.lineTo(xForSeries(s, static_cast<int>(s.data.size()) - 1), plot.bottom());
        fill.lineTo(xForSeries(s, 0), plot.bottom());
        fill.closeSubpath();
        p.fillPath(fill, grad);

        p.setPen(QPen(s.color, 1.6));
        p.drawPath(path);
    }

    // ---- X 轴标签 ----
    const double axisY = height() - 14;
    p.setPen(QColor("#a0a0a0"));
    const int lastIdx = static_cast<int>(first.data.size()) - 1;
    auto drawXLabel = [&](int idx, Qt::Alignment align) {
        if (idx < 0) return;
        QString text = (static_cast<size_t>(idx) < xLabels_.size() && !xLabels_[idx].isEmpty())
            ? xLabels_[idx]
            : QString::number(idx + 1);
        const double xStep = (lastIdx > 0)
            ? plot.width() / static_cast<double>(lastIdx) : 0.0;
        QRectF r(plot.left() + xStep * idx - 40, axisY, 80, 14);
        if (align & Qt::AlignRight) r.moveRight(plot.right());
        if (align & Qt::AlignLeft) r.moveLeft(plot.left());
        p.drawText(r, Qt::AlignCenter, text);
    };
    drawXLabel(0, Qt::AlignLeft);
    if (lastIdx > 0) drawXLabel(lastIdx / 2, Qt::AlignCenter);
    drawXLabel(lastIdx, Qt::AlignRight);

    // ---- 图例 ----
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm2(f);
    int legendW = 0;
    for (const auto& s : vis) {
        const QString t = QStringLiteral("%1 %2").arg(s.name).arg(s.data.back(), 0, 'f', 3);
        legendW = std::max(legendW, fm2.horizontalAdvance(t) + 18);
    }
    const int maxLegendW = std::max(8, static_cast<int>(plot.width() - 8));
    legendW = std::min(legendW, maxLegendW);
    if (legendW < 16) legendW = maxLegendW;

    const double lx = plot.left() + 4;
    double ly = plot.top() + 4;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 120));
    p.drawRoundedRect(QRectF(lx - 2, ly - 2,
                             legendW + 4,
                             static_cast<double>(vis.size()) * 15.0 + 4), 4, 4);
    for (const auto& s : vis) {
        const QString t = QStringLiteral("%1 %2").arg(s.name).arg(s.data.back(), 0, 'f', 3);
        p.setPen(Qt::NoPen);
        p.setBrush(s.color);
        p.drawRect(QRectF(lx, ly + 3, 8, 8));
        p.setPen(QColor("#d4d4d4"));
        p.drawText(QRectF(lx + 12, ly, legendW - 14, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, t);
        ly += 15;
    }

    // ---- 悬停 / 点击十字线 + 数据浮框 ----
    if (hoverValid_ && hoverIndex_ >= 0 && hoverIndex_ <= lastIdx) {
        const double hx = xForSeries(first, hoverIndex_);
        p.setPen(QPen(QColor(255, 255, 255, 100), 1, Qt::DotLine));
        p.drawLine(QPointF(hx, plot.top()), QPointF(hx, plot.bottom()));

        QStringList lines;
        if (static_cast<size_t>(hoverIndex_) < xLabels_.size() && !xLabels_[hoverIndex_].isEmpty()) {
            lines << xLabels_[hoverIndex_];
        } else {
            lines << tr("第 %1 点").arg(hoverIndex_ + 1);
        }
        for (const auto& s : vis) {
            if (static_cast<size_t>(hoverIndex_) >= s.data.size()) continue;
            const double v = s.data[hoverIndex_];
            p.setPen(QPen(s.color, 2));
            p.drawEllipse(QPointF(hx, yFor(v)), 3, 3);
            lines << QStringLiteral("%1: %2").arg(s.name).arg(v, 0, 'f', 3);
        }

        // 浮框（尽量不超出右边界）
        const double boxW = 150;
        const double boxH = 16.0 * lines.size() + 8;
        double bx = hoverX_ + 12;
        if (bx + boxW > width()) bx = hoverX_ - boxW - 12;
        double by = plot.top() + 4;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawRoundedRect(QRectF(bx, by, boxW, boxH), 4, 4);
        p.setPen(QColor("#e0e0e0"));
        double ty = by + 4;
        for (const QString& line : lines) {
            p.drawText(QRectF(bx + 6, ty, boxW - 12, 14),
                       Qt::AlignLeft | Qt::AlignVCenter, line);
            ty += 16;
        }
    }
}

void EquityCurveWidget::mouseMoveEvent(QMouseEvent* event) {
    const auto pos = event->position();
    std::vector<EquitySeries> vis;
    for (const auto& s : series_) {
        if (!s.data.empty()) vis.push_back(s);
    }
    if (vis.empty()) return;

    const QRectF plot(kPadLeft, kPadTop, width() - kPadLeft - kPadRight,
                      height() - kPadTop - kPadBottom);
    if (!plot.contains(pos)) {
        if (hoverValid_) {
            hoverValid_ = false;
            hoverIndex_ = -1;
            update();
        }
        return;
    }

    const auto& first = vis.front();
    const int lastIdx = static_cast<int>(first.data.size()) - 1;
    if (lastIdx <= 0) return;
    const double ratio = (pos.x() - plot.left()) / plot.width();
    int idx = static_cast<int>(std::lround(ratio * lastIdx));
    idx = std::clamp(idx, 0, lastIdx);
    hoverIndex_ = idx;
    hoverX_ = pos.x();
    hoverValid_ = true;
    update();
}

void EquityCurveWidget::mousePressEvent(QMouseEvent* event) {
    // 点击与悬停共用同一套查看逻辑；点击时刷新并固定浮框（离开时自动清除）
    mouseMoveEvent(event);
}

void EquityCurveWidget::leaveEvent(QEvent*) {
    hoverValid_ = false;
    hoverIndex_ = -1;
    update();
}

} // namespace st

#include "moc_equity_curve_widget.cpp"
