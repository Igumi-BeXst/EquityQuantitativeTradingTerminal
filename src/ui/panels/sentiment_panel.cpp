#include "ui/panels/sentiment_panel.h"
#include "core/log_manager.h"
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

SentimentPanel::SentimentPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    hintLabel_ = new QLabel(tr("本地关键词打分 · 暂无真实资讯源（ISentimentProvider 待接入）"));
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color:#999999;"));
    layout->addWidget(hintLabel_);

    auto* codeRow = new QHBoxLayout;
    codeRow->addWidget(new QLabel(tr("股票代码")));
    codeEdit_ = new QLineEdit;
    codeEdit_->setPlaceholderText(tr("600519（仅展示，不参与计算）"));
    codeRow->addWidget(codeEdit_, 1);
    layout->addLayout(codeRow);

    layout->addWidget(new QLabel(tr("新闻标题（每行一条）")));
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
}

void SentimentPanel::onAnalyzeClicked() {
    resetTable();
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
