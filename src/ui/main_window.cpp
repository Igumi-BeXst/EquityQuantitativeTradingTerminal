#include "ui/main_window.h"
#include "ui/theme_manager.h"
#include "ui/shortcut_manager.h"
#include "ui/preferences_dialog.h"
#include "ui/widgets/market_index_strip.h"
#include "ui/widgets/central_chart_widget.h"
#include "ui/panels/log_panel.h"
#include "ui/panels/stock_search_bar.h"
#include "ui/panels/market_panel.h"
#include "ui/panels/strategy_panel.h"
#include "ui/panels/backtest_panel.h"
#include "ui/panels/screener_panel.h"
#include "ui/panels/paper_trade_panel.h"
#include "ui/panels/optimization_panel.h"
#include "ui/panels/strategy_compare_panel.h"
#include "ui/panels/stress_test_panel.h"
#include "data/idata_provider.h"
#include "data/provider_factory.h"
#include "core/app_paths.h"
#include "core/config_manager.h"
#include "core/log_manager.h"
#include "core/event_bus.h"
#include "foundation/stock_info.h"
#include <QDockWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QSettings>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QApplication>
#include <QCloseEvent>
#include <QKeySequence>

namespace st {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    initServices();

    createCentral();
    createDocks();
    createMenus();
    createToolbar();
    createStatusBar();
    registerShortcuts();

    // 恢复窗口几何与 dock 布局
    settings_ = std::make_unique<QSettings>(
        QString::fromStdString(AppPaths::configDir() + "/stock_terminal.ini"),
        QSettings::IniFormat);
    restoreGeometry(settings_->value(QStringLiteral("ui/geometry")).toByteArray());
    restoreState(settings_->value(QStringLiteral("ui/state")).toByteArray());

    // 搜索/指数点击 → 中央图表打开对应标的
    connect(searchBar_, &StockSearchBar::stockSelected, this,
            [this](const StockInfo& info) {
        centralStack_->setCurrentWidget(centralChart_);
        centralChart_->loadStock(info.code, QString::fromStdString(info.name));
        statusBar()->showMessage(
            QStringLiteral("已选择 %1 (%2)")
                .arg(QString::fromStdString(info.name),
                     QString::fromStdString(info.code.displayCode())), 5000);
        LogManager::instance()->log(LogLevel::Info, "选择股票: {} ({})",
                                    info.name, info.code.fullCode());
    });
    connect(indexStrip_, &MarketIndexStrip::indexClicked, this,
            [this](const StockCode& code) {
        centralStack_->setCurrentWidget(centralChart_);
        centralChart_->loadStock(code, QString::fromStdString(code.displayCode()));
        statusBar()->showMessage(
            QStringLiteral("打开指数 %1 K线图")
                .arg(QString::fromStdString(code.fullCode())), 5000);
    });
}

MainWindow::~MainWindow() {
    if (provider_) provider_->disconnect();
}

void MainWindow::initServices() {
    // 1. 用户目录 + 日志 + 配置
    AppPaths::ensureDirectories();
    LogManager::instance()->init(AppPaths::logDir() + "/stock_terminal.log");

    auto* cfg = ConfigManager::instance();
    cfg->load(AppPaths::configDir() + "/default.json");
    // 确保默认配置键存在（首启或迁移）
    cfg->set("ui.theme", cfg->get<std::string>("ui.theme", "dark"));
    cfg->set("ui.shortcuts.focusSearch", cfg->get<std::string>("ui.shortcuts.focusSearch", "Ctrl+Space"));
    cfg->set("ui.shortcuts.focusLog", cfg->get<std::string>("ui.shortcuts.focusLog", "Ctrl+L"));
    cfg->set("ui.shortcuts.refreshQuotes", cfg->get<std::string>("ui.shortcuts.refreshQuotes", "F5"));
    cfg->set("ui.shortcuts.settings", cfg->get<std::string>("ui.shortcuts.settings", "Ctrl+,"));
    cfg->save();

    // 2. 数据源（主线程亲和，供指数条/搜索栏使用）
    provider_ = makeDataProvider();
    provider_->connect();

    shortcuts_ = std::make_unique<ShortcutManager>();
}

