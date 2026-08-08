#include "ui/main_window.h"
#include "ui/theme_manager.h"
#include "ui/shortcut_manager.h"
#include "ui/preferences_dialog.h"
#include "ui/widgets/market_index_strip.h"
#include "ui/widgets/central_chart_widget.h"
#include "ui/panels/stock_search_bar.h"
#include "ui/panels/market_panel.h"
#include "ui/panels/sector_panel.h"
#include "ui/panels/quant_window.h"
#include "ui/panels/funds_window.h"
#include "ui/panels/journal_window.h"
#include "engine/journal/trade_journal.h"
#include "engine/journal/trade_journal_store.h"
#include "ui/widgets/market_depth_widget.h"
#include "ui/widgets/stock_key_data_widget.h"
#include "ui/widgets/chip_panel.h"
#include "ui/widgets/custom_index_panel.h"
#include "data/idata_provider.h"
#include "data/provider_factory.h"
#include "core/app_paths.h"
#include "core/thread_pool.h"
#include "core/config_manager.h"
#include "core/log_manager.h"
#include "core/event_bus.h"
#include "foundation/stock_info.h"
#include <QDockWidget>
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
#include <QDir>
#include <QDateTime>
#include <QFileDialog>

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
    // 筹码分布默认收起（不随历史布局常驻），需要时 视图→筹码分布 打开
    if (chipDock_) chipDock_->hide();

    // 搜索/指数点击 → 中央图表打开对应标的
    connect(searchBar_, &StockSearchBar::stockSelected, this,
            [this](const StockInfo& info) {
        centralStack_->setCurrentWidget(centralChart_);
        centralChart_->loadStock(info.code, QString::fromStdString(info.name));
        if (marketDepth_) {
            marketDepth_->setStock(info.code, QString::fromStdString(info.name));
        }
        if (keyData_) {
            keyData_->setStock(info.code, QString::fromStdString(info.name));
        }
        if (chipPanel_) {
            chipPanel_->setStock(info.code, QString::fromStdString(info.name));
        }
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
    // 关闭前排空在途异步任务：面板/图表的 IO/worker 任务持有 provider_ 裸指针，
    // 若在 provider_ 释放后再执行 → use-after-free → 关闭时堆损坏。
    // 事件循环已停止，不会再提交新任务，waitForDone 只等待已在运行/排队的最长任务。
    ThreadPool::ioPool()->waitForDone();
    ThreadPool::workerPool()->waitForDone();
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
    cfg->set("ui.shortcuts.refreshQuotes", cfg->get<std::string>("ui.shortcuts.refreshQuotes", "F5"));
    cfg->set("ui.shortcuts.settings", cfg->get<std::string>("ui.shortcuts.settings", "Ctrl+,"));
    cfg->save();

    // 2. 数据源（主线程亲和，供指数条/搜索栏使用）
    provider_ = makeDataProvider();
    provider_->connect();

    shortcuts_ = std::make_unique<ShortcutManager>();

    // 3. 交易日志引擎 + 持久化加载
    journal_ = std::make_shared<TradeJournalEngine>();
    TradeJournalStore store;
    store.load(AppPaths::configDir() + "/trade_journal.json", *journal_);
    // 费率配置（独立文件，Task 9 费率设置对话框读写此文件）
    auto feeCfg = TradeJournalStore::loadFeeConfig(AppPaths::configDir() + "/journal_config.json");
    journal_->setFees(feeCfg);
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
    connect(marketPanel_, &MarketPanel::openChart, this,
            [this](const StockCode& code, const QString& name) {
        centralStack_->setCurrentWidget(centralChart_);
        centralChart_->loadStock(code, name.isEmpty()
            ? QString::fromStdString(code.displayCode()) : name);
        if (marketDepth_) marketDepth_->setStock(code, name);
        if (keyData_) keyData_->setStock(code, name);
        if (chipPanel_) chipPanel_->setStock(code, name);
    });

    // 左: 板块前十榜单（市场面板下方；行业/概念涨跌幅 Top10 简单列表）
    // [BISECT] 定位关闭堆损坏时曾临时禁用；根因实为陈旧对象/ABI 错位（全量重建已修复），面板本身无问题。
    auto* sectorDock = new QDockWidget(tr("板块"), this);
    sectorDock->setObjectName(QStringLiteral("sectorDock"));
    sectorPanel_ = new SectorPanel(sectorDock);
    sectorDock->setWidget(sectorPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, sectorDock);
    splitDockWidget(marketDock, sectorDock, Qt::Vertical);
    sectorDock->setMinimumWidth(260);

    // 左: 自定义指数（与板块 tab 并列；建/编/删 + 实时点位 + 打开图表）
    customIndexDock_ = new QDockWidget(tr("自定义指数"), this);
    customIndexDock_->setObjectName(QStringLiteral("customIndexDock"));
    customIndexPanel_ = new CustomIndexPanel(provider_.get(), customIndexDock_);
    customIndexDock_->setWidget(customIndexPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, customIndexDock_);
    tabifyDockWidget(sectorDock, customIndexDock_);  // 与板块同 tab
    customIndexDock_->setMinimumWidth(260);
    connect(customIndexPanel_, &CustomIndexPanel::openChart, this,
            [this](const CustomIndex& idx) {
                centralStack_->setCurrentWidget(centralChart_);
                centralChart_->loadCustomIndex(idx);
            });

    // 右: 个股关键数据（盘口上方）+ 盘口五档 + 成交明细
    auto* keyDataDock = new QDockWidget(tr("个股关键数据"), this);
    keyDataDock->setObjectName(QStringLiteral("keyDataDock"));
    keyData_ = new StockKeyDataWidget(provider_.get(), keyDataDock);
    keyDataDock->setWidget(keyData_);
    addDockWidget(Qt::RightDockWidgetArea, keyDataDock);

    auto* depthDock = new QDockWidget(tr("盘口"), this);
    depthDock->setObjectName(QStringLiteral("marketDepthDock"));
    marketDepth_ = new MarketDepthWidget(provider_.get(), depthDock);
    depthDock->setWidget(marketDepth_);
    addDockWidget(Qt::RightDockWidgetArea, depthDock);
    splitDockWidget(keyDataDock, depthDock, Qt::Vertical);  // 关键数据在上，盘口在下
    depthDock->setMinimumWidth(260);
    depthDock->setMaximumWidth(400);

    // 右: 筹码分布（盘口下方；筹码云 + 成交分布 + 区间统计）
    // 默认收起，需要时经 视图→筹码分布 打开（chipDock_ 成员供菜单 toggle）。
    chipDock_ = new QDockWidget(tr("筹码分布"), this);
    chipDock_->setObjectName(QStringLiteral("chipDock"));
    chipPanel_ = new ChipPanel(provider_.get(), chipDock_);
    chipDock_->setWidget(chipPanel_);
    addDockWidget(Qt::RightDockWidgetArea, chipDock_);
    splitDockWidget(depthDock, chipDock_, Qt::Vertical);
    chipDock_->setMinimumWidth(260);
    chipDock_->setMaximumWidth(400);

    // 中央图表周期栏「筹码分布」按钮（叠加对比旁）↔ 本 Dock 联动
    connect(centralChart_, &CentralChartWidget::chipDockToggled, this, [this] {
        if (chipDock_) chipDock_->toggleViewAction()->trigger();
    });
    connect(chipDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (centralChart_) centralChart_->setChipButtonChecked(visible);
    });

    // K线十字光标日期（日/周/月）→ 筹码面板按日期查询；nullopt 回退最新
    connect(centralChart_, &CentralChartWidget::crosshairDateChanged, this,
            [this](const std::optional<DateTime>& date) {
                if (chipPanel_) chipPanel_->setAsOfDate(date);
            });

}

