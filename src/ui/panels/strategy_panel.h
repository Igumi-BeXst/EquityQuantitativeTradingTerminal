#pragma once

#include <QWidget>

namespace st {

/// 策略面板 — 策略列表/参数/运行状态（P6 实现）
class StrategyPanel : public QWidget {
    Q_OBJECT

public:
    explicit StrategyPanel(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
