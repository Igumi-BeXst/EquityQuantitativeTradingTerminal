#pragma once

#include "engine/analyzer/stress_test.h"
#include "engine/optimizer/grid_search.h"
#include "ui/utils/progress_eta.h"
#include "foundation/stock_code.h"
#include <QWidget>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QSpinBox;
class QDateEdit;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;
class QTableView;
class QPlainTextEdit;

namespace st {

class IDataProvider;
class DataCache;
class GridSearchTableModel;
class StockPoolPicker;
namespace advisor { struct AdvisorSuggestion; }

/// 优化建议面板 — 网格搜索 + 蒙特卡洛 → StrategyAdvisor 中文建议 + 精化网格
///
/// 照 OptimizationPanel 的两阶段异步模式跑真实网格回测，
/// 再用 StrategyAdvisor 给出参数建议（含过拟合/风险/整体不佳警告）。
class AdvisorPanel : public QWidget {
    Q_OBJECT

public:
    explicit AdvisorPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 应用建议参数到回测面板
    void applyParams(const QString& strategyId, const QVariantMap& params);

private slots:
    void onRunClicked();
    void onAllDataFetched();
    void onStrategyChanged();
    void onUseRefined();

private:
    void onResult(const std::vector<GridSearchResult>& results,
                  const st::advisor::AdvisorSuggestion& suggestion,
                  const std::vector<st::ParamRange>& refined,
                  const std::optional<StressTestOutput>& stress,
                  const QString& p1Name, const QString& p2Name);
    void displaySuggestion(const st::advisor::AdvisorSuggestion& s);
    void displayStress(const std::optional<StressTestOutput>& stress);
    void fillRefinedRanges(const std::vector<st::ParamRange>& ranges);
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
    QLabel* progressEtaLabel_ = nullptr; // 进度已用/预计剩余
    st::ui::ProgressEta eta_;            // 进度时间估算（主线程专用）

    // 建议区
    QLabel* advParams_ = nullptr;
    QLabel* advConfidence_ = nullptr;
    QLabel* advWarnings_ = nullptr;
    QLabel* advStress_ = nullptr;   // 压力测试各窗口最大回撤摘要
    QPlainTextEdit* advText_ = nullptr;
    QLabel* refinedText_ = nullptr;
    QPushButton* useRefinedBtn_ = nullptr;
    std::vector<st::ParamRange> refinedRanges_;

    QTableView* resultView_ = nullptr;
    GridSearchTableModel* resultModel_ = nullptr;

    bool running_ = false;
};

} // namespace st
