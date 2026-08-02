#pragma once

#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "foundation/types.h"
#include <QWidget>
#include <QString>
#include <QColor>
#include <vector>

class QPainterPath;

namespace st {

class TencentProvider;

/// K线图主控件 — QPainter 自绘蜡烛图
///
/// 布局（上→下）: 主图(蜡烛+MA+BOLL) / 成交量 / MACD / RSI / 时间轴。
/// 交互: 十字光标+OHLC 浮框、滚轮缩放、左键拖拽平移、周期切换。
/// 数据: loadStock 异步加载（loadGen_ 世代守卫竞态）。
class KLineChart : public QWidget {
    Q_OBJECT

public:
    explicit KLineChart(TencentProvider* provider, QWidget* parent = nullptr);

    /// 异步加载股票（保持当前周期），快速切换安全
    void loadStock(const StockCode& code, const QString& name);

    /// 周期切换（日/周/月等）→ 重载数据
    void setPeriod(BarPeriod period);
    BarPeriod period() const { return period_; }

signals:
    void periodChanged(BarPeriod period);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct IndicatorSet {
        std::vector<double> ma5, ma10, ma20, ma60;
        std::vector<double> bollMid, bollUpper, bollLower;
        std::vector<double> macdDif, macdDea, macdHist;
        std::vector<double> rsi6, rsi12, rsi24;
        bool valid = false;
    };

    // 主线程 setData（含指标重算），由异步回调调用
    void setData(const std::vector<Bar>& bars);

    void recomputeIndicators();
    void computeVisibleRange();

    // 坐标辅助
    double bodyWidth() const { return mainRect_.width() / visibleCount_; }
    double barCenterX(int index) const;
    double priceToY(double price) const;
    double volToY(double v) const;
    double macdToY(double v) const;
    double rsiToY(double v) const;

    void drawCandles(QPainter& p);
    void drawVolume(QPainter& p);
    void drawOverlayLines(QPainter& p);
    void drawMacd(QPainter& p);
    void drawRsi(QPainter& p);
    void drawAxes(QPainter& p);
    void drawTitle(QPainter& p);
    void drawCrosshair(QPainter& p);

    TencentProvider* provider_ = nullptr;
    std::vector<Bar> bars_;
    StockCode code_;
    QString name_;
    BarPeriod period_ = BarPeriod::Daily;
    bool loading_ = false;
    int loadGen_ = 0;

    int firstVisible_ = 0;
    int visibleCount_ = 120;
    int mouseIndex_ = -1;
    double mouseY_ = 0;
    bool dragging_ = false;
    int dragStartX_ = 0;
    int dragStartFirst_ = 0;

    IndicatorSet ind_;
    QRectF mainRect_, volRect_, macdRect_, rsiRect_;
    double priceHi_ = 0, priceLo_ = 0, volHi_ = 0, macdMaxAbs_ = 0;
};

} // namespace st
