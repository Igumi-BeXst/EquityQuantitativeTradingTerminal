#pragma once

#include <QWidget>

namespace st {

/// 回测面板 — 参数配置 + 结果展示（P6 实现）
class BacktestPanel : public QWidget {
    Q_OBJECT

public:
    explicit BacktestPanel(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
