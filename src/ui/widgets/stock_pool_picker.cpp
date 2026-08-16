#include "ui/widgets/stock_pool_picker.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <chrono>
#include <thread>

namespace st {

StockPoolPicker::StockPoolPicker(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // 搜索过滤框
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("过滤: 代码/名称/拼音首字母…"));
    searchEdit_->setClearButtonEnabled(true);
    layout->addWidget(searchEdit_);

    // 全市场列表（多选）
    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setMaximumHeight(120);
    list_->setUniformItemSizes(true);
    layout->addWidget(list_);

    // 状态 + 全选/清空
    auto* bottom = new QHBoxLayout;
    statusLabel_ = new QLabel(tr("加载中…"), this);
    bottom->addWidget(statusLabel_);
    bottom->addStretch();
    selectAllBtn_ = new QPushButton(tr("全选"), this);
    selectAllBtn_->setEnabled(false);
    clearBtn_ = new QPushButton(tr("清空"), this);
    clearBtn_->setEnabled(false);
    bottom->addWidget(selectAllBtn_);
    bottom->addWidget(clearBtn_);
    layout->addLayout(bottom);

    connect(searchEdit_, &QLineEdit::textChanged,
            this, &StockPoolPicker::onSearchTextChanged);
    connect(selectAllBtn_, &QPushButton::clicked, this, [this] {
        setAllVisibleChecked(true);
    });
    connect(clearBtn_, &QPushButton::clicked, this, [this] {
        setAllVisibleChecked(false);
    });
    // 勾选状态（checkbox）变化 → 刷新计数；selection 高亮不用于计数
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        updateStatus();
        emit selectionChanged();
    });

    // 异步加载全市场列表（IO 线程池，不阻塞主线程）
    if (provider_) {
        QPointer<StockPoolPicker> guard(this);
        ThreadPool::submitIO([provider, guard] {
            // 等待数据源连接就绪（最多 15s）：避免连接建立期的并发请求
            // 与 TDX doConnect 竞争 → 堆损坏（用户快速打开量化工作台时 8+ 组件同时拉列表）
            for (int i = 0; i < 75 && !provider->isConnected(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            auto sh = provider->getStockList(Market::SH);
            auto sz = provider->getStockList(Market::SZ);
            std::vector<StockInfo> all;
            all.reserve(sh.size() + sz.size());
            // 过滤为可交易 A 股（丢弃回购 999999/799999、债券、基金等）
            for (auto& s : sh) {
                if (tdx::isTradableAShare(s.code)) all.push_back(std::move(s));
            }
            for (auto& s : sz) {
                if (tdx::isTradableAShare(s.code)) all.push_back(std::move(s));
            }
            QMetaObject::invokeMethod(guard,
                [guard, all = std::move(all)]() mutable {
                    guard->onLoadingFinished(std::move(all));
                }, Qt::QueuedConnection);
        });
    }
}

void StockPoolPicker::onLoadingFinished(std::vector<StockInfo> stocks) {
    allStocks_ = std::move(stocks);
    ready_ = true;
    list_->setUpdatesEnabled(false);   // 5213 项批量插入：先禁重绘，插完一次刷新
    list_->clear();
    for (const auto& s : allStocks_) {
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2")
            .arg(QString::fromStdString(s.code.displayCode()),
                 QString::fromUtf8(s.name)));
        item->setData(Qt::UserRole,
                      QString::fromStdString(s.code.fullCode()));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // 先设勾选再 addItem：未挂载时 setCheckState 不发 itemChanged（避免 5k 次信号风暴）
        item->setCheckState(Qt::Checked);   // 加载完成默认全选
        list_->addItem(item);
    }
    list_->setUpdatesEnabled(true);
    list_->viewport()->update();
    selectAllBtn_->setEnabled(true);
    clearBtn_->setEnabled(true);
    updateStatus();
    emit selectionChanged();
    LogManager::instance()->log(LogLevel::Info,
        "股票池: 全市场 {} 只已加载", allStocks_.size());
}

void StockPoolPicker::onSearchTextChanged(const QString& text) {
    (void)text;   // 过滤直接读 edit 当前文本
    applyFilter();
}

void StockPoolPicker::applyFilter() {
    const QString q = searchEdit_->text().trimmed();
    const bool empty = q.isEmpty();
    // 批量 setHidden 会逐项触发 itemChanged → 阻断信号 + 禁重绘，最后一次性刷新
    // （5213 项逐个发信号 + 计数 = O(n²) 卡顿）
    list_->setUpdatesEnabled(false);
    const bool oldBlock = list_->blockSignals(true);
    for (int i = 0; i < list_->count(); ++i) {
        auto* item = list_->item(i);
        const auto& s = allStocks_[static_cast<size_t>(i)];
        bool match = empty;
        if (!empty) {
            const QString code = QString::fromStdString(s.code.displayCode());
            const QString name = QString::fromUtf8(s.name);
            const QString initials = QString::fromUtf8(s.pinyinInitials);
            match = code.contains(q, Qt::CaseInsensitive) ||
                    name.contains(q, Qt::CaseInsensitive) ||
                    initials.contains(q.toUpper());
        }
        item->setHidden(!match);
    }
    list_->blockSignals(oldBlock);
    list_->setUpdatesEnabled(true);
    list_->viewport()->update();
    updateStatus();
}

void StockPoolPicker::setAllVisibleChecked(bool on) {
    // 批量 setCheckState/setSelected 逐项触发 itemChanged → 阻断信号 + 禁重绘，
    // 结束后一次性更新计数与 selectionChanged（5213 项 O(n²) → O(n)）
    list_->setUpdatesEnabled(false);
    const bool oldBlock = list_->blockSignals(true);
    for (int i = 0; i < list_->count(); ++i) {
        auto* item = list_->item(i);
        if (item->isHidden()) continue;
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        item->setSelected(on);
    }
    list_->blockSignals(oldBlock);
    list_->setUpdatesEnabled(true);
    list_->viewport()->update();
    updateStatus();
    emit selectionChanged();
}

void StockPoolPicker::updateStatus() {
    if (!ready_) {
        statusLabel_->setText(tr("加载中…"));
        return;
    }
    int checked = 0;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->checkState() == Qt::Checked) ++checked;
    }
    statusLabel_->setText(tr("共 %1 只 · 已选 %2").arg(list_->count()).arg(checked));
}

std::vector<StockCode> StockPoolPicker::selectedSymbols() const {
    std::vector<StockCode> out;
    out.reserve(allStocks_.size());
    for (int i = 0; i < list_->count(); ++i) {
        auto* item = list_->item(i);
        if (item->checkState() == Qt::Checked) {
            out.push_back(StockCode(item->data(Qt::UserRole).toString().toStdString()));
        }
    }
    return out;
}

} // namespace st

#include "moc_stock_pool_picker.cpp"
