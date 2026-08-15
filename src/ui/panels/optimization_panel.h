#pragma once

#include "engine/optimizer/grid_search.h"
#include "engine/optimizer/grid_heatmap.h"
#include "ui/utils/progress_eta.h"
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
class QTabWidget;

namespace st {

class IDataProvider;
class DataCache;
class GridSearchTableModel;
class GridHeatmapWidget;
class StockPoolPicker;

/// 参数优化面板 — 两参数范围 + 目标函数 → 网格搜索 → 结果表/热力图（点行/双击格应用回测）
class OptimizationPanel : public QWidget {
    Q_OBJECT

public:
    explicit OptimizationPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 点中某行 → 应用该组参数到回测面板
    void applyParams(const QString& strategyId, const QVariantMap& params);

private slots:
    void onRunClicked();
    void onAllDataFetched();
    void onStrategyChanged();

private:
    void onResult(const std::vector<GridSearchResult>& results,
                  const QString& p1Name, const QString& p2Name,
                  const QString& p1Param, const QString& p2Param);
    std::vector<StockCode> selectedSymbols() const;
    Objective currentObjective() const;
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    // shared_ptr：异步 lambda 按值捕获，面板销毁后 DataCache 仍存活，避免悬垂写
    std::shared_ptr<DataCache> cache_;

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
    StockPoolPicker* stockPicker_ = nullptr;
    QDateEdit* startDate_ = nullptr;
    QDateEdit* endDate_ = nullptr;
    QDoubleSpinBox* capital_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableView* resultView_ = nullptr;
    GridSearchTableModel* resultModel_ = nullptr;
    QTabWidget* resultTabs_ = nullptr;
    GridHeatmapWidget* heatmap_ = nullptr;
    QLabel* resultInfoLabel_ = nullptr;  // 结果上下文：股票池/目标函数/日期区间
    QLabel* progressEtaLabel_ = nullptr; // 进度已用/预计剩余
    st::ui::ProgressEta eta_;            // 进度时间估算（主线程专用）
    QString lastP1Param_;  // 最近一次搜索的英文参数名（热力图点击应用用）
    QString lastP2Param_;

    bool running_ = false;
};

} // namespace st
