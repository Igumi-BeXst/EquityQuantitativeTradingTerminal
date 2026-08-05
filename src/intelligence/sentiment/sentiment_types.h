#pragma once

#include "foundation/stock_code.h"
#include <string>
#include <vector>

namespace st::sentiment {

/// 舆情标签
enum class SentimentLabel : uint8_t {
    Positive,  // 积极
    Neutral,   // 中性
    Negative,  // 消极
};

/// 单条新闻/资讯
struct NewsItem {
    std::string title;
    std::string content;
    std::string source;
    std::string date;  // "2024-05-01"
};

/// 情绪得分（-1 消极 ~ +1 积极）
struct SentimentScore {
    double score = 0.0;                       // -1 ~ +1
    SentimentLabel label = SentimentLabel::Neutral;
    std::string summary;                      // 中文摘要
};

/// 舆情数据源抽象接口 — 暂无实现，P10 接真实资讯流
class ISentimentProvider {
public:
    virtual ~ISentimentProvider() = default;

    /// 获取指定股票最近的 limit 条新闻
    virtual std::vector<NewsItem> fetchNews(const StockCode& code, int limit) = 0;
};

} // namespace st::sentiment
