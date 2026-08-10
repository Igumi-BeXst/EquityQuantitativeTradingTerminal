#pragma once

#include "foundation/stock_code.h"
#include "foundation/enums.h"
#include "foundation/types.h"
#include "engine/backtest/fee_calculator.h"
#include <QDialog>
#include <QMainWindow>
#include <QString>
#include <memory>

class QTabWidget;
class QTableWidget;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QDateTimeEdit;

namespace st {
class EquityCurveWidget;
}

namespace st {

class TradeJournalEngine;
class IDataProvider;
struct JournalEntry;
class StockSearchBar;
struct StockInfo;

/// 新建/编辑交易记录对话框
/// StockSearchBar 搜代码/名称 + 方向 + 价格/数量 + 自动算费用（可覆盖）
class JournalEntryDialog : public QDialog {
    Q_OBJECT
public:
    explicit JournalEntryDialog(const FeeConfig& fees,
                                IDataProvider* provider = nullptr,
                                QWidget* parent = nullptr);

    /// 编辑模式：回填原行数据
    void setEntry(const JournalEntry& entry);

    /// 获取用户填写的结果（accept 后调用）
    JournalEntry entry() const;

private slots:
    void onStockSelected(const StockInfo& info);
    void recalcFees();

private:
    StockSearchBar* searchBar_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QDoubleSpinBox* priceSpin_ = nullptr;
    QSpinBox* volumeSpin_ = nullptr;
    QDoubleSpinBox* feesSpin_ = nullptr;
    QLineEdit* strategyEdit_ = nullptr;
    QLineEdit* noteEdit_ = nullptr;
    QDateTimeEdit* timeEdit_ = nullptr;   // 成交时间（默认当前时间，可改真实成交时刻）

    StockCode code_;
    QString name_;
    FeeConfig feesConfig_;
    std::string editId_;     // 非空 = 编辑模式
};

/// 费率设置对话框 — 四行 QDoubleSpinBox（佣金/最低佣金/印花税/过户费）
class JournalFeeDialog : public QDialog {
    Q_OBJECT
public:
    explicit JournalFeeDialog(const FeeConfig& fees, QWidget* parent = nullptr);
    /// 获取用户设置的费率（accept 后调用）
    FeeConfig feeConfig() const;
private:
    QDoubleSpinBox* commissionRateSpin_ = nullptr;
    QDoubleSpinBox* minCommissionSpin_ = nullptr;
    QDoubleSpinBox* stampTaxRateSpin_ = nullptr;
    QDoubleSpinBox* transferFeeRateSpin_ = nullptr;
};

/// 交易日志窗口 — 顶部「日志」菜单打开（仿资金窗口独立窗口）
/// tab1 交易记录（CRUD + 筛选），tab2 对比回顾（Task 7 填）
class JournalWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit JournalWindow(std::shared_ptr<TradeJournalEngine> journal,
                           IDataProvider* provider,
                           QWidget* parent = nullptr);
    ~JournalWindow() override;

signals:
    void openChart(const StockCode& code, const QString& name);

private:
    void rebuildAll();          // 重读引擎 → 刷两个 tab
    void refreshStats();        // computeStats → 填对比回顾 tab

    std::shared_ptr<TradeJournalEngine> journal_;
    IDataProvider* provider_ = nullptr;   // 供录入对话框的股票搜索栏使用
    QTabWidget* tabs_ = nullptr;
    QTableWidget* recordsTable_ = nullptr;
    QLineEdit* filter_ = nullptr;
    // tab2「对比回顾」控件
    QLabel* overallLabel_ = nullptr;
    QLabel* simLabel_ = nullptr;
    QLabel* manualLabel_ = nullptr;
    EquityCurveWidget* curve_ = nullptr;
    QTableWidget* pairTable_ = nullptr;
    QTableWidget* monthlyTable_ = nullptr;
    QTableWidget* realizedTable_ = nullptr;
    QTableWidget* strategyTable_ = nullptr;
};

} // namespace st
