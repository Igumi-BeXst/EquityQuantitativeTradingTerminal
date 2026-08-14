#include "ui/widgets/grid_heatmap_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <limits>

namespace st {

namespace {

constexpr char kHmUpColor[] = "#e54648";     // 红（优）
constexpr char kHmDownColor[] = "#2e9e5b";   // 绿（劣）
constexpr char kHmMidColor[] = "#4a4a4c";    // 灰（中）
constexpr char kHmNanColor[] = "#26262a";    // 缺格

QColor lerpColor(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * t,
        a.greenF() + (b.greenF() - a.greenF()) * t,
        a.blueF() + (b.blueF() - a.blueF()) * t);
}

QColor goodnessColor(double g) {  // g ∈ [0,1]：0=劣(绿) 0.5=中(灰) 1=优(红)
    return (g <= 0.5)
        ? lerpColor(QColor(kHmDownColor), QColor(kHmMidColor), g * 2.0)
        : lerpColor(QColor(kHmMidColor), QColor(kHmUpColor), (g - 0.5) * 2.0);
}

constexpr int kMarginLeft = 48;   // 左（y 轴标签）
constexpr int kMarginRight = 66;  // 右（图例）
constexpr int kMarginTop = 22;    // 上（标题）
constexpr int kMarginBottom = 30; // 下（x 轴标签）

}  // namespace

GridHeatmapWidget::GridHeatmapWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(220);
}

void GridHeatmapWidget::setData(const HeatmapMatrix& m, double bestX, double bestY,
                                bool betterIsHigher, const QString& xLabel,
                                const QString& yLabel, const QString& valueLabel) {
    m_ = m;
    bestX_ = bestX;
    bestY_ = bestY;
    betterIsHigher_ = betterIsHigher;
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    valueLabel_ = valueLabel;
    update();
}

void GridHeatmapWidget::clear() {
    m_.reset();
    update();
}

