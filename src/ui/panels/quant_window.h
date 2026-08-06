#pragma once

#include "foundation/stock_code.h"
#include <QMainWindow>

class QCloseEvent;

namespace st {

class IDataProvider;
class PatternPanel;
class AdvisorPanel;
class SentimentPanel;
class ScreenerPanel;
class PaperTradePanel;
class OptimizationPanel;
class StrategyComparePanel;
class StressTestPanel;
class StrategyPanel;
class BacktestPanel;

/// 量化工作台 — 独立窗口，承载全部量化面板
///
/// 从主窗口菜单「量化 → 量化工作台」打开（非模态，WA_DeleteOnClose）。
/// 含 3 个 Intelligence 面板（形态识别/优化建议/舆情情绪）+ 原主窗口的
/// 选股/策略/回测/参数优化/策略对比/压力测试/模拟交易。
/// 内部跨面板信号：策略/参数优化/优化建议 → 回测面板；双击结果 → 主窗口中央图。
class QuantWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit QuantWindow(IDataProvider* provider, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;  // 调试：关闭前检查堆完整性

signals:
    /// 双击结果行 → 主窗口打开 K 线
    void openChart(const StockCode& code);

private:
    PatternPanel* patternPanel_ = nullptr;
    AdvisorPanel* advisorPanel_ = nullptr;
    SentimentPanel* sentimentPanel_ = nullptr;
    ScreenerPanel* screenerPanel_ = nullptr;
    PaperTradePanel* paperTradePanel_ = nullptr;
    OptimizationPanel* optimizationPanel_ = nullptr;
    StrategyComparePanel* strategyComparePanel_ = nullptr;
    StressTestPanel* stressTestPanel_ = nullptr;
    StrategyPanel* strategyPanel_ = nullptr;
    BacktestPanel* backtestPanel_ = nullptr;
};

} // namespace st
