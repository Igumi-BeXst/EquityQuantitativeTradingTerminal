#include "ui/widgets/equity_curve_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace st {

namespace {
constexpr double kPadLeft = 8;
constexpr double kPadRight = 8;
constexpr double kPadTop = 8;
constexpr double kPadBottom = 22;
}  // namespace

EquityCurveWidget::EquityCurveWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
}

void EquityCurveWidget::setSeries(const std::vector<EquitySeries>& seriesIn) {
    series_ = seriesIn;
    update();
}

void EquityCurveWidget::setData(const std::vector<double>& equity) {
    series_.clear();
    if (!equity.empty()) {
        series_.push_back({tr("策略"), QColor("#4caf50"), equity});
    }
    update();
}

void EquityCurveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));

    // 过滤出非空序列
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
    double pad = (hi - lo) * 0.08;
    if (pad < 0.005) pad = 0.01;
    hi += pad;
    // 允许负值序列（累计盈亏/回撤等可为负），pad 已防贴边，无需钳制
    lo = lo - pad;

    const QRectF plot(kPadLeft, kPadTop, width() - kPadLeft - kPadRight,
                      height() - kPadTop - kPadBottom);
    if (plot.width() <= 0.0 || plot.height() <= 0.0) return;
    const auto yFor = [&](double v) {
        return plot.bottom() - (v - lo) / (hi - lo) * plot.height();
    };
    const auto xFor = [&](const EquitySeries& s, int i) {
        return plot.left() +
               static_cast<double>(i) /
                   std::max(1, static_cast<int>(s.data.size()) - 1) * plot.width();
    };

    // 基准 1.0 虚线
    p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
    p.drawLine(QPointF(plot.left(), yFor(1.0)), QPointF(plot.right(), yFor(1.0)));

    // 各序列折线 + 渐变填充
    for (const auto& s : vis) {
        QPainterPath path;
        path.moveTo(xFor(s, 0), yFor(s.data[0]));
        for (size_t i = 1; i < s.data.size(); ++i) {
            path.lineTo(xFor(s, static_cast<int>(i)), yFor(s.data[i]));
        }
        QColor c = s.color;
        c.setAlpha(45);
        QColor c0 = s.color;
        c0.setAlpha(0);
        QLinearGradient grad(0, plot.top(), 0, plot.bottom());
        grad.setColorAt(0, c);
        grad.setColorAt(1, c0);
        QPainterPath fill = path;
        fill.lineTo(xFor(s, static_cast<int>(s.data.size()) - 1), plot.bottom());
        fill.lineTo(xFor(s, 0), plot.bottom());
        fill.closeSubpath();
        p.fillPath(fill, grad);

        p.setPen(QPen(s.color, 1.6));
        p.drawPath(path);
    }

    // 左上角图例：色块 + 名称 + 期末净值
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    int legendW = 0;
    for (const auto& s : vis) {
        const QString t = QStringLiteral("%1 %2").arg(s.name).arg(s.data.back(), 0, 'f', 3);
        legendW = std::max(legendW, fm.horizontalAdvance(t) + 18);
    }
    const int maxLegendW = std::max(8, static_cast<int>(plot.width() - 8));
    legendW = std::min(legendW, maxLegendW);
    if (legendW < 16) legendW = maxLegendW;

    const double lx = plot.left() + 4;
    double ly = plot.top() + 4;
    // 半透明背景，避免被曲线遮挡看不清
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
}

} // namespace st

#include "moc_equity_curve_widget.cpp"