std::optional<std::pair<double, double>> GridHeatmapWidget::cellAt(const QPoint& pos) const {
    if (!m_) return std::nullopt;
    const auto rects = cellRects();
    if (!rects) return std::nullopt;
    for (int yi = 0; yi < static_cast<int>(m_->yValues.size()); ++yi) {
        for (int xi = 0; xi < static_cast<int>(m_->xValues.size()); ++xi) {
            if (rects->at(static_cast<size_t>(yi)).at(static_cast<size_t>(xi))
                    .contains(pos)) {
                return std::make_pair(m_->xValues[static_cast<size_t>(xi)],
                                      m_->yValues[static_cast<size_t>(yi)]);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::vector<QRectF>>> GridHeatmapWidget::cellRects() const {
    if (!m_ || m_->xValues.empty() || m_->yValues.empty()) return std::nullopt;
    const double nx = static_cast<double>(m_->xValues.size());
    const double ny = static_cast<double>(m_->yValues.size());
    const double cw = (static_cast<double>(width()) - kMarginLeft - kMarginRight) / nx;
    const double ch = (static_cast<double>(height()) - kMarginTop - kMarginBottom) / ny;
    std::vector<std::vector<QRectF>> rects;
    rects.reserve(m_->yValues.size());
    for (int yi = 0; yi < static_cast<int>(m_->yValues.size()); ++yi) {
        std::vector<QRectF> row;
        row.reserve(m_->xValues.size());
        for (int xi = 0; xi < static_cast<int>(m_->xValues.size()); ++xi) {
            row.emplace_back(QRectF(kMarginLeft + cw * xi, kMarginTop + ch * yi, cw, ch));
        }
        rects.push_back(std::move(row));
    }
    return rects;
}

std::optional<int> GridHeatmapWidget::indexOf(const std::vector<double>& axis,
                                              double v) const {
    for (size_t i = 0; i < axis.size(); ++i) {
        if (std::abs(axis[i] - v) < 1e-9) return static_cast<int>(i);
    }
    return std::nullopt;
}

void GridHeatmapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto rects = cellRects();
    if (!m_ || !rects) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("无热力图数据（需两个参数且至少 2 组结果）"));
        return;
    }

    p.setPen(QColor("#d4d4d4"));
    p.drawText(QRect(0, 2, width(), 16), Qt::AlignCenter,
               tr("参数热力图（%1）").arg(valueLabel_));

    // 数值范围（忽略 NaN）
    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -vmin;
    for (const auto& row : m_->values) {
        for (double v : row) {
            if (!std::isnan(v)) {
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
        }
    }
    const double span = (vmax > vmin) ? (vmax - vmin) : 1.0;

    for (int yi = 0; yi < static_cast<int>(m_->yValues.size()); ++yi) {
        for (int xi = 0; xi < static_cast<int>(m_->xValues.size()); ++xi) {
            const QRectF cell = rects->at(static_cast<size_t>(yi))
                                    .at(static_cast<size_t>(xi));
            const double v = m_->values[static_cast<size_t>(yi)][static_cast<size_t>(xi)];
            if (std::isnan(v)) {
                p.fillRect(cell, QColor(kHmNanColor));
            } else {
                const double t = (v - vmin) / span;
                const double g = betterIsHigher_ ? t : 1.0 - t;  // 优劣度 0~1
                p.fillRect(cell, goodnessColor(g));
                if (cell.width() >= 34 && cell.height() >= 18) {
                    p.setPen(QColor(255, 255, 255, 200));
                    p.drawText(cell, Qt::AlignCenter, QString::number(v, 'f', 2));
                }
            }
            // 最优组合高亮（白框）
            const bool isBest = !std::isnan(v) &&
                std::abs(m_->xValues[static_cast<size_t>(xi)] - bestX_) < 1e-9 &&
                std::abs(m_->yValues[static_cast<size_t>(yi)] - bestY_) < 1e-9;
            if (isBest) {
                p.setPen(QPen(QColor(255, 255, 255), 2));
                p.drawRect(cell.adjusted(1, 1, -1, -1));
            } else {
                p.setPen(QPen(QColor(0, 0, 0, 60), 1));
                p.drawRect(cell.adjusted(0, 0, -1, -1));
            }
        }
    }

    // 坐标轴刻度 + 轴名
    p.setPen(QColor("#d4d4d4"));
    // x 刻度：画在最后一行格子下方（底部留白区），不压在表格上
    const double xLabelY = rects->back().front().bottom() + 4;
    for (int xi = 0; xi < static_cast<int>(m_->xValues.size()); ++xi) {
        const QRectF& cell = rects->back().at(static_cast<size_t>(xi));
        p.drawText(QRectF(cell.left(), xLabelY, cell.width(), 14), Qt::AlignCenter,
                   QString::number(m_->xValues[static_cast<size_t>(xi)], 'g', 6));
    }
    for (int yi = 0; yi < static_cast<int>(m_->yValues.size()); ++yi) {
        const QRectF& cell = rects->at(static_cast<size_t>(yi)).front();
        p.drawText(QRectF(2, cell.top(), kMarginLeft - 6, cell.height()),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(m_->yValues[static_cast<size_t>(yi)], 'g', 6));
    }
    // 轴名：x 横排在底部，y 竖排（防中文长名截断）
    p.drawText(QRectF(0, height() - 14, width(), 14), Qt::AlignCenter,
               tr("%1 →").arg(xLabel_));
    p.save();
    p.translate(10, height() / 2.0);
    p.rotate(-90);
    p.drawText(QRectF(-height() / 2.0, 0, height(), 14), Qt::AlignCenter,
               tr("← %1").arg(yLabel_));
    p.restore();

    // 右侧图例（优→劣 渐变条）
    const QRectF legend(static_cast<qreal>(width()) - 58, 26, 16,
                        static_cast<qreal>(height()) - 80);
    for (int i = 0; i <= 32; ++i) {
        const double g = 1.0 - static_cast<double>(i) / 32.0;
        const double y0 = legend.top() + legend.height() * i / 33.0;
        const double y1 = legend.top() + legend.height() * (i + 1) / 33.0;
        p.fillRect(QRectF(legend.left(), y0, legend.width(), y1 - y0),
                   goodnessColor(g));
    }
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.drawRect(legend);
    p.setPen(QColor("#d4d4d4"));
    p.drawText(QRectF(legend.right() + 4, legend.top() - 2, 40, 14),
               Qt::AlignLeft, tr("优"));
    p.drawText(QRectF(legend.right() + 4, legend.bottom() - 12, 40, 14),
               Qt::AlignLeft, tr("劣"));
}

void GridHeatmapWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_) return;
    const auto cell = cellAt(event->pos());
    if (!cell) {
        QToolTip::hideText();
        return;
    }
    const auto xi = indexOf(m_->xValues, cell->first);
    const auto yi = indexOf(m_->yValues, cell->second);
    if (!xi || !yi) return;
    const double v = m_->values[static_cast<size_t>(*yi)][static_cast<size_t>(*xi)];
    QString text = QStringLiteral("%1=%2\n%3=%4")
        .arg(xLabel_, QString::number(cell->first, 'g', 6),
             yLabel_, QString::number(cell->second, 'g', 6));
    text += std::isnan(v) ? tr("\n（无该组合）")
                          : tr("\n%1 %2").arg(valueLabel_, QString::number(v, 'f', 2));
    QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

void GridHeatmapWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const auto cell = cellAt(event->pos());
    if (!cell) return;
    emit cellActivated(cell->first, cell->second);
}

} // namespace st

#include "moc_grid_heatmap_widget.cpp"
