#pragma once

#include "data/idata_provider.h"
#include "foundation/stock_code.h"
#include "engine/analyzer/overlay_analysis.h"
#include "data/eastmoney_sector_provider.h"
#include <QWidget>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
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
    ~TimelineChart() override;  // 停止自动刷新定时器，避免关闭窗口时触发异步任务

    /// 异步加载分时数据（loadGen_ 守卫竞态）
    void loadStock(const StockCode& code, const QString& name);

    /// 直接载入已计算好的分时数据（自定义指数等外部数据源）
    void loadIntraday(IntradayData intraday, const StockCode& code, const QString& name);

    /// 外部数据模式：非空时 loadStock/定时刷新 不再向 provider 拉取，改调 reloadFn
    /// （由拥有者重算后经 loadIntraday 喂入）；置空退出该模式
    void setExternalReloader(std::function<void()> reloadFn);

    /// 设置叠加对比目标（指数/个股/板块/概念）——按视图隔离：只叠加本图分时
    void setOverlay(const OverlayTarget& target);
    /// 清除叠加
    void clearOverlay();
    bool overlayActive() const { return overlayActive_; }
    /// 当前叠加目标名称（未叠加为空）
    QString overlayName() const { return overlayTarget_.name; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setData(IntradayData data);
    void refreshData();  // 定时器静默刷新（不闪"加载中"）
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
    void drawOverlayLine(QPainter& p);
    void drawVolume(QPainter& p);
    void drawMacd(QPainter& p);
    void drawCrosshair(QPainter& p);

    void fetchOverlayData();  // 异步拉取叠加标的分时并存储（auto-refresh 只重对齐不重拉）

    IDataProvider* provider_ = nullptr;
    std::function<void()> externalReloader_;  // 外部数据模式（自定义指数等）
    IntradayData data_;
    StockCode code_;
    QString name_;
    bool loading_ = false;
    int loadGen_ = 0;
    QTimer* refreshTimer_ = nullptr;  // 实时自动刷新

    std::vector<double> avgLine_;
    std::vector<double> macdDif_, macdDea_, macdHist_;
    int mouseIndex_ = -1;
    double mouseY_ = 0;

    QRectF mainRect_, volRect_, macdRect_;
    double priceHi_ = 0, priceLo_ = 0, volHi_ = 0, macdMaxAbs_ = 0;
    double symRange_ = 0;  // 涨跌对称区间（昨收 ± symRange_），保证涨/跌分段一致

    // 叠加对比（指数/个股/板块/概念）——按视图隔离：只叠加本图分时
    bool overlayActive_ = false;
    OverlayTarget overlayTarget_;
    IntradayData overlayData_;                    // 缓存叠加标的分时（auto-refresh 只重对齐）
    std::vector<IntradayOverlayRow> overlayRows_; // 已按 data_.points 分钟对齐
    int overlayGen_ = 0;                          // 叠加数据异步拉取世代守卫
    int overlayAnchor_ = -1;                      // 首个 matched 索引（每帧重算）
    std::shared_ptr<EastMoneySectorProvider> sectorProvider_;
};

} // namespace st
