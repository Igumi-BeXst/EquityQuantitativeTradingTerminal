#pragma once

#include "foundation/stock_code.h"
#include "engine/screener/factor.h"
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

/// 选股面板 — 多因子勾选 + 股票池 + topN → 异步选股 → 结果表（双击开图）
class ScreenerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ScreenerPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 双击选股结果行 → 打开 K 线图
    void openChart(const StockCode& code);

private slots:
    void onRunClicked();
    void onAllDataFetched();

private:
    void onResult(const std::vector<ScreenResult>& results,
                  const std::vector<std::string>& factorNames);
    std::vector<StockCode> selectedSymbols() const;
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    // shared_ptr：异步 lambda 按值捕获，面板销毁后 DataCache 仍存活
    std::shared_ptr<DataCache> cache_;

    // 候选因子（默认权重集）
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> candidateFactors_;
    std::vector<QCheckBox*> factorChecks_;

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
