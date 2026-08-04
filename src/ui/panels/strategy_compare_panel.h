#pragma once

#include "engine/analyzer/strategy_comparator.h"
#include "foundation/stock_code.h"
#include <QWidget>
#include <memory>

class QListWidget;
class QDateEdit;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;
class QTableView;
class QLabel;

namespace st {

class IDataProvider;
class DataCache;
class ComparisonTableModel;
class EquityCurveWidget;

/// 策略对比面板 — 多策略同数据同时回测 + 净值叠加 + 蒙特卡洛置信区间
class StrategyComparePanel : public QWidget {
    Q_OBJECT

public:
    explicit StrategyComparePanel(IDataProvider* provider, QWidget* parent = nullptr);

private slots:
    void onRunClicked();
    void onAllDataFetched();
    void onMonteCarloClicked();

private:
    void onResult(const std::vector<ComparisonItemResult>& items);
    std::vector<StockCode> selectedSymbols() const;
    std::vector<ComparisonItem> selectedItems() const;
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    std::unique_ptr<DataCache> cache_;

    QListWidget* strategyList_ = nullptr;
    QListWidget* stockList_ = nullptr;
    QDateEdit* startDate_ = nullptr;
    QDateEdit* endDate_ = nullptr;
    QDoubleSpinBox* capital_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QPushButton* mcBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableView* resultView_ = nullptr;
    ComparisonTableModel* resultModel_ = nullptr;
    EquityCurveWidget* equityCurve_ = nullptr;
    QLabel* p5_ = nullptr;
    QLabel* p50_ = nullptr;
    QLabel* p95_ = nullptr;
    QLabel* probLoss_ = nullptr;

    bool running_ = false;
    std::vector<ComparisonItemResult> lastResults_;
};

} // namespace st
