#pragma once

#include "foundation/stock_code.h"
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QTimer;
class QTableView;
class QPlainTextEdit;

namespace st {

class TencentProvider;
class PaperTradeEngine;
class TradeTableModel;
class IStrategy;

/// 模拟交易面板 — 单股票 + 策略 → 实时行情驱动 PaperTradeEngine
class PaperTradePanel : public QWidget {
    Q_OBJECT

public:
    explicit PaperTradePanel(TencentProvider* provider, QWidget* parent = nullptr);

private slots:
    void onToggleClicked();
    void onStrategyChanged();
    void onTimerTick();

private:
    void onStarted();
    void onQuotesReady();
    void refreshStatus();
    std::shared_ptr<IStrategy> makeStrategy() const;
    StockCode selectedCode() const;

    TencentProvider* provider_ = nullptr;
    std::unique_ptr<PaperTradeEngine> engine_;
    QTimer* timer_ = nullptr;

    QComboBox* stockCombo_ = nullptr;
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
    QTableView* tradesView_ = nullptr;
    TradeTableModel* tradeModel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;

    bool running_ = false;
    bool refreshing_ = false;
    int gen_ = 0;
    size_t lastTradeCount_ = 0;
};

} // namespace st
