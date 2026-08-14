#pragma once

#include "foundation/stock_code.h"
#include "engine/screener/factor.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <QWidget>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QListWidget;
class QSpinBox;
class QDateEdit;
class QPushButton;
class QProgressBar;
class QTableView;

namespace st {

class IDataProvider;
class DataCache;
class ScreenResultModel;

/// 选股面板 — 多因子勾选 + AI 因子（形态/情绪）+ 股票池 + topN → 异步选股 → 结果表
///
/// AI 因子：勾选后 worker 额外跑 runAiScreener（形态+情绪+技术），
/// 结果表显示「AI 分」列并按 AI 分降序；情绪数据 IO 阶段限量拉取（池前 30 只）。
class ScreenerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScreenerPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 双击选股结果行 → 打开 K 线图
    void openChart(const StockCode& code);

private slots:
    void onRunClicked();
    void onAllDataFetched(std::vector<std::optional<st::sentiment::SentimentScore>> sentiments);

private:
    void onResult(const std::vector<ScreenResult>& results,
                  const std::vector<std::string>& factorNames,
                  const std::vector<double>& aiScores);
    std::vector<StockCode> selectedSymbols() const;
    bool aiEnabled() const;   // 形态/情绪任一勾选
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    // shared_ptr：异步 lambda 按值捕获，面板销毁后 DataCache 仍存活
    std::shared_ptr<DataCache> cache_;
    std::shared_ptr<st::sentiment::ISentimentProvider> newsProvider_;  // 东财资讯（情绪分项）

    // 候选因子（默认权重集）
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> candidateFactors_;
    std::vector<QCheckBox*> factorChecks_;

    // AI 因子配置
    QCheckBox* aiPatternCheck_ = nullptr;    // 形态
    QCheckBox* aiSentimentCheck_ = nullptr;  // 情绪

    QListWidget* stockList_ = nullptr;
    QSpinBox* topN_ = nullptr;
    QSpinBox* lookback_ = nullptr;
    QDateEdit* endDate_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableView* resultView_ = nullptr;
    ScreenResultModel* resultModel_ = nullptr;

    bool running_ = false;
};

} // namespace st
