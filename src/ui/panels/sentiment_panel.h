#pragma once

#include "intelligence/sentiment/sentiment_analyzer.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <QWidget>
#include <memory>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QTableView;
class QStandardItemModel;

namespace st {

/// 舆情情绪面板 — 东财真实资讯流 + 本地关键词打分
///
/// 两种输入：手动输入多行标题逐条打分；或输入股票代码拉取东财新闻后聚合。
/// 拉取走 IO 池（安全异步：捕获 provider 按值 + QPointer 守卫），本地打分主线程直接算。
class SentimentPanel : public QWidget {
    Q_OBJECT

public:
    explicit SentimentPanel(QWidget* parent = nullptr,
                            std::shared_ptr<st::sentiment::ISentimentProvider> provider = {});

private slots:
    void onAnalyzeClicked();
    void onFetchNewsClicked();

private:
    /// 逐条 analyze + 填表 + averageScore 综合（手动输入与拉取新闻共用）
    void analyzeItems(const std::vector<st::sentiment::NewsItem>& items);
    void applyFetchedNews(const std::vector<st::sentiment::NewsItem>& items);
    void resetTable();

    st::sentiment::SentimentAnalyzer analyzer_;
    std::shared_ptr<st::sentiment::ISentimentProvider> provider_;
    QLineEdit* codeEdit_ = nullptr;
    QPlainTextEdit* newsEdit_ = nullptr;
    QPushButton* analyzeBtn_ = nullptr;
    QPushButton* fetchBtn_ = nullptr;
    QLabel* overallScore_ = nullptr;
    QLabel* overallLabel_ = nullptr;
    QLabel* hintLabel_ = nullptr;
    QTableView* table_ = nullptr;
    QStandardItemModel* tableModel_ = nullptr;
    bool fetching_ = false;
};

} // namespace st
