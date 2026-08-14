#pragma once

#include "foundation/stock_info.h"
#include "intelligence/sentiment/sentiment_analyzer.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <QWidget>
#include <memory>
#include <optional>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QTableView;
class QStandardItemModel;

namespace st {

class IDataProvider;
class StockSearchBar;

/// 舆情情绪面板 — 东财真实资讯流 + 本地关键词打分
///
/// 两种输入：股票搜索栏（代码/名称/拼音，选中自动拉取东财新闻）；
/// 或手动输入多行标题逐条打分。拉取走 IO 池（安全异步）。
class SentimentPanel : public QWidget {
    Q_OBJECT

public:
    explicit SentimentPanel(IDataProvider* provider, QWidget* parent = nullptr,
                            std::shared_ptr<st::sentiment::ISentimentProvider> newsProvider = {});

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
    StockSearchBar* searchBar_ = nullptr;      // 代码/名称/拼音搜索
    std::optional<StockInfo> selectedStock_;   // 最近选中的股票
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
