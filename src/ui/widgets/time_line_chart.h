#pragma once

#include <QWidget>

namespace st {

/// 分时图控件（P6 实现，日内走势 + 均线）
class TimelineChart : public QWidget {
    Q_OBJECT

public:
    explicit TimelineChart(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
