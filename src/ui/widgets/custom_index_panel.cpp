#include "ui/widgets/custom_index_panel.h"
#include "ui/widgets/custom_index_editor.h"
#include "data/idata_provider.h"
#include "core/event_bus.h"
#include "core/app_paths.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace st {

namespace {
const QString kUpColor   = QStringLiteral("#ef5350");   // A股红涨
const QString kDownColor = QStringLiteral("#26a69a");   // 绿跌
const QString kFlatColor = QStringLiteral("#9e9e9e");
}  // namespace

CustomIndexPanel::CustomIndexPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    storePath_ = AppPaths::configDir() + "/custom_indexes.json";
    indexes_ = store_.load(storePath_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    list_ = new QListWidget(this);
    layout->addWidget(list_, 1);

    status_ = new QLabel(tr("自定义指数：%1 个").arg(indexes_.size()), this);
    layout->addWidget(status_);

    auto* btnRow = new QHBoxLayout();
    auto* newBtn = new QPushButton(tr("新建"), this);
    auto* editBtn = new QPushButton(tr("编辑"), this);
    auto* delBtn = new QPushButton(tr("删除"), this);
    auto* openBtn = new QPushButton(tr("打开图表"), this);
    openBtn->setStyleSheet(QStringLiteral(
        "QPushButton{color:#ffd700;border:1px solid #8a7b2f;border-radius:3px;"
        "padding:2px 8px;background:#2a2a2c;}QPushButton:hover{background:#3d3d40;}"));
    btnRow->addWidget(newBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(openBtn);
    layout->addLayout(btnRow);

    connect(newBtn, &QPushButton::clicked, this, [this] {
        CustomIndexEditorDialog dlg(provider_, CustomIndex{}, this);
        if (dlg.exec() != QDialog::Accepted) return;
        indexes_.push_back(dlg.result());
        save();
        subscribeQuotes();
        computePrevCloses();
        reloadList();
    });
    connect(editBtn, &QPushButton::clicked, this, [this] {
        auto* item = list_->currentItem();
        if (!item) return;
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        auto it = std::find_if(indexes_.begin(), indexes_.end(),
                               [&id](const CustomIndex& i) { return i.id == id; });
        if (it == indexes_.end()) return;
        CustomIndexEditorDialog dlg(provider_, *it, this);
        if (dlg.exec() != QDialog::Accepted) return;
        *it = dlg.result();
        save();
        subscribeQuotes();
        computePrevCloses();
        reloadList();
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        auto* item = list_->currentItem();
        if (!item) return;
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        indexes_.erase(std::remove_if(indexes_.begin(), indexes_.end(),
                                      [&id](const CustomIndex& i) { return i.id == id; }),
                       indexes_.end());
        save();
        subscribeQuotes();
        computePrevCloses();
        reloadList();
    });
    connect(openBtn, &QPushButton::clicked, this, [this] {
        auto* item = list_->currentItem();
        if (!item) return;
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        auto it = std::find_if(indexes_.begin(), indexes_.end(),
                               [&id](const CustomIndex& i) { return i.id == id; });
        if (it != indexes_.end()) emit openChart(*it);
    });

    // 监听成分股实时行情（主线程直接连接，天然安全）
    connect(EventBus::instance(), &EventBus::eventFired,
            this, &CustomIndexPanel::onQuoteEvent);

    subscribeQuotes();
    computePrevCloses();
    reloadList();
}

void CustomIndexPanel::reloadList() {
    list_->clear();
    for (const auto& idx : indexes_) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  [%2只]").arg(
                QString::fromStdString(idx.name),
                QString::number(static_cast<int>(idx.constituents.size()))), list_);
        item->setData(Qt::UserRole, QString::fromStdString(idx.id));
    }
    status_->setText(tr("自定义指数：%1 个").arg(indexes_.size()));
    updateLiveValues();
}

void CustomIndexPanel::save() {
    store_.save(storePath_, indexes_);
}

void CustomIndexPanel::subscribeQuotes() {
    if (!provider_) return;
    std::set<StockCode> codes;
    for (const auto& idx : indexes_) {
        for (const auto& c : idx.constituents) codes.insert(c.code);
    }
    for (const auto& code : codes) provider_->subscribeQuote(code);
}

void CustomIndexPanel::computePrevCloses() {
    if (!provider_) return;
    IDataProvider* provider = provider_;
    for (const auto& idx : indexes_) {
        const CustomIndex copy = idx;
        QPointer<CustomIndexPanel> guard(this);
        ThreadPool::submitIO([provider, guard, copy] {
            const auto fetchDaily = [provider](const StockCode& c, BarPeriod) {
                return provider ? provider->getBars(c, BarPeriod::Daily, DateTime{}, utils::now())
                                : std::vector<Bar>{};
            };
            const auto daily = computeIndexBars(copy, fetchDaily, BarPeriod::Daily);
            const double prevClose = lastCompletedClose(daily, utils::now());
            QMetaObject::invokeMethod(guard, [guard, id = copy.id, prevClose] {
                if (!guard) return;
                guard->prevClose_[id] = prevClose;
                guard->updateLiveValues();
            }, Qt::QueuedConnection);
        });
    }
}

void CustomIndexPanel::onQuoteEvent(const QString& event, const QVariantMap& mapData) {
    if (event != events::QuoteReceived) return;
    const QString fullCode = mapData.value(QStringLiteral("code")).toString();
    const double change = mapData.value(QStringLiteral("change")).toDouble();
    changePct_[fullCode] = change;
    updateLiveValues();
}

void CustomIndexPanel::updateLiveValues() {
    if (indexes_.empty()) return;
    // 把最新涨跌幅构造成 Quote 向量（computeIndexLive 只读 code/change）
    std::vector<Quote> quotes;
    quotes.reserve(changePct_.size());
    for (auto it = changePct_.begin(); it != changePct_.end(); ++it) {
        Quote q;
        q.code = StockCode(it.key().toStdString());
        q.change = it.value();
        quotes.push_back(std::move(q));
    }

    for (int r = 0; r < list_->count(); ++r) {
        QListWidgetItem* item = list_->item(r);
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        auto it = std::find_if(indexes_.begin(), indexes_.end(),
                               [&id](const CustomIndex& i) { return i.id == id; });
        if (it == indexes_.end()) continue;
        const CustomIndex& idx = *it;

        QString text = QStringLiteral("%1  [%2只]").arg(
            QString::fromStdString(idx.name),
            QString::number(static_cast<int>(idx.constituents.size())));

        const auto pc = prevClose_.find(id);
        if (pc != prevClose_.end() && pc->second > 0.0) {
            const double live = computeIndexLive(pc->second, idx, quotes);
            const double pct = (live / pc->second - 1.0) * 100.0;
            text += QStringLiteral("  %1  %2%").arg(
                QString::number(live, 'f', 2),
                QString::number(pct, 'f', 2));
            const QColor color = pct > 0 ? QColor(kUpColor)
                                : (pct < 0 ? QColor(kDownColor) : QColor(kFlatColor));
            item->setForeground(color);
        }
        item->setText(text);
    }
}

} // namespace st
