#include "ui/widgets/equity_curve_widget.h"
#include <QPainter>
#include <QPainterPath>
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

void EquityCurveWidget::setData(const std::vector<double>& equity) {
    data_ = equity;
    update();
}

void EquityCurveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));

    if (data_.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter, tr("无数据"));
        return;
    }

    double hi = 1.0, lo = 1.0;
    for (double v : data_) { hi = std::max(hi, v); lo = std::min(lo, v); }
    double pad = (hi - lo) * 0.08;
    if (pad < 0.005) pad = 0.01;
    hi += pad; lo = std::max(0.0, lo - pad);

    QRectF plot(kPadLeft, kPadTop, width() - kPadLeft - kPadRight,
                height() - kPadTop - kPadBottom);
    auto yFor = [&](double v) { return plot.bottom() - (v - lo) / (hi - lo) * plot.height(); };
    auto xFor = [&](int i) {
        return plot.left() + static_cast<double>(i) / std::max(1, static_cast<int>(data_.size()) - 1) * plot.width();
    };

    // 基准 1.0 虚线
    p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
    p.drawLine(QPointF(plot.left(), yFor(1.0)), QPointF(plot.right(), yFor(1.0)));

    // 折线 + 渐变填充
    QPainterPath path;
    path.moveTo(xFor(0), yFor(data_[0]));
    for (size_t i = 1; i < data_.size(); ++i) {
        path.lineTo(xFor(static_cast<int>(i)), yFor(data_[i]));
    }
    QLinearGradient grad(0, plot.top(), 0, plot.bottom());
    grad.setColorAt(0, QColor(76, 175, 80, 60));
    grad.setColorAt(1, QColor(76, 175, 80, 0));
    QPainterPath fill = path;
    fill.lineTo(xFor(static_cast<int>(data_.size()) - 1), plot.bottom());
    fill.lineTo(xFor(0), plot.bottom());
    fill.closeSubpath();
    p.fillPath(fill, grad);

    p.setPen(QPen(QColor("#4caf50"), 1.6));
    p.drawPath(path);

    // 期末净值标注
    p.setPen(QColor("#d4d4d4"));
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(QRectF(plot.right() - 130, plot.top() + 2, 126, 16), Qt::AlignRight,
               tr("期末净值 %1").arg(data_.back(), 0, 'f', 3));
}

} // namespace st

#include "moc_equity_curve_widget.cpp"
