#pragma once

#include "data/idata_provider.h"
#include "foundation/stock_code.h"
#include <QWidget>
#include <QString>
#include <vector>

namespace st {

class IDataProvider;

/// 分时图 — 日内逐分钟价格线 + 均价线 + 量柱
///
/// X 轴 240 分钟（09:30-11:30 / 13:00-15:00），午休虚分隔。
/// 昨收线灰色虚线；价格线白/蓝；均价线橙；量柱红涨绿跌（对比昨收）。
class TimelineChart : public QWidget {
    Q_OBJECT

public:
    explicit TimelineChart(IDataProvider* provider, QWidget* parent = nullptr);

    /// 异步加载分时数据（loadGen_ 守卫竞态）
    void loadStock(const StockCode& code, const QString& name);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setData(IntradayData data);
    void computeRanges();
    void computeAvgLine();
    void computeMacd();

    int minutesFromOpen(const DateTime& t) const;
    double xFor(int minutes) const;
    double priceToY(double price) const;
    double volToY(double v) const;
    double macdToY(double v) const;

    void drawTitle(QPainter& p);
    void drawGridAndAxis(QPainter& p);
    void drawPriceLines(QPainter& p);
    void drawVolume(QPainter& p);
    void drawMacd(QPainter& p);
    void drawCrosshair(QPainter& p);

    IDataProvider* provider_ = nullptr;
    IntradayData data_;
    StockCode code_;
    QString name_;
    bool loading_ = false;
    int loadGen_ = 0;

    std::vector<double> avgLine_;
    std::vector<double> macdDif_, macdDea_, macdHist_;
    int mouseIndex_ = -1;
    double mouseY_ = 0;

    QRectF mainRect_, volRect_, macdRect_;
    double priceHi_ = 0, priceLo_ = 0, volHi_ = 0, macdMaxAbs_ = 0;
    double symRange_ = 0;  // 涨跌对称区间（昨收 ± symRange_），保证涨/跌分段一致
};

} // namespace st