void MainWindow::createCentral() {
    centralStack_ = new QStackedWidget(this);

    // 页 0: 欢迎
    auto* welcome = new QWidget(centralStack_);
    auto* layout = new QVBoxLayout(welcome);
    auto* title = new QLabel(QStringLiteral("StockTerminal"));
    title->setObjectName(QStringLiteral("welcomeTitle"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size:32px; font-weight:bold;"));
    auto* hint = new QLabel(tr("顶部搜索股票开始使用\n或点击顶部指数查看 K 线图"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QStringLiteral("color:#888888;"));
    layout->addStretch();
    layout->addWidget(title);
    layout->addWidget(hint);
    layout->addStretch();
    centralStack_->addWidget(welcome);

    // 页 1: 图表（分时 ↔ K线 + 周期栏）
    centralChart_ = new CentralChartWidget(provider_.get(), centralStack_);
    centralStack_->addWidget(centralChart_);

    setCentralWidget(centralStack_);
}

void MainWindow::createDocks() {
    setDockOptions(QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks);

    // 左: 市场面板（涨幅/跌幅榜 + 市场宽度）
    auto* marketDock = new QDockWidget(tr("市场"), this);
    marketDock->setObjectName(QStringLiteral("marketDock"));
    marketPanel_ = new MarketPanel(provider_.get(), marketDock);
    marketDock->setWidget(marketPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, marketDock);
    connect(marketPanel_, &MarketPanel::openChart, this, [this](const StockCode& code) {
        centralStack_->setCurrentWidget(centralChart_);
        centralChart_->loadStock(code, QString::fromStdString(code.displayCode()));
    });

    // 右: 策略 + 回测（tabify）
    auto* strategyDock = new QDockWidget(tr("策略"), this);
    strategyDock->setObjectName(QStringLiteral("strategyDock"));
    auto* strategyPanel = new StrategyPanel(strategyDock);
    strategyDock->setWidget(strategyPanel);
    addDockWidget(Qt::RightDockWidgetArea, strategyDock);
    connect(strategyPanel, &StrategyPanel::applyStrategy, this,
            [this](const QString& id, const QVariantMap& params) {
        if (backtestDock_) {
            backtestDock_->raise();
            backtestDock_->activateWindow();
        }
        if (auto* bt = findChild<BacktestPanel*>()) {
            bt->loadStrategy(id, params);
        }
    });

    backtestDock_ = new QDockWidget(tr("回测"), this);
    backtestDock_->setObjectName(QStringLiteral("backtestDock"));
    backtestDock_->setWidget(new BacktestPanel(provider_.get(), backtestDock_));
    tabifyDockWidget(strategyDock, backtestDock_);

    // 右: 量化面板（选股/模拟交易/参数优化/策略对比/压力测试 一个 dock 内嵌标签页）
    quantDock_ = new QDockWidget(tr("量化"), this);
    quantDock_->setObjectName(QStringLiteral("quantDock"));
    quantTabs_ = new QTabWidget(quantDock_);
    screenerPanel_ = new ScreenerPanel(provider_.get(), quantTabs_);
    paperTradePanel_ = new PaperTradePanel(provider_.get(), quantTabs_);
    optimizationPanel_ = new OptimizationPanel(provider_.get(), quantTabs_);
    strategyComparePanel_ = new StrategyComparePanel(provider_.get(), quantTabs_);
    stressTestPanel_ = new StressTestPanel(provider_.get(), quantTabs_);
    quantTabs_->addTab(screenerPanel_, tr("选股"));
    quantTabs_->addTab(paperTradePanel_, tr("模拟交易"));
    quantTabs_->addTab(optimizationPanel_, tr("参数优化"));
    quantTabs_->addTab(strategyComparePanel_, tr("策略对比"));
    quantTabs_->addTab(stressTestPanel_, tr("压力测试"));
    quantDock_->setWidget(quantTabs_);
    tabifyDockWidget(backtestDock_, quantDock_);

    // 选股双击 → 中央开图
    connect(screenerPanel_, &ScreenerPanel::openChart, this, [this](const StockCode& code) {
        centralStack_->setCurrentWidget(centralChart_);
        centralChart_->loadStock(code, QString::fromStdString(code.displayCode()));
    });
    // 参数优化点行 → 应用到回测面板
    connect(optimizationPanel_, &OptimizationPanel::applyParams, this,
            [this](const QString& id, const QVariantMap& params) {
        if (backtestDock_) {
            backtestDock_->raise();
            backtestDock_->activateWindow();
        }
        if (auto* bt = findChild<BacktestPanel*>()) {
            bt->loadStrategy(id, params);
        }
    });

    // 底: 日志面板（真实实现）
    logDock_ = new QDockWidget(tr("日志"), this);
    logDock_->setObjectName(QStringLiteral("logDock"));
    logPanel_ = new LogPanel(logDock_);
    logDock_->setWidget(logPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);
    logDock_->setMinimumHeight(140);
}

void MainWindow::createMenus() {
    // 文件
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    // 视图
    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("切换主题(&T)"), this, [this] {
        ThemeManager::toggle();
        statusBar()->showMessage(
            QStringLiteral("已切换主题: %1")
                .arg(ThemeManager::themeName(ThemeManager::current())), 3000);
    });
    viewMenu->addAction(tr("重置布局(&R)"), this, &MainWindow::resetLayout);

    // 设置
    auto* settingsMenu = menuBar()->addMenu(tr("设置(&S)"));
    settingsMenu->addAction(tr("偏好设置(&P)…"), this, &MainWindow::openPreferences);

    // 帮助
    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于(&A)"), this, [this] {
        QMessageBox::about(this, tr("关于 StockTerminal"),
            QStringLiteral("<h3>StockTerminal</h3>"
                           "<p>量化交易工作站 · 版本 0.1.0</p>"
                           "<p>数据源: %1</p>"
                           "<p>纯本地单机，零遥测。</p>")
                .arg(QString::fromStdString(
                    provider_ ? provider_->providerName() : "unknown")));
    });
}

