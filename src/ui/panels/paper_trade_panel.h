#pragma once

#include "foundation/stock_code.h"
#include <QWidget>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QTimer;
class QTableView;
class QPlainTextEdit;

namespace st {

class IDataProvider;
class PaperTradeEngine;
class TradeTableModel;
class IStrategy;
class TradeJournalEngine;
class StockPoolPicker;

/// 模拟交易面板 — 搜索多选股票（全市场）+ 策略 → 实时行情驱动 PaperTradeEngine（多股票）
class PaperTradePanel : public QWidget {
    Q_OBJECT

public:
    explicit PaperTradePanel(IDataProvider* provider, QWidget* parent = nullptr);
    ~PaperTradePanel() override;  // 停止轮询定时器，避免关闭窗口时触发异步任务

    /// 注入交易日志引擎（供 MainWindow 装配后，模拟成交自动落库）
    void setJournal(std::shared_ptr<TradeJournalEngine> journal);

private slots:
    void onToggleClicked();
    void onStrategyChanged();
    void onTimerTick();

private:
    void refreshStatus();
    std::shared_ptr<IStrategy> makeStrategy() const;
    std::vector<StockCode> selectedSymbols() const;

    IDataProvider* provider_ = nullptr;
    std::unique_ptr<PaperTradeEngine> engine_;
    std::shared_ptr<TradeJournalEngine> journal_;  // 模拟成交自动落库
    QTimer* timer_ = nullptr;

    StockPoolPicker* stockPicker_ = nullptr;
    QComboBox* strategyCombo_ = nullptr;
    QLabel* p1Label_ = nullptr;
    QLabel* p2Label_ = nullptr;
    QSpinBox* p1_ = nullptr;
    QSpinBox* p2_ = nullptr;
    QDoubleSpinBox* capital_ = nullptr;
    QDoubleSpinBox* slippage_ = nullptr;
    QPushButton* toggleBtn_ = nullptr;

    QLabel* cash_ = nullptr;
    QLabel* marketValue_ = nullptr;
    QLabel* totalAsset_ = nullptr;
    QLabel* todayPnl_ = nullptr;
    QLabel* posCount_ = nullptr;
    QLabel* stockCountLabel_ = nullptr;  // 已选股票数（运行中显示）
    QTableView* tradesView_ = nullptr;
    TradeTableModel* tradeModel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;

    bool running_ = false;
    bool refreshing_ = false;
    int gen_ = 0;
    size_t lastTradeCount_ = 0;
};

} // namespace st
