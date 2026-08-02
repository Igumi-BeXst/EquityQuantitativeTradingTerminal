#pragma once

#include <QWidget>

namespace st {

/// 选股面板 — 因子/条件/排名（P7 实现）
class ScreenerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScreenerPanel(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
