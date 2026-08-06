#pragma once

#include <QMainWindow>
#include <memory>

class QDockWidget;
class QToolBar;
class QLabel;
class QSettings;
class QStackedWidget;

namespace st {

class IDataProvider;
class LogPanel;
class MarketIndexStrip;
class StockSearchBar;
class MarketPanel;
class CentralChartWidget;
class ShortcutManager;
class QuantWindow;
class MarketDepthWidget;
class StockKeyDataWidget;
class ChipPanel;
class SectorPanel;

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
    void resetLayout();

    std::unique_ptr<IDataProvider> provider_;
    std::unique_ptr<ShortcutManager> shortcuts_;
    std::unique_ptr<QSettings> settings_;

    QDockWidget* logDock_ = nullptr;
    QuantWindow* quantWindow_ = nullptr;
    MarketDepthWidget* marketDepth_ = nullptr;
    StockKeyDataWidget* keyData_ = nullptr;
    ChipPanel* chipPanel_ = nullptr;
    LogPanel* logPanel_ = nullptr;
    MarketIndexStrip* indexStrip_ = nullptr;
    StockSearchBar* searchBar_ = nullptr;
    MarketPanel* marketPanel_ = nullptr;
    SectorPanel* sectorPanel_ = nullptr;
    CentralChartWidget* centralChart_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    QLabel* connLabel_ = nullptr;
};

} // namespace st