void MainWindow::createMenus() {
    // 文件
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("截图当前图表(&S)…"), this, [this] {
        if (!centralChart_) return;
        const QString dir = QString::fromStdString(AppPaths::dataDir() + "/screenshots");
        QDir().mkpath(dir);
        const QString code = centralChart_->currentCode().isValid()
            ? QString::fromStdString(centralChart_->currentCode().displayCode())
            : QStringLiteral("chart");
        const QString defaultPath = dir + "/screenshot_" + code + "_" +
            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
        // 保存对话框：用户自己选位置（默认 screenshots 目录），避免找不到
        const QString path = QFileDialog::getSaveFileName(
            this, tr("保存截图"), defaultPath, tr("PNG 图片 (*.png)"));
        if (path.isEmpty()) return;
        const QPixmap pm = centralChart_->grab();
        if (pm.save(path, "PNG")) {
            statusBar()->showMessage(tr("截图已保存: %1").arg(path), 5000);
            LogManager::instance()->log(LogLevel::Info, "截图: {}", path.toStdString());
        } else {
            statusBar()->showMessage(tr("截图保存失败"), 3000);
        }
    });
    fileMenu->addAction(tr("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    // 视图
    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("切换主题(&T)"), this, [this] {
        ThemeManager::toggle();
        statusBar()->showMessage(
            QStringLiteral("已切换主题: %1")
                .arg(ThemeManager::themeName(ThemeManager::current())), 3000);
    });
    // 筹码分布不进视图菜单（仍可通过图表「筹码分布」按钮开关）
    if (customIndexDock_) viewMenu->addAction(customIndexDock_->toggleViewAction());
    viewMenu->addAction(tr("重置布局(&R)"), this, &MainWindow::resetLayout);

    // 量化
    auto* quantMenu = menuBar()->addMenu(tr("量化(&Q)"));
    quantMenu->addAction(tr("量化工作台(&W)"), this, &MainWindow::openQuantWindow);

    // 资金（龙虎榜/北向资金/融资融券）
    auto* fundsMenu = menuBar()->addMenu(tr("资金(&F)"));
    fundsMenu->addAction(tr("资金数据(&D)…"), this, &MainWindow::openFundsWindow);

    // 日志（交易日志窗口）
    auto* journalMenu = menuBar()->addMenu(tr("日志(&L)"));
    journalMenu->addAction(tr("交易日志(&J)…"), this, &MainWindow::openJournalWindow);

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

void MainWindow::openQuantWindow() {
    if (!quantWindow_) {
        quantWindow_ = new QuantWindow(provider_.get(), journal_);
        quantWindow_->setAttribute(Qt::WA_DeleteOnClose);
        // 关闭即置空，避免悬垂
        connect(quantWindow_, &QObject::destroyed, this,
                [this] { quantWindow_ = nullptr; });
        // 面板双击结果 → 主窗口中央图
        connect(quantWindow_, &QuantWindow::openChart, this,
                [this](const StockCode& code) {
            centralStack_->setCurrentWidget(centralChart_);
            centralChart_->loadStock(code, QString::fromStdString(code.displayCode()));
            if (marketDepth_) marketDepth_->setStock(code, QString());
            if (keyData_) keyData_->setStock(code, QString());
            if (chipPanel_) chipPanel_->setStock(code, QString());
        });
    }
    quantWindow_->show();
    quantWindow_->raise();
    quantWindow_->activateWindow();
}

void MainWindow::openFundsWindow() {
    if (!fundsWindow_) {
        fundsWindow_ = new FundsWindow(provider_.get());
        fundsWindow_->setAttribute(Qt::WA_DeleteOnClose);
        connect(fundsWindow_, &QObject::destroyed, this,
                [this] { fundsWindow_ = nullptr; });
        // 龙虎榜双击 → 主窗口中央图
        connect(fundsWindow_, &FundsWindow::openChart, this,
                [this](const StockCode& code, const QString& name) {
            centralStack_->setCurrentWidget(centralChart_);
            centralChart_->loadStock(code, name);
            if (marketDepth_) marketDepth_->setStock(code, name);
            if (keyData_) keyData_->setStock(code, name);
            if (chipPanel_) chipPanel_->setStock(code, name);
            if (fundsWindow_) fundsWindow_->setCurrentStock(code, name);
        });
    }
    fundsWindow_->show();
    fundsWindow_->raise();
    fundsWindow_->activateWindow();
}

void MainWindow::openJournalWindow() {
    if (!journalWindow_) {
        journalWindow_ = new JournalWindow(journal_);
        journalWindow_->setAttribute(Qt::WA_DeleteOnClose);
        connect(journalWindow_, &QObject::destroyed, this,
                [this] { journalWindow_ = nullptr; });
        // 双击记录行 → 主窗口中央图
        connect(journalWindow_, &JournalWindow::openChart, this,
                [this](const StockCode& code, const QString& name) {
            centralStack_->setCurrentWidget(centralChart_);
            centralChart_->loadStock(code, name);
            if (marketDepth_) marketDepth_->setStock(code, name);
            if (keyData_) keyData_->setStock(code, name);
            if (chipPanel_) chipPanel_->setStock(code, name);
        });
    }
    journalWindow_->show();
    journalWindow_->raise();
    journalWindow_->activateWindow();
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

    // 交易日志持久化（在 provider 清理前保存，确保关闭时不丢数据）
    if (journal_) {
        TradeJournalStore store;
        store.save(AppPaths::configDir() + "/trade_journal.json", *journal_);
    }

    ConfigManager::instance()->save();
    LogManager::instance()->flush();
    // 不在 closeEvent 里 disconnect：会与在飞 IO 线程（市场面板/关键数据/盘口 TDX 轮询）
    // 竞争 provider 内部状态 → 释放无效指针。~MainWindow 已在 waitForDone 后排空后再 disconnect。

    QMainWindow::closeEvent(event);
}

} // namespace st

#include "moc_main_window.cpp"
