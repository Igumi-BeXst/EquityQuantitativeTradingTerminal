#pragma once

#include "foundation/stock_code.h"
#include <QMainWindow>
#include <memory>

class QCloseEvent;

namespace st {
class IDataProvider;
class CentralChartWidget;
class TradeJournalEngine;

/// 独立图表窗口 — 完整 CentralChartWidget（周期/叠加/交易标记），多实例可并存
class ChartWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ChartWindow(IDataProvider* provider,
                         std::shared_ptr<TradeJournalEngine> journal = nullptr,
                         QWidget* parent = nullptr);
    ~ChartWindow() override;

    /// 打开时加载股票（周期默认日线）
    void loadStock(const StockCode& code, const QString& name);

    /// 刷新本窗口交易标记（供 MainWindow 集中分发——onChange 覆盖式，窗口不自己注册）
    void refreshTradeMarks();

signals:
    /// 新窗口内再开新窗口（递归）
    void openNewWindow(const StockCode& code, const QString& name);

private:
    CentralChartWidget* chart_ = nullptr;
    std::shared_ptr<TradeJournalEngine> journal_;
};

} // namespace st
