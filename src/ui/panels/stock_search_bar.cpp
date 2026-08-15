#include "ui/panels/stock_search_bar.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include "core/thread_pool.h"
#include "foundation/enums.h"
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <chrono>
#include <thread>

namespace st {

namespace {
constexpr int kMaxResults = 8;
constexpr int kDebounceMs = 100;
}

StockSearchBar::StockSearchBar(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(tr("搜索股票 (代码/名称/拼音)…"));
    edit_->setClearButtonEnabled(true);
    layout->addWidget(edit_);

    // 下拉弹层（Qt::ToolTip 不抓取鼠标/键盘，避免弹层可见时其他控件无法交互）
    popup_ = new QListWidget(this);
    popup_->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    popup_->setAttribute(Qt::WA_ShowWithoutActivating);  // 显示时不激活/抢键盘焦点
    popup_->setFocusPolicy(Qt::NoFocus);
    popup_->setSelectionMode(QAbstractItemView::SingleSelection);
    popup_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popup_->hide();

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);

    edit_->installEventFilter(this);
    popup_->installEventFilter(this);  // 兜底：popup 若收到按键（导航/可打印字符）
    qApp->installEventFilter(this);    // 全局：点击弹层/编辑框之外时关闭弹层（不拦截事件）
    connect(edit_, &QLineEdit::textChanged, this, &StockSearchBar::onTextChanged);
    connect(debounce_, &QTimer::timeout, this, &StockSearchBar::performSearch);
    connect(popup_, &QListWidget::itemClicked, this, [this] {
        currentRow_ = popup_->currentRow();
        onPopupActivated();
    });

    // 异步加载精选股票池（IO 线程池，不阻塞主线程）
    if (provider_) {
        // 安全异步：捕获构造参数 provider + QPointer 守卫投递回主线程
        QPointer<StockSearchBar> guard(this);
        ThreadPool::submitIO([provider, guard] {
            // 等待数据源连接就绪（最多 15s）：避免连接建立期并发请求与 TDX doConnect 竞争
            for (int i = 0; i < 75 && !provider->isConnected(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            auto sh = provider->getStockList(Market::SH);
            auto sz = provider->getStockList(Market::SZ);
            std::vector<StockInfo> all;
            all.reserve(sh.size() + sz.size());
            // 过滤为可交易 A 股 + 常见指数（丢弃回购 999999/799999、债券、基金等）
            for (auto& s : sh) {
                if (tdx::isTradableAShare(s.code) || tdx::isIndexCode(s.code))
                    all.push_back(std::move(s));
            }
            for (auto& s : sz) {
                if (tdx::isTradableAShare(s.code) || tdx::isIndexCode(s.code))
                    all.push_back(std::move(s));
            }
            QMetaObject::invokeMethod(guard,
                [guard, all = std::move(all)]() mutable {
                    guard->onLoadingFinished(all);
                }, Qt::QueuedConnection);
        });
    }
}

void StockSearchBar::focusEdit() {
    edit_->setFocus();
    edit_->selectAll();
}

bool StockSearchBar::eventFilter(QObject* watched, QEvent* event) {
    // 全局: 点击编辑框/弹层之外 → 关闭弹层（不消费事件，点击继续传给目标控件）
    if (popup_->isVisible() && watched != edit_ && watched != popup_
        && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint g = me->globalPosition().toPoint();
        const bool insideEdit = edit_->rect().contains(edit_->mapFromGlobal(g));
        const bool insidePopup = popup_->geometry().contains(g);
        if (!insideEdit && !insidePopup) hidePopup();
    }
    if (watched == edit_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (popup_->isVisible()) {
            switch (key->key()) {
                case Qt::Key_Down:   moveNext(true);  return true;
                case Qt::Key_Up:     moveNext(false); return true;
                case Qt::Key_Return:
                case Qt::Key_Enter:  onPopupActivated(); return true;
                case Qt::Key_Escape: hidePopup(); return true;
                default: break;
            }
        } else if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Return) {
            performSearch();
            return true;
        }
    } else if (watched == popup_ && event->type() == QEvent::KeyPress) {
        // Qt::Popup 抢键盘后按键到达 popup：导航键本地处理，可打印字符转发给编辑
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Down:    moveNext(true);   return true;
            case Qt::Key_Up:      moveNext(false);  return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:   onPopupActivated(); return true;
            case Qt::Key_Escape:  hidePopup();      return true;
            default:
                if (!key->text().isEmpty()) {
                    QCoreApplication::sendEvent(edit_, key);  // 继续输入
                    return true;
                }
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StockSearchBar::onTextChanged(const QString&) {
    debounce_->start(kDebounceMs);
}

void StockSearchBar::performSearch() {
    const QString query = edit_->text().trimmed();
    if (query.isEmpty()) {
        // 空查询：显示已加载前 8 条精选（加载完成前无内容）
        std::vector<StockInfo> hot;
        const size_t count = std::min<size_t>(allStocks_.size(), kMaxResults);
        for (size_t i = 0; i < count; ++i) hot.push_back(allStocks_[i]);
        showResults(hot);
        return;
    }
    showResults(index_.search(query.toStdString(), kMaxResults));
}

void StockSearchBar::showResults(const std::vector<StockInfo>& results) {
    results_ = results;
    popup_->clear();
    for (const auto& r : results_) {
        popup_->addItem(QStringLiteral("%1  %2")
            .arg(QString::fromStdString(r.code.displayCode()),
                 QString::fromStdString(r.name)));
    }
    if (results_.empty()) {
        hidePopup();
        return;
    }

    currentRow_ = 0;
    popup_->setCurrentRow(0);
    popup_->move(edit_->mapToGlobal(QPoint(0, edit_->height())));
    const int rowHeight = popup_->sizeHintForRow(0);
    const int height = qMin(static_cast<int>(results_.size()) * rowHeight + 4, 300);
    popup_->resize(edit_->width(), height);
    popup_->show();
    popup_->raise();
}

void StockSearchBar::onPopupActivated() {
    if (currentRow_ < 0 || currentRow_ >= static_cast<int>(results_.size())) {
        hidePopup();
        return;
    }
    const StockInfo info = results_[static_cast<size_t>(currentRow_)];
    edit_->setText(QString::fromStdString(info.name));  // 回填名称
    hidePopup();
    emit stockSelected(info);
}

void StockSearchBar::hidePopup() {
    popup_->hide();
    currentRow_ = -1;
}

void StockSearchBar::moveNext(bool down) {
    if (results_.empty()) return;
    const int n = static_cast<int>(results_.size());
    currentRow_ = down ? (currentRow_ + 1) % n : (currentRow_ - 1 + n) % n;
    popup_->setCurrentRow(currentRow_);
    popup_->scrollToItem(popup_->currentItem());
}

void StockSearchBar::onLoadingFinished(const std::vector<StockInfo>& stocks) {
    allStocks_ = stocks;
    index_.build(stocks);
    if (index_.size() > 0) {
        edit_->setPlaceholderText(
            QStringLiteral("搜索 %1 只股票 (代码/名称/拼音)…")
                .arg(static_cast<int>(index_.size())));
    } else {
        edit_->setPlaceholderText(tr("股票列表加载失败"));
    }
}

} // namespace st

#include "moc_stock_search_bar.cpp"
