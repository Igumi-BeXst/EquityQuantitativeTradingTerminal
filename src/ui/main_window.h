#pragma once

#include <QMainWindow>
#include <QPointer>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "foundation/stock_code.h"

class QDockWidget;
class QToolBar;
class QLabel;
class QSettings;
class QStackedWidget;

namespace st {

class IDataProvider;
class MarketIndexStrip;
class CustomIndexPanel;
class FundsWindow;
class JournalWindow;
class TradeJournalEngine;
class StockSearchBar;
class MarketPanel;
class WatchlistPanel;
class CentralChartWidget;
class ChartWindow;
class ShortcutManager;
class QuantWindow;
class MarketDepthWidget;
class StockKeyDataWidget;
class ChipPanel;
class TaskScheduler;
class TaskWindow;
struct ScheduledTask;

/// 主窗口 — 菜单栏 + 工具栏(搜索/指数条) + QDockWidget 布局 + 状态栏
///
/// 服务装配: AppPaths/LogManager/ConfigManager/IDataProvider 初始化，
/// 快捷键注册，dock 布局持久化（QSettings 到 %APPDATA%）。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void initServices();
    void createCentral();
    void createDocks();
    void createMenus();
    void createToolbar();
    void createStatusBar();
    void registerShortcuts();
    void openPreferences();
    void openQuantWindow();
    void openFundsWindow();
    void openJournalWindow();
    void openTaskWindow();
    void resetLayout();
    void runScheduledTask(ScheduledTask& t);
    /// 交易日志变更 → 重算当前股票交易标记并注入中央图表（可能从 IO 线程触发，需 marshal 主线程）
    void refreshTradeMarks();
    /// 打开新图表窗口（多实例并存；关窗即销毁）
    void openNewChartWindow(const StockCode& code, const QString& name);
    void openStockChart(const StockCode& code, const QString& name);  // 统一开图（股票：含右侧面板）
    void syncWatchlistButton();  // 图表「加入自选」按钮状态 = 当前股票是否在自选
    void openMarketWindow();  // 市场窗口（独立面板，视图→市场 打开）

    std::unique_ptr<IDataProvider> provider_;
    std::unique_ptr<ShortcutManager> shortcuts_;
    std::unique_ptr<QSettings> settings_;

    QDockWidget* chipDock_ = nullptr;  // 筹码分布（默认收起，视图菜单按需打开）
    QDockWidget* customIndexDock_ = nullptr;  // 自定义指数（与板块 tab 并列）
    QDockWidget* watchlistDock_ = nullptr;   // 自选股 dock（左主位）
    class WatchlistPanel* watchlistPanel_ = nullptr;
    QMainWindow* marketWindow_ = nullptr;  // 市场窗口（独立面板，视图→市场 打开）
    QuantWindow* quantWindow_ = nullptr;
    FundsWindow* fundsWindow_ = nullptr;  // 资金数据（顶部「资金」菜单打开）
    JournalWindow* journalWindow_ = nullptr;  // 交易日志（顶部「日志」菜单打开）
    TaskWindow* taskWindow_ = nullptr;  // 定时任务（设置→定时任务）
    std::shared_ptr<TradeJournalEngine> journal_;  // 交易日志引擎（共享给 JournalWindow/QuantWindow/PaperTradePanel）
    std::shared_ptr<TaskScheduler> scheduler_;  // 定时任务调度器
    MarketDepthWidget* marketDepth_ = nullptr;
    StockKeyDataWidget* keyData_ = nullptr;
    ChipPanel* chipPanel_ = nullptr;
    CustomIndexPanel* customIndexPanel_ = nullptr;
    MarketIndexStrip* indexStrip_ = nullptr;
    StockSearchBar* searchBar_ = nullptr;
    MarketPanel* marketPanel_ = nullptr;
    CentralChartWidget* centralChart_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    std::vector<QPointer<ChartWindow>> chartWindows_;   // 独立图表窗口（QPointer 自动置空）
    QLabel* connLabel_ = nullptr;

    std::vector<StockCode> lastScreenerConfig_;   // 上次手动选股池（scope=last 复用）
    std::set<std::string> runningAsync_;          // 异步任务防重入（id → 执行中）
};

} // namespace st
