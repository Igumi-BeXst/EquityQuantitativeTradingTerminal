#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include <QWidget>

class QStackedWidget;
class QToolButton;

namespace st {

class TencentProvider;
class KLineChart;
class TimelineChart;

/// 中央图表容器 — 分时图(TimelineChart) ↔ K线图(KLineChart) 切换 + 周期栏
class CentralChartWidget : public QWidget {
    Q_OBJECT

public:
    explicit CentralChartWidget(TencentProvider* provider, QWidget* parent = nullptr);

    /// 加载股票（默认日线 K 线）
    void loadStock(const StockCode& code, const QString& name);

    /// 周期切换: 分时→Timeline，其余→KLine
    void setPeriod(BarPeriod period);

private:
    TencentProvider* provider_ = nullptr;
    KLineChart* kline_ = nullptr;
    TimelineChart* timeline_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    StockCode currentCode_;
    QString currentName_;
};

} // namespace st
