#pragma once

#include "engine/optimizer/grid_heatmap.h"
#include <QWidget>
#include <optional>
#include <utility>

namespace st {

/// 参数热力图（自绘）— x/y 轴参数值矩阵 + 目标值着色（优红劣绿）+ 悬停浮框 + 最优格高亮
///
/// 双击格子 → cellActivated(x, y)（调用方应用该组参数）。
class GridHeatmapWidget : public QWidget {
    Q_OBJECT

public:
    explicit GridHeatmapWidget(QWidget* parent = nullptr);

    void setData(const HeatmapMatrix& m, double bestX, double bestY,
                 bool betterIsHigher, const QString& xLabel,
                 const QString& yLabel, const QString& valueLabel);
    void clear();
    bool hasData() const { return m_.has_value(); }

    /// 位置 → (x 参数值, y 参数值)；不在格子内返回 nullopt
    std::optional<std::pair<double, double>> cellAt(const QPoint& pos) const;

signals:
    /// 双击某格 → 该格参数组合
    void cellActivated(double x, double y);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    std::optional<std::vector<std::vector<QRectF>>> cellRects() const;
    std::optional<int> indexOf(const std::vector<double>& axis, double v) const;

    std::optional<HeatmapMatrix> m_;
    double bestX_ = 0.0;
    double bestY_ = 0.0;
    bool betterIsHigher_ = true;
    QString xLabel_, yLabel_, valueLabel_;
};

} // namespace st
