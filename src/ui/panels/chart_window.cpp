#include "ui/panels/chart_window.h"
#include "ui/widgets/central_chart_widget.h"
#include "engine/journal/trade_journal.h"
#include "core/log_manager.h"
#include <QApplication>
#include <QThread>
#include <utility>

namespace st {

ChartWindow::ChartWindow(IDataProvider* provider,
                         std::shared_ptr<TradeJournalEngine> journal,
                         QWidget* parent)
    : QMainWindow(parent), journal_(std::move(journal)) {
    setWindowTitle(tr("图表"));
    resize(900, 560);
    chart_ = new CentralChartWidget(provider, /*standalone=*/true, this);
    setCentralWidget(chart_);
    // 新窗口内再开新窗口
    connect(chart_, &CentralChartWidget::openNewWindow, this,
            &ChartWindow::openNewWindow);
}

ChartWindow::~ChartWindow() = default;

void ChartWindow::loadStock(const StockCode& code, const QString& name) {
    setWindowTitle(QStringLiteral("%1 - 图表").arg(name));
    chart_->loadStock(code, name);
    refreshTradeMarks();
}

// 由 MainWindow 在日志变更时统一调用（onChange 覆盖式，窗口不自己注册）
void ChartWindow::refreshTradeMarks() {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, [this] { refreshTradeMarks(); },
                                  Qt::QueuedConnection);
        return;
    }
    if (!journal_ || !chart_) return;
    const StockCode code = chart_->currentCode();
    if (!code.isValid()) return;
    const auto entries = journal_->entries();
    const auto marks = collectTradeMarks(entries, code);
    const auto holdings = deriveHoldings(entries, code);
    chart_->setTradeMarks(marks, holdings);
}

} // namespace st

#include "moc_chart_window.cpp"
