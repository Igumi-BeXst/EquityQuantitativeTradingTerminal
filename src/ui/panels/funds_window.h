#pragma once

#include "foundation/stock_code.h"
#include <QMainWindow>
#include <QString>
#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QTabWidget;
class QTableWidget;

namespace st {

class EastMoneyFundsProvider;
class IDataProvider;

/// 龙虎榜面板 — 交易日下拉（只列有数据的交易日，来自 TDX 上证指数日线日历）
/// → 当日榜单（净买额降序），双击开图
class FundsDragonTigerPanel : public QWidget {
    Q_OBJECT
public:
    FundsDragonTigerPanel(std::shared_ptr<EastMoneyFundsProvider> funds,
                          IDataProvider* provider, QWidget* parent = nullptr);

signals:
    void openChart(const StockCode& code, const QString& name);

private:
    void refresh();
    void populateTradingDates();

    std::shared_ptr<EastMoneyFundsProvider> funds_;
    IDataProvider* provider_ = nullptr;
    QComboBox* dateCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    int loadGen_ = 0;
    int dateGen_ = 0;
};

/// 融资融券面板 — 市场总览 + 当前股票两融明细（联动全局选股）
class FundsMarginPanel : public QWidget {
    Q_OBJECT
public:
    FundsMarginPanel(std::shared_ptr<EastMoneyFundsProvider> funds,
                     QWidget* parent = nullptr);

    /// 设置当前股票 → 异步刷新该股两融明细
    void setStock(const StockCode& code, const QString& name);

private:
    void refresh();

    std::shared_ptr<EastMoneyFundsProvider> funds_;
    StockCode code_;
    QString name_;
    QLabel* stockLabel_ = nullptr;
    QLabel* overview_ = nullptr;
    QTableWidget* table_ = nullptr;
    int loadGen_ = 0;
};

/// 资金数据窗口 — 顶部「资金」菜单打开（仿量化工作台独立窗口）
class FundsWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FundsWindow(IDataProvider* provider, QWidget* parent = nullptr);

    /// 主窗口选股联动 → 融资融券 tab 显示该股明细
    void setCurrentStock(const StockCode& code, const QString& name);

signals:
    void openChart(const StockCode& code, const QString& name);

private:
    IDataProvider* provider_ = nullptr;
    std::shared_ptr<EastMoneyFundsProvider> funds_;
    QTabWidget* tabs_ = nullptr;
    FundsDragonTigerPanel* dragonTiger_ = nullptr;
    FundsMarginPanel* margin_ = nullptr;
};

} // namespace st
