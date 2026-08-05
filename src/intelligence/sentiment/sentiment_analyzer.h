#pragma once

#include "intelligence/sentiment/sentiment_types.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace st::sentiment {

/// 舆情情绪分析器 — 本地关键词表打分（接口桩，无真实新闻源）
///
/// 可独立工作：analyze() 直接对 NewsItem 打分；
/// 也可配合 ISentimentProvider：analyzeStock() 拉取新闻后聚合。
class SentimentAnalyzer {
public:
    SentimentAnalyzer();

    /// 分析单条新闻 → 分数（-1~+1）
    SentimentScore analyze(const NewsItem& item) const;

    /// 多条新闻的加权平均情绪
    SentimentScore averageScore(const std::vector<NewsItem>& items) const;

    /// 拉取并聚合一只股票的情绪（无 provider 时返回 nullopt）
    std::optional<SentimentScore> analyzeStock(const StockCode& code,
                                               int limit = 20) const;

    /// 注入数据源（P10 接入真实资讯流）
    void setProvider(std::shared_ptr<ISentimentProvider> provider);

    /// 自定义关键词表
    void setKeywordTable(std::vector<std::string> positive,
                         std::vector<std::string> negative);

private:
    double scoreItem(const std::string& text) const;

    std::vector<std::string> positiveWords_;
    std::vector<std::string> negativeWords_;
    std::shared_ptr<ISentimentProvider> provider_;
};

} // namespace st::sentiment
