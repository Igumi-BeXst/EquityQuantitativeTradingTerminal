#pragma once

#include "foundation/stock_code.h"
#include "engine/screener/factor.h"
#include "data/quote_fundamentals.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <QWidget>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QSpinBox;
class QDateEdit;
class QPushButton;
class QProgressBar;
class QTableView;

namespace st {

class IDataProvider;
class DataCache;
class ScreenResultModel;
class StockPoolPicker;

/// 选股面板 — 三分组因子（Alpha191 / 量价 / 基本面）+ 综合评分分项 + 股票池 + topN
/// → 异步选股 → 结果表
///
/// 因子打分：引擎对全池做截面标准化（MAD 去极值 → z-score → 方向 → Φ(z)×100），
/// 三组各自总权重 1.0（组内等权），结果表列头显示因子中文名。
///
/// 综合评分：勾选分项后 worker 额外跑 runCompositeScreener（K线形态+舆情情绪+技术指标），
/// 结果表显示「综合评分」列并按综合评分降序重排结果（不改变入选股票）；技术指标恒参与；
/// 舆情数据 IO 阶段按股票池顺序限量拉取（前 30 只），其余缺失分项按权重折减。
class ScreenerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScreenerPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 双击选股结果行 → 打开 K 线图
    void openChart(const StockCode& code);

private slots:
    void onRunClicked();
    void onAllDataFetched(std::vector<std::optional<st::sentiment::SentimentScore>> sentiments,
                          std::vector<st::QuoteFundamentals> quotes,
                          std::vector<Bar> benchmarkBars);

private:
    void onResult(const std::vector<ScreenResult>& results,
                  const std::vector<std::string>& factorNames,
                  const std::vector<double>& compositeScores);
    std::vector<StockCode> selectedSymbols() const;
    bool compositeEnabled() const;   // 形态/情绪任一勾选
    bool anyFactorChecked() const;   // 因子区是否有勾选项
    void updateRunButton();          // 未勾选因子/运行中 → 禁用开始按钮
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    // shared_ptr：异步 lambda 按值捕获，面板销毁后 DataCache 仍存活
    std::shared_ptr<DataCache> cache_;
    std::shared_ptr<st::sentiment::ISentimentProvider> newsProvider_;  // 东财资讯（情绪分项）

    // 候选因子（默认权重集）
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> candidateFactors_;
    std::vector<QCheckBox*> factorChecks_;

    // 综合评分配置
    QCheckBox* compositePatternCheck_ = nullptr;    // K线形态分项（近 3 根 K 线）
    QCheckBox* compositeSentimentCheck_ = nullptr;  // 舆情情绪分项（东财新闻）

    StockPoolPicker* stockPicker_ = nullptr;
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
