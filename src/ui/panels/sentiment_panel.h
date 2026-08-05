#pragma once

#include "intelligence/sentiment/sentiment_analyzer.h"
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QTableView;
class QStandardItemModel;

namespace st {

/// 舆情情绪面板 — 本地关键词打分（无真实资讯源，ISentimentProvider 待 P10 第二轮）
///
/// 输入多行新闻标题 → 逐条 SentimentAnalyzer::analyze + averageScore。
/// 纯内存计算，主线程直接执行，无需 provider 与线程池。
class SentimentPanel : public QWidget {
    Q_OBJECT

public:
    explicit SentimentPanel(QWidget* parent = nullptr);

private slots:
    void onAnalyzeClicked();

private:
    void resetTable();

    st::sentiment::SentimentAnalyzer analyzer_;
    QLineEdit* codeEdit_ = nullptr;        // 仅展示（无真实资讯源，不参与计算）
    QPlainTextEdit* newsEdit_ = nullptr;   // 每行一条新闻标题
    QPushButton* analyzeBtn_ = nullptr;
    QLabel* overallScore_ = nullptr;
    QLabel* overallLabel_ = nullptr;
    QLabel* hintLabel_ = nullptr;
    QTableView* table_ = nullptr;
    QStandardItemModel* tableModel_ = nullptr;
};

} // namespace st
