#pragma once

#include "foundation/stock_info.h"
#include "ui/models/pattern_table_model.h"
#include <QWidget>
#include <memory>
#include <vector>

class QPushButton;
class QProgressBar;
class QTableView;
class QSplitter;

namespace st {

class IDataProvider;
class DataCache;
class StockSearchBar;
class KLineChart;

/// 形态识别面板 — 搜索股票 → 检测 K 线形态 → 信号表 + 联动 K 线
///
/// 复用 StockSearchBar（代码/名称/拼音搜索）与 KLineChart（K 线联动）。
/// 数据流照 BacktestPanel 异步模式：IO 拉日K → Worker 检测 → 主线程填表。
class PatternPanel : public QWidget {
    Q_OBJECT

public:
    explicit PatternPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 双击结果行 → 主窗口打开 K 线
    void openChart(const StockCode& code);

private slots:
    void onStockSelected(const StockInfo& info);
    void onRunClicked();
    void onAllDataFetched(int gen);

private:
    void startDetect();
    void onResult(const std::vector<PatternTableModel::Row>& rows);
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    // shared_ptr：异步 lambda 按值捕获，面板销毁后 DataCache 仍存活
    std::shared_ptr<DataCache> cache_;
    StockCode code_;
    QString name_;
    int detectGen_ = 0;  // 世代守卫：快速切股时丢弃旧结果

    StockSearchBar* searchBar_ = nullptr;
    QPushButton* runBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QTableView* resultView_ = nullptr;
    PatternTableModel* resultModel_ = nullptr;
    KLineChart* chart_ = nullptr;

    bool running_ = false;
};

} // namespace st
