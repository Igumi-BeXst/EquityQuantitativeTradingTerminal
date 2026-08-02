#pragma once

#include <QWidget>

namespace st {

/// 十字光标交互层（P6 实现，K线图悬浮提示）
class Crosshair : public QWidget {
    Q_OBJECT

public:
    explicit Crosshair(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
