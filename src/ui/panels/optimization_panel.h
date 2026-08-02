#pragma once

#include "engine/optimizer/grid_search.h"
#include "foundation/stock_code.h"
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QSpinBox;
class QDateEdit;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;
class QTableView;
class QListWidget;

namespace st {

class TencentProvider;
class DataCache;
class GridSearchTableModel;

/// 参数优化面板 — 两参数范围 + 目标函数 → 网格搜索 → 结果表（点行应用到回测）
class OptimizationPanel : public QWidget {
    Q_OBJECT

public:
    explicit OptimizationPanel(TencentProvider* provider, QWidget* parent = nullptr);

signals:
    /// 点中某行 → 应用该组参数到回测面板
    void applyParams(const QString& strategyId, const QVariantMap& params);

private slots:
    void onRunClicked();
    void onAllDataFetched();
    void onStrategyChanged();

private:
    void onResult(const std::vector<GridSearchResult>& results,
                  const QString& p1Name, const QString& p2Name);
    std::vector<StockCode> selectedSymbols() const;
    Objective currentObjective() const;
    void resetToIdle();

    TencentProvider* provider_ = nullptr;
    std::unique_ptr<DataCache> cache_;

    QComboBox* strategyCombo_ = nullptr;
    QLabel* p1Label_ = nullptr;
    QLabel* p2Label_ = nullptr;
    QSpinBox* p1From_ = nullptr;
    QSpinBox* p1To_ = nullptr;
    QSpinBox* p1Step_ = nullptr;
    QSpinBox* p2From_ = nullptr;
    QSpinBox* p2To_ = nullptr;
    QSpinBox* p2Step_ = nullptr;
    QComboBox* objectiveCombo_ = nullptr;
    QListWidget* stockList_ = nullptr;
    QDateEdit* startDate_ = nullptr;
    QDateEdit* endDate_ = nullptr;
    QDoubleSpinBox* capital_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableView* resultView_ = nullptr;
    GridSearchTableModel* resultModel_ = nullptr;

    bool running_ = false;
};

} // namespace st
