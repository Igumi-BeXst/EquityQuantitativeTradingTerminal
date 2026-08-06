#pragma once

#include "engine/analyzer/stress_test.h"
#include "foundation/stock_code.h"
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;

namespace st {

class IDataProvider;
class DataCache;
class EquityCurveWidget;

/// 压力测试面板 — 历史极端行情片段回放 + 基线对比
class StressTestPanel : public QWidget {
    Q_OBJECT

public:
    explicit StressTestPanel(IDataProvider* provider, QWidget* parent = nullptr);

private slots:
    void onRunClicked();
    void onAllDataFetched();
    void onStrategyChanged();
    void onWindowChanged();

private:
    void onResult(StressTestOutput output);
    void displayWindow(const QString& windowId);
    void resetToIdle();
    std::vector<StockCode> selectedSymbols() const;

    IDataProvider* provider_ = nullptr;
    // shared_ptr：异步 lambda 按值捕获，面板销毁后 DataCache 仍存活
    std::shared_ptr<DataCache> cache_;

    QComboBox* strategyCombo_ = nullptr;
    QLabel* p1Label_ = nullptr;
    QLabel* p2Label_ = nullptr;
    QSpinBox* p1_ = nullptr;
    QSpinBox* p2_ = nullptr;
    QDoubleSpinBox* capital_ = nullptr;
    QComboBox* windowCombo_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;

    EquityCurveWidget* curve_ = nullptr;
    QLabel* ret_ = nullptr;
    QLabel* annual_ = nullptr;
    QLabel* sharpe_ = nullptr;
    QLabel* mdd_ = nullptr;
    QLabel* winRate_ = nullptr;
    QLabel* pf_ = nullptr;
    QLabel* trades_ = nullptr;
    QLabel* endEquity_ = nullptr;
    QLabel* baseRet_ = nullptr;
    QLabel* baseMdd_ = nullptr;
    QLabel* deltaRet_ = nullptr;

    StressTestOutput output_;
    bool running_ = false;
};

} // namespace st
