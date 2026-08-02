#pragma once

#include <QWidget>

namespace st {

/// 图层渲染基类（P6 实现，K线图 9 层渲染系统）
class ChartLayers : public QWidget {
    Q_OBJECT

public:
    explicit ChartLayers(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
