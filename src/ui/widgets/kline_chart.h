#pragma once

#include <QWidget>

namespace st {

/// K线图主控件（P6 实现，QPainter 自绘 + 图层渲染）
class KLineChart : public QWidget {
    Q_OBJECT

public:
    explicit KLineChart(QWidget* parent = nullptr);
};

} // namespace st
