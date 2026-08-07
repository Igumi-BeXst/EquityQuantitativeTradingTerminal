#include "ui/widgets/overlay_dialog.h"
#include "ui/panels/stock_search_bar.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "foundation/stock_info.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace st {

namespace {
// 快捷指数按钮样式（与工具按钮区分，简洁深色）
const char* kQuickBtnStyle = "QPushButton{color:#e6e6e6;border:1px solid #5a5a5a;"
    "border-radius:3px;padding:3px 6px;background:#2a2a2c;}"
    "QPushButton:hover{background:#3d3d40;border-color:#999;}";

/// 按名称/代码过滤板块列表（行业只过滤行业，概念只过滤概念）
void applyListFilter(QListWidget* list, const QString& text) {
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        const bool match = item->text().contains(text, Qt::CaseInsensitive) ||
                           item->data(Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
        item->setHidden(!text.isEmpty() && !match);
    }
}
}  // namespace

OverlayDialog::OverlayDialog(IDataProvider* provider, bool showRelativeStrength,
                             QWidget* parent)
    : QDialog(parent), provider_(provider) {
    setWindowTitle(tr("叠加对比"));
    resize(360, 420);

    auto* tabs = new QTabWidget(this);

    // --- Tab 1: 指数 / 个股 ---
    auto* secTab = new QWidget(tabs);
    auto* secLayout = new QVBoxLayout(secTab);
    auto* indexRow = new QHBoxLayout();
    struct Index { const char* code; const char* name; };
    const Index kIndices[] = {
        {"SH000001", "上证指数"},
        {"SZ399001", "深证成指"},
        {"SZ399006", "创业板指"},
        {"SH000688", "科创50"},
    };
    for (const auto& idx : kIndices) {
        auto* btn = new QPushButton(QString::fromUtf8(idx.name), secTab);
        btn->setStyleSheet(QString::fromUtf8(kQuickBtnStyle));
        connect(btn, &QPushButton::clicked, this, [this, idx] {
            OverlayTarget t;
            t.kind = OverlayKind::Security;
            t.stockCode = StockCode(idx.code);
            t.name = QString::fromUtf8(idx.name);
            selectTarget(t);
        });
        indexRow->addWidget(btn);
    }
    secLayout->addLayout(indexRow);
    secLayout->addSpacing(4);
    secLayout->addWidget(new QLabel(tr("或搜索个股/指数："), secTab));
    search_ = new StockSearchBar(provider_, secTab);
    search_->setFixedWidth(320);
    secLayout->addWidget(search_);
    secLayout->addStretch();
    connect(search_, &StockSearchBar::stockSelected, this, [this](const StockInfo& info) {
        OverlayTarget t;
        t.kind = OverlayKind::Security;
        t.stockCode = info.code;
        t.name = QString::fromStdString(info.name);
        selectTarget(t);
    });
    tabs->addTab(secTab, tr("指数/个股"));

    // --- Tab 2 / 3: 行业 / 概念板块（懒加载 + 名称/代码搜索） ---
    auto* indTab = new QWidget(tabs);
    auto* indLayout = new QVBoxLayout(indTab);
    indLayout->setContentsMargins(4, 4, 4, 4);
    indLayout->setSpacing(4);
    auto* indSearch = new QLineEdit(indTab);
    indSearch->setPlaceholderText(tr("搜索行业板块…"));
    indSearch->setClearButtonEnabled(true);
    indLayout->addWidget(indSearch);
    auto* indList = new QListWidget(indTab);
    indLayout->addWidget(indList);
    tabs->addTab(indTab, tr("行业板块"));

    auto* conTab = new QWidget(tabs);
    auto* conLayout = new QVBoxLayout(conTab);
    conLayout->setContentsMargins(4, 4, 4, 4);
    conLayout->setSpacing(4);
    auto* conSearch = new QLineEdit(conTab);
    conSearch->setPlaceholderText(tr("搜索概念板块…"));
    conSearch->setClearButtonEnabled(true);
    conLayout->addWidget(conSearch);
    auto* conList = new QListWidget(conTab);
    conLayout->addWidget(conList);
    tabs->addTab(conTab, tr("概念板块"));

    // 板块选中（双击确认，避免误触滚动）
    auto onSectorChosen = [this](QListWidgetItem* item) {
        if (!item) return;
        OverlayTarget t;
        t.kind = OverlayKind::Sector;
        t.sectorCode = item->data(Qt::UserRole).toString().toStdString();
        t.sectorType = static_cast<SectorType>(item->data(Qt::UserRole + 1).toInt());
        t.name = item->text();
        selectTarget(t);
    };
    connect(indList, &QListWidget::itemDoubleClicked, this, onSectorChosen);
    connect(conList, &QListWidget::itemDoubleClicked, this, onSectorChosen);

    // 搜索过滤：行业只过滤行业、概念只过滤概念；列表异步加载完成后也会套用当前过滤
    connect(indSearch, &QLineEdit::textChanged, this, [indList](const QString& text) {
        applyListFilter(indList, text);
    });
    connect(conSearch, &QLineEdit::textChanged, this, [conList](const QString& text) {
        applyListFilter(conList, text);
    });

    // Tab 切换懒加载
    connect(tabs, &QTabWidget::currentChanged, this,
            [this, tabs, indTab, conTab, indList, conList, indSearch, conSearch](int) {
        if (tabs->currentWidget() == indTab) {
            loadSectorList(indList, indSearch, SectorType::Industry);
        } else if (tabs->currentWidget() == conTab) {
            loadSectorList(conList, conSearch, SectorType::Concept);
        }
    });

    // --- 底部：相对强弱 + 清除 / 取消 ---
    rsCheck_ = new QCheckBox(tr("显示相对强弱副图（K线图）"), this);
    rsCheck_->setChecked(showRelativeStrength);
    auto* clearBtn = new QPushButton(tr("清除叠加"), this);
    auto* cancelBtn = new QPushButton(tr("取消"), this);
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addWidget(rsCheck_);
    bottomRow->addStretch();
    bottomRow->addWidget(clearBtn);
    bottomRow->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tabs, 1);
    mainLayout->addLayout(bottomRow);

    connect(clearBtn, &QPushButton::clicked, this, [this] {
        emit clearRequested();
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void OverlayDialog::loadSectorList(QListWidget* list, QLineEdit* search, SectorType type) {
    if (list->count() > 0 || busy_ || !provider_) return;
    busy_ = true;
    // 占位提示（异步加载期间不显示空列表）
    auto* loadingItem = new QListWidgetItem(tr("加载中…"));
    loadingItem->setFlags(Qt::NoItemFlags);
    list->addItem(loadingItem);
    const int gen = ++gen_;
    IDataProvider* provider = provider_;
    QPointer<OverlayDialog> guard(this);
    ThreadPool::submitIO([provider, guard, gen, type, list, search] {
        // 通达信板块指数（880xxx 行业 / 885xxx 概念），走 TDX 主源（定向快速抓取）
        const auto indices = provider->getSectorIndices();
        QMetaObject::invokeMethod(guard, [guard, gen, type, list, search,
                                          indices = std::move(indices)]() mutable {
            if (!guard || gen != guard->gen_) return;  // 对话框已关 / 新请求
            guard->busy_ = false;
            list->clear();  // 移除占位
            if (indices.empty()) {
                auto* emptyItem = new QListWidgetItem(tr("暂无板块数据"));
                emptyItem->setFlags(Qt::NoItemFlags);
                list->addItem(emptyItem);
                return;
            }
            // 通达信板块指数：行业 8803xx-8804xx，概念 8805xx+（880001-099 大盘/8802xx 地域不展示）
            for (const auto& s : indices) {
                const std::string c = s.code.code();
                const bool isIndustry = c.size() >= 6 && c.compare(0, 3, "880") == 0 &&
                                        c >= "880300" && c < "880500";
                const bool isConcept = c.size() >= 6 && c.compare(0, 3, "880") == 0 &&
                                       c >= "880500";
                if (type == SectorType::Industry && !isIndustry) continue;
                if (type == SectorType::Concept && !isConcept) continue;
                auto* item = new QListWidgetItem(QString::fromUtf8(s.name.c_str()));
                item->setData(Qt::UserRole, QString::fromStdString(c));
                item->setData(Qt::UserRole + 1, static_cast<int>(type));
                list->addItem(item);
            }
            // 列表异步加载完成后，套用搜索框当前过滤（用户可能已先输入）
            if (search) applyListFilter(list, search->text());
        }, Qt::QueuedConnection);
    });
}

void OverlayDialog::selectTarget(const OverlayTarget& target) {
    emit overlaySelected(target, rsCheck_ ? rsCheck_->isChecked() : false);
    accept();
}

} // namespace st

#include "moc_overlay_dialog.cpp"
