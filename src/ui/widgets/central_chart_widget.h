#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "engine/analyzer/overlay_analysis.h"
#include "engine/analyzer/custom_index.h"
#include "engine/journal/trade_journal.h"
#include <QWidget>
#include <optional>
#include <string>
#include <vector>

class QStackedWidget;
class QToolButton;
class QPushButton;

namespace st {

class IDataProvider;
class KLineChart;
class TimelineChart;

/// 中央图表容器 — 分时图(TimelineChart) ↔ K线图(KLineChart) 切换 + 周期栏
///
/// 「叠加对比」按钮作用于当前显示的图（按视图隔离）：分时→Timeline 独立叠加，
/// 日/周/月→KLine 独立叠加；互不影响，各自切股清除、K线图内切周期保留重取。
class CentralChartWidget : public QWidget {
    Q_OBJECT

public:
    explicit CentralChartWidget(IDataProvider* provider, QWidget* parent = nullptr);

    /// 加载股票（默认日线 K 线）
    void loadStock(const StockCode& code, const QString& name);

    /// 加载自定义指数到当前图表（进入外部数据模式：切周期/分时刷新都重算喂数据）
    void loadCustomIndex(const CustomIndex& idx);

    /// 周期切换: 分时→Timeline，其余→KLine
    void setPeriod(BarPeriod period);

    /// 当前股票代码（截图文件名用）
    StockCode currentCode() const { return currentCode_; }

    /// 设置当前股票的交易标记 + 持仓成本线（转发给分时+K线两图）
    /// 内部缓存：loadStock/setPeriod 重载图表后自动重新注入
    void setTradeMarks(const std::vector<TradeMark>& marks,
                       const std::vector<HoldingLine>& holdings);

signals:
    /// 转发 K线十字光标日期（日/周/月；nullopt = 离开/重载回退最新）
    void crosshairDateChanged(const std::optional<DateTime>& date);
    /// 筹码分布按钮被点击（请求切换主窗口筹码面板可见性）
    void chipDockToggled();

public:
    /// 同步筹码按钮勾选状态（主窗口筹码面板可见性变化时调用）
    void setChipButtonChecked(bool visible);

private:
    void applyOverlay(const OverlayTarget& target, bool showRelativeStrength);
    void clearCurrentOverlay();
    void refreshOverlayButton();
    void reloadCustomIndexNow();
    void clearCustomIndexMode();
    /// 重注入缓存的交易标记（loadStock/setPeriod/指数重算清标记后调用）
    void reapplyTradeMarks();

    std::optional<CustomIndex> customIndex_;   // 非空 = 当前在自定义指数模式
    std::string customIndexCode_;              // 伪代码 "CI"+id（仅占位，不查行情）
    int customIndexGen_ = 0;                   // 指数重算异步世代守卫

    IDataProvider* provider_ = nullptr;
    KLineChart* kline_ = nullptr;
    TimelineChart* timeline_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QPushButton* overlayBtn_ = nullptr;
    QPushButton* chipBtn_ = nullptr;   // 筹码分布开关（联动主窗口筹码面板）
    StockCode currentCode_;
    QString currentName_;

    std::vector<TradeMark> marks_;     // 当前股票交易标记缓存（重载后重新注入）
    std::vector<HoldingLine> holdings_; // 当前股票持仓成本线缓存
};

} // namespace st
