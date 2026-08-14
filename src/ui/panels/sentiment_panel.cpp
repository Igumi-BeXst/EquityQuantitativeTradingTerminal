#include "ui/panels/sentiment_panel.h"
#include "ui/panels/stock_search_bar.h"
#include "core/log_manager.h"
#include "core/thread_pool.h"
#include <QColor>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMetaObject>
#include <QPointer>
#include <utility>

namespace st {

namespace {
constexpr char kUpColor[] = "#e54648";    // 红
constexpr char kDownColor[] = "#2e9e5b";  // 绿

QString labelText(st::sentiment::SentimentLabel label) {
    switch (label) {
        case st::sentiment::SentimentLabel::Positive: return QStringLiteral("积极");
        case st::sentiment::SentimentLabel::Negative: return QStringLiteral("消极");
        default: return QStringLiteral("中性");
    }
}
}  // namespace

SentimentPanel::SentimentPanel(IDataProvider* provider, QWidget* parent,
                               std::shared_ptr<st::sentiment::ISentimentProvider> newsProvider)
    : QWidget(parent), provider_(std::move(newsProvider)) {
    analyzer_.setProvider(provider_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    hintLabel_ = new QLabel(tr("东财资讯流 · 本地关键词打分"));
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color:#999999;"));
    layout->addWidget(hintLabel_);

    // 股票搜索栏（代码/名称/拼音，选中自动拉取）
    auto* codeRow = new QHBoxLayout;
    codeRow->addWidget(new QLabel(tr("股票")));
    searchBar_ = new StockSearchBar(provider, this);
    fetchBtn_ = new QPushButton(tr("拉取东财新闻"));
    codeRow->addWidget(searchBar_, 1);
    codeRow->addWidget(fetchBtn_);
    layout->addLayout(codeRow);
    connect(searchBar_, &StockSearchBar::stockSelected, this,
            [this](const StockInfo& info) {
                selectedStock_ = info;
                onFetchNewsClicked();   // 选中即拉取
            });

    layout->addWidget(new QLabel(tr("或手动输入新闻标题（每行一条）")));
    newsEdit_ = new QPlainTextEdit;
    newsEdit_->setPlaceholderText(tr("公司业绩增长超预期，净利润创新高\n股东发布减持公告，机构下调评级"));
    newsEdit_->setMinimumHeight(120);
    layout->addWidget(newsEdit_);

    analyzeBtn_ = new QPushButton(tr("分析情绪"));
    layout->addWidget(analyzeBtn_);

    auto* overallRow = new QHBoxLayout;
    overallRow->addWidget(new QLabel(tr("综合情绪")));
    overallLabel_ = new QLabel(tr("—"));
    overallScore_ = new QLabel(tr("—"));
    overallRow->addStretch();
    overallRow->addWidget(overallLabel_);
    overallRow->addWidget(overallScore_);
    layout->addLayout(overallRow);

    tableModel_ = new QStandardItemModel(0, 3, this);
    tableModel_->setHorizontalHeaderLabels({tr("标题"), tr("评分"), tr("情绪")});
    table_ = new QTableView;
    table_->setModel(tableModel_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    connect(analyzeBtn_, &QPushButton::clicked, this, &SentimentPanel::onAnalyzeClicked);
    connect(fetchBtn_, &QPushButton::clicked, this, &SentimentPanel::onFetchNewsClicked);
}

void SentimentPanel::onAnalyzeClicked() {
    std::vector<st::sentiment::NewsItem> items;
    const QStringList lines = newsEdit_->toPlainText().split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        st::sentiment::NewsItem item;
        item.title = line.trimmed().toStdString();
        if (!item.title.empty()) items.push_back(std::move(item));
    }
    if (items.empty()) {
        LogManager::instance()->log(LogLevel::Warn, "舆情分析: 请输入至少一条新闻标题");
        overallLabel_->setText(tr("—"));
        overallScore_->setText(tr("—"));
        return;
    }
    analyzeItems(items);
}

void SentimentPanel::onFetchNewsClicked() {
    if (fetching_) return;
    if (!selectedStock_) {
        LogManager::instance()->log(LogLevel::Warn, "舆情: 请先在搜索栏选择股票");
        return;
    }
    const StockCode code = selectedStock_->code;
    if (!code.isValid()) {
        LogManager::instance()->log(LogLevel::Warn, "舆情: 请选择有效股票");
        return;
    }
    if (!provider_) {
        LogManager::instance()->log(LogLevel::Warn, "舆情: 无资讯源，仅支持手动输入");
        return;
    }
    fetching_ = true;
    fetchBtn_->setEnabled(false);
    hintLabel_->setText(tr("正在拉取 %1 的东财资讯…")
        .arg(QString::fromStdString(selectedStock_->name)));
    resetTable();

    // 安全异步：捕获 provider/code 按值 + QPointer 守卫投递回主线程
    const auto provider = provider_;
    QPointer<SentimentPanel> guard(this);
    ThreadPool::submitIO([provider, code, guard] {
        auto items = provider->fetchNews(code, 20);
        QMetaObject::invokeMethod(guard,
            [guard, items = std::move(items)]() mutable {
                guard->fetching_ = false;
                guard->fetchBtn_->setEnabled(true);
                guard->applyFetchedNews(items);
            }, Qt::QueuedConnection);
    });
}

void SentimentPanel::applyFetchedNews(const std::vector<st::sentiment::NewsItem>& items) {
    if (items.empty()) {
        hintLabel_->setText(tr("东财资讯流 · 拉取失败或该股近期无资讯"));
        overallLabel_->setText(tr("—"));
        overallScore_->setText(tr("—"));
        return;
    }
    hintLabel_->setText(tr("东财资讯流 · 本地关键词打分（%1 条）").arg(items.size()));
    analyzeItems(items);
}

void SentimentPanel::analyzeItems(const std::vector<st::sentiment::NewsItem>& items) {
    resetTable();
    for (const auto& item : items) {
        const auto s = analyzer_.analyze(item);
        const int row = tableModel_->rowCount();
        tableModel_->insertRow(row);  // 先插行再 setItem，避免模型内部状态不一致
        tableModel_->setItem(row, 0, new QStandardItem(QString::fromStdString(item.title)));
        tableModel_->setItem(row, 1, new QStandardItem(QString::number(s.score, 'f', 2)));
        auto* lbl = new QStandardItem(labelText(s.label));
        lbl->setForeground(s.score >= 0 ? QColor(kUpColor) : QColor(kDownColor));
        tableModel_->setItem(row, 2, lbl);
    }

    const auto avg = analyzer_.averageScore(items);
    overallLabel_->setText(labelText(avg.label));
    overallLabel_->setStyleSheet(QStringLiteral("color:%1;")
        .arg(avg.score >= 0 ? kUpColor : kDownColor));
    overallScore_->setText(QString::number(avg.score, 'f', 2));
}

void SentimentPanel::resetTable() {
    tableModel_->removeRows(0, tableModel_->rowCount());
}

} // namespace st

#include "moc_sentiment_panel.cpp"
