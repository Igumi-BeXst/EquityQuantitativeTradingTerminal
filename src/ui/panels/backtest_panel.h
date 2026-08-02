#pragma once

#include "foundation/types.h"
#include <QWidget>
#include <memory>
#include <vector>

class QComboBox;
class QSpinBox;
class QListWidget;
class QDateEdit;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;
class QLabel;
class QTableView;

namespace st {

class TencentProvider;
class DataCache;
class EquityCurveWidget;
class TradeTableModel;
class IStrategy;
struct BacktestConfig;
struct BacktestResult;
struct Performance;
class StockCode;

/// 回测面板 — 选股/选策略/参数/日期/资金 → 异步回测 → 指标+净值曲线+成交明细
class BacktestPanel : public QWidget {
    Q_OBJECT

public:
    explicit BacktestPanel(TencentProvider* provider, QWidget* parent = nullptr);

    /// 从策略面板载入策略与参数
    void loadStrategy(const QString& id, const QVariantMap& params);

private slots:
    void onRunClicked();
    void onStrategyChanged();
    void onAllDataFetched();
    void onResult(const BacktestResult& result);

private:
    std::vector<StockCode> selectedSymbols() const;
    BacktestConfig makeConfig(const std::vector<StockCode>& symbols) const;
    std::shared_ptr<IStrategy> makeStrategy() const;
    void setMetrics(const Performance& perf, const BacktestResult& result);
    void updateParamLabels();

    TencentProvider* provider_ = nullptr;
    std::unique_ptr<DataCache> cache_;
    bool running_ = false;

    QComboBox* strategyCombo_ = nullptr;
    QLabel* p1Label_ = nullptr;
    QSpinBox* p1_ = nullptr;
    QLabel* p2Label_ = nullptr;
    QSpinBox* p2_ = nullptr;
    QListWidget* stockList_ = nullptr;
    QDateEdit* startDate_ = nullptr;
    QDateEdit* endDate_ = nullptr;
    QDoubleSpinBox* capital_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;

    QLabel* ret_ = nullptr;
    QLabel* annual_ = nullptr;
    QLabel* sharpe_ = nullptr;
    QLabel* mdd_ = nullptr;
    QLabel* winRate_ = nullptr;
    QLabel* pf_ = nullptr;
    QLabel* calmar_ = nullptr;
    QLabel* vol_ = nullptr;
    QLabel* sortino_ = nullptr;
    QLabel* alpha_ = nullptr;
    QLabel* beta_ = nullptr;
    QLabel* trades_ = nullptr;

    EquityCurveWidget* equityCurve_ = nullptr;
    QTableView* tradesView_ = nullptr;
    TradeTableModel* tradeModel_ = nullptr;
};

} // namespace st
