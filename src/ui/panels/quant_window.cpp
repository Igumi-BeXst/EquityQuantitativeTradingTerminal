#include "ui/panels/quant_window.h"
#include "ui/panels/pattern_panel.h"
#include "ui/panels/advisor_panel.h"
#include "ui/panels/sentiment_panel.h"
#include "intelligence/sentiment/eastmoney_news_provider.h"
#include "ui/panels/screener_panel.h"
#include "ui/panels/paper_trade_panel.h"
#include "ui/panels/optimization_panel.h"
#include "ui/panels/strategy_compare_panel.h"
#include "ui/panels/stress_test_panel.h"
#include "ui/panels/strategy_panel.h"
#include "ui/panels/backtest_panel.h"
#include "data/idata_provider.h"
#include <QTabWidget>
#include <QVariantMap>
#include <QCloseEvent>

namespace st {

QuantWindow::QuantWindow(IDataProvider* provider,
                         std::shared_ptr<TradeJournalEngine> journal,
                         QWidget* parent)
    : QMainWindow(parent), journal_(std::move(journal)) {
    setWindowTitle(tr("量化工作台"));
    resize(1280, 820);

    auto* tabs = new QTabWidget(this);
    setCentralWidget(tabs);

    // 智能面板
    patternPanel_ = new PatternPanel(provider, tabs);
    advisorPanel_ = new AdvisorPanel(provider, tabs);
    sentimentPanel_ = new SentimentPanel(
        provider, tabs, std::make_shared<st::sentiment::EastMoneyNewsProvider>());
    // 量化面板（原主窗口 quantDock + 策略/回测）
    optimizationPanel_ = new OptimizationPanel(provider, tabs);
    screenerPanel_ = new ScreenerPanel(provider, tabs);
    strategyPanel_ = new StrategyPanel(tabs);
    backtestPanel_ = new BacktestPanel(provider, tabs);
    strategyComparePanel_ = new StrategyComparePanel(provider, tabs);
    stressTestPanel_ = new StressTestPanel(provider, tabs);
    paperTradePanel_ = new PaperTradePanel(provider, tabs);
    if (journal_) {
        paperTradePanel_->setJournal(journal_);
    }

    tabs->addTab(patternPanel_, tr("形态识别"));
    tabs->addTab(optimizationPanel_, tr("参数优化"));
    tabs->addTab(advisorPanel_, tr("优化建议"));
    tabs->addTab(screenerPanel_, tr("选股"));
    tabs->addTab(strategyPanel_, tr("策略"));
    tabs->addTab(backtestPanel_, tr("回测"));
    tabs->addTab(strategyComparePanel_, tr("策略对比"));
    tabs->addTab(stressTestPanel_, tr("压力测试"));
    tabs->addTab(paperTradePanel_, tr("模拟交易"));
    tabs->addTab(sentimentPanel_, tr("舆情情绪"));

    // 跨面板信号（策略/参数优化/优化建议 → 回测）
    connect(strategyPanel_, &StrategyPanel::applyStrategy, this,
            [this](const QString& id, const QVariantMap& params) {
        backtestPanel_->loadStrategy(id, params);
    });
    connect(optimizationPanel_, &OptimizationPanel::applyParams, this,
            [this](const QString& id, const QVariantMap& params) {
        backtestPanel_->loadStrategy(id, params);
    });
    connect(advisorPanel_, &AdvisorPanel::applyParams, this,
            [this](const QString& id, const QVariantMap& params) {
        backtestPanel_->loadStrategy(id, params);
    });
    // 双击结果行 → 主窗口中央图
    connect(patternPanel_, &PatternPanel::openChart, this, &QuantWindow::openChart);
    connect(screenerPanel_, &ScreenerPanel::openChart, this, &QuantWindow::openChart);
}

void QuantWindow::closeEvent(QCloseEvent* event) {
    // 注意：不能对共享线程池 waitForDone()——它是全应用共享的（市场面板/图表/自定义指数
    // 都在用），会阻塞等到所有无关任务跑完（全市场批量报价可达几十秒）→ 关闭卡死。
    // 面板异步任务均为守卫模式（QPointer + shared_ptr cache），面板销毁后 QueuedConnection
    // 回调被 Qt 自动丢弃（实测不执行），无 use-after-free 风险。
    QMainWindow::closeEvent(event);
}

} // namespace st

#include "moc_quant_window.cpp"
