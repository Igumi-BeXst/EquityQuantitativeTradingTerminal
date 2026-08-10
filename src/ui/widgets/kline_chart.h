#pragma once

#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "engine/analyzer/overlay_analysis.h"
#include "engine/journal/trade_journal.h"
#include "data/eastmoney_sector_provider.h"
#include <QWidget>
#include <QString>
#include <QColor>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QPainterPath;

namespace st {

class IDataProvider;

/// K线图主控件 — QPainter 自绘蜡烛图
///
/// 布局（上→下）: 指标控制条 / 主图(蜡烛+MA) / 可见面板(VOL/BOLL/MACD/RSI 可开关)。
/// 交互: 十字光标+OHLC 浮框、滚轮缩放、左键拖拽平移、周期切换。
/// 数据: loadStock 异步加载（loadGen_ 世代守卫竞态）。
class KLineChart : public QWidget {
    Q_OBJECT

public:
    enum class Indicator { Ma, Vol, Boll, Macd, Rsi, RelativeStrength };

    explicit KLineChart(IDataProvider* provider, QWidget* parent = nullptr);

    /// 异步加载股票（保持当前周期），快速切换安全
    void loadStock(const StockCode& code, const QString& name);

    /// 周期切换（日/周/月等）→ 重载数据
    void setPeriod(BarPeriod period);
    BarPeriod period() const { return period_; }

    /// 直接载入已计算好的 K 线（自定义指数等外部数据源；同 loadStock 的清叠加/清标注语义）
    void loadBars(const std::vector<Bar>& bars, const StockCode& code, const QString& name);

    /// 外部数据模式：非空时 loadStock/setPeriod 不再向 provider 拉取，改调 reloadFn
    /// （由拥有者重算数据后经 loadBars 喂入）；置空退出该模式
    void setExternalReloader(std::function<void()> reloadFn);

    /// 指标开关
    void setIndicatorVisible(Indicator ind, bool visible);
    bool isIndicatorVisible(Indicator ind) const;

    /// 导出当前 K 线数据为 CSV（当前周期全部已加载 bar）
    void exportData();
    /// 清除画线标注
    void clearAnnotations();

    /// 设置交易标记 + 持仓成本线（空 = 无；切股/清空时传空；切周期保留）
    void setTradeMarks(const std::vector<TradeMark>& marks,
                       const std::vector<HoldingLine>& holdings);

    /// 设置叠加对比目标（指数/个股/板块/概念）——按视图隔离：只叠加本图（日/周/月）
    void setOverlay(const OverlayTarget& target, bool showRelativeStrength);
    /// 清除叠加
    void clearOverlay();
    bool overlayActive() const { return overlayActive_; }
    /// 当前叠加目标名称（未叠加为空）
    QString overlayName() const { return overlayTarget_.name; }

signals:
    void periodChanged(BarPeriod period);

    /// 十字光标移动：发出当前悬停 K 线的日期（nullopt = 离开图表/数据重载）
    void crosshairDateChanged(const std::optional<DateTime>& date);

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
    void buildLayout();
    void drawTradeMarks(QPainter& p);       // 成本线 + 买卖箭头
    void buildMarkBarIndex();               // 标记 → bar 索引（对齐）

    // 坐标辅助
    double plotTop() const { return controlBar_ ? controlBar_->height() : 0.0; }
    double bodyWidth() const { return mainRect_.width() / visibleCount_; }
    double barCenterX(int index) const;
    double priceToY(double price) const;
    double volToY(double v) const;
    double bollToY(double v) const;
    double macdToY(double v) const;
    double rsiToY(double v) const;

    void drawCandles(QPainter& p);
    void drawVolume(QPainter& p);
    void drawBoll(QPainter& p);
    void drawMacd(QPainter& p);
    void drawRsi(QPainter& p);
    void drawOverlayLines(QPainter& p);
    void drawRebasedOverlay(QPainter& p);
    void drawRelativeStrength(QPainter& p);
    double rsToY(double v) const;
    void fetchOverlayData();
    void drawAxes(QPainter& p);
    void drawTitle(QPainter& p);
    void drawCrosshair(QPainter& p);
    void drawPaneHeader(QPainter& p, const QRectF& rect, const QString& title,
                        const QColor& color);

    // 画线工具
    enum class DrawMode { None, Horizontal, Trend };
    struct ChartLine {
        bool horizontal = false;
        int idx1 = 0;
        double price1 = 0.0;
        int idx2 = 0;
        double price2 = 0.0;
    };
    void setDrawMode(DrawMode mode);
    void drawAnnotations(QPainter& p);
    int indexAtX(double x) const;
    double priceFromY(double y) const;

    IDataProvider* provider_ = nullptr;
    std::function<void()> externalReloader_;  // 外部数据模式（自定义指数等）
    std::vector<Bar> bars_;
    StockCode code_;
    QString name_;
    BarPeriod period_ = BarPeriod::Daily;
    bool loading_ = false;
    int loadGen_ = 0;

    int firstVisible_ = 0;
    int visibleCount_ = 120;
    int mouseIndex_ = -1;
    std::optional<DateTime> lastEmittedDate_;  // 已发出的十字线日期（避免重复发射）
    double mouseX_ = 0;
    double mouseY_ = 0;
    bool dragging_ = false;
    int dragStartX_ = 0;
    int dragStartFirst_ = 0;

    // 指标开关
    QWidget* controlBar_ = nullptr;
    bool showMa_ = true;
    bool showVol_ = true;
    bool showBoll_ = true;
    bool showMacd_ = true;
    bool showRsi_ = true;

    IndicatorSet ind_;
    QRectF mainRect_, volRect_, macdRect_, rsiRect_, bollRect_;
    std::vector<std::pair<Indicator, QRectF>> paneRects_;  // 可见面板及其区域
    double priceHi_ = 0, priceLo_ = 0, volHi_ = 0, bollHi_ = 0, bollLo_ = 0, macdMaxAbs_ = 0;

    DrawMode drawMode_ = DrawMode::None;
    bool drawing_ = false;
    int dragStartIdx_ = -1;
    double dragStartPrice_ = 0.0;
    std::vector<ChartLine> lines_;  // 画线标注（锚定 bar 索引+价格，随平移缩放稳定）

    // 交易标记
    std::vector<TradeMark> tradeMarks_;
    std::vector<HoldingLine> holdings_;
    std::vector<int> markBarIndex_;         // 与 tradeMarks_ 平行：-1 = 不在数据范围

    // 叠加对比（指数/个股/板块/概念）——按视图隔离：只叠加本图（日/周/月）
    bool overlayActive_ = false;
    OverlayTarget overlayTarget_;
    std::vector<OverlayRow> overlayRows_;   // 已按 bars_ 日期对齐
    bool showRelativeStrength_ = false;
    int overlayGen_ = 0;                    // 叠加数据异步拉取世代守卫
    int overlayAnchor_ = -1;                // 可见区首个 matched 索引（每帧重算）
    QRectF rsRect_;                         // 相对强弱面板
    double rsHi_ = 100, rsLo_ = 100;        // RS 面板 y 范围（锚点=100）
    std::shared_ptr<EastMoneySectorProvider> sectorProvider_;
};

} // namespace st
