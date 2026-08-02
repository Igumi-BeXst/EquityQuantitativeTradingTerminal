#pragma once

#include <QWidget>

namespace st {

/// 市场全景面板 — 涨幅榜/跌幅榜/板块/指数（P6 实现）
class MarketPanel : public QWidget {
    Q_OBJECT

public:
    explicit MarketPanel(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace st