void MainWindow::createToolbar() {
    auto* toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // 全局搜索栏
    searchBar_ = new StockSearchBar(provider_.get(), this);
    searchBar_->setFixedWidth(300);
    toolbar->addWidget(searchBar_);

    // 顶部指数条
    indexStrip_ = new MarketIndexStrip(provider_.get(), this);
    toolbar->addSeparator();
    toolbar->addWidget(indexStrip_);
    toolbar->addSeparator();

    // 刷新按钮（F5）
    auto* refreshAction = toolbar->addAction(tr("刷新行情"));
    connect(refreshAction, &QAction::triggered, this, [this] {
        provider_->refreshQuotes();
    });
}

void MainWindow::createStatusBar() {
    auto* sb = statusBar();
    auto* sourceLabel = new QLabel(
        tr("数据源: %1").arg(QString::fromStdString(
            provider_ ? provider_->providerName() : "unknown")), this);
    connLabel_ = new QLabel(tr("未连接"), this);
    connLabel_->setStyleSheet(QStringLiteral("color:#fb8c00;"));

    sb->addPermanentWidget(sourceLabel);
    sb->addPermanentWidget(connLabel_);
    sb->showMessage(tr("就绪"));

    if (provider_ && provider_->isConnected()) {
        connLabel_->setText(tr("已连接"));
        connLabel_->setStyleSheet(QStringLiteral("color:#43A047;"));
    }
}

void MainWindow::registerShortcuts() {
    shortcuts_->registerAction(QStringLiteral("focusSearch"),
        QKeySequence(QStringLiteral("Ctrl+Space")),
        this, [this] { if (searchBar_) searchBar_->focusEdit(); });

    shortcuts_->registerAction(QStringLiteral("focusLog"),
        QKeySequence(QStringLiteral("Ctrl+L")),
        this, [this] {
            if (logDock_) { logDock_->raise(); logDock_->activateWindow(); }
            if (logPanel_) logPanel_->setFocus();
        });

    shortcuts_->registerAction(QStringLiteral("refreshQuotes"),
        QKeySequence(QStringLiteral("F5")),
        this, [this] { if (provider_) provider_->refreshQuotes(); });

    shortcuts_->registerAction(QStringLiteral("settings"),
        QKeySequence(QStringLiteral("Ctrl+,")),
        this, [this] { openPreferences(); });
}

void MainWindow::openPreferences() {
    PreferencesDialog dlg(this);
    dlg.exec();
}

void MainWindow::resetLayout() {
    settings_->remove(QStringLiteral("ui/state"));
    restoreState(QByteArray());
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        dock->show();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // 持久化窗口几何 + dock 布局
    settings_->setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings_->setValue(QStringLiteral("ui/state"), saveState());
    settings_->sync();

    ConfigManager::instance()->save();
    LogManager::instance()->flush();
    if (provider_) provider_->disconnect();

    QMainWindow::closeEvent(event);
}

} // namespace st

#include "moc_main_window.cpp"
