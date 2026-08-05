#include "intelligence/sentiment/sentiment_analyzer.h"

#include <algorithm>

namespace st::sentiment {

namespace {

constexpr double kLabelThreshold = 0.15;  // 情绪标签判定阈值
constexpr double kSmoothDivisor = 3.0;    // 打分平滑除数（单个关键词 → ~0.33）

SentimentLabel labelFor(double score) {
    if (score > kLabelThreshold) return SentimentLabel::Positive;
    if (score < -kLabelThreshold) return SentimentLabel::Negative;
    return SentimentLabel::Neutral;
}

std::string labelName(SentimentLabel label) {
    switch (label) {
        case SentimentLabel::Positive: return "积极";
        case SentimentLabel::Neutral: return "中性";
        case SentimentLabel::Negative: return "消极";
    }
    return "中性";
}

} // namespace

SentimentAnalyzer::SentimentAnalyzer() {
    // 本地默认关键词表（P10 接入真实资讯流前使用）
    positiveWords_ = {
        "利好", "增长", "盈利", "上涨", "突破", "中标",
        "回购", "增持", "超预期", "签约",
    };
    negativeWords_ = {
        "利空", "亏损", "下滑", "减持", "违规",
        "处罚", "退市", "下跌", "爆雷",
    };
}

double SentimentAnalyzer::scoreItem(const std::string& text) const {
    int pos = 0;
    int neg = 0;
    for (const auto& w : positiveWords_) {
        if (text.find(w) != std::string::npos) ++pos;
    }
    for (const auto& w : negativeWords_) {
        if (text.find(w) != std::string::npos) ++neg;
    }
    const double raw = static_cast<double>(pos - neg);
    if (raw == 0.0) return 0.0;
    return std::clamp(raw / kSmoothDivisor, -1.0, 1.0);
}

SentimentScore SentimentAnalyzer::analyze(const NewsItem& item) const {
    const double s = scoreItem(item.title + "\n" + item.content);
    SentimentScore out;
    out.score = s;
    out.label = labelFor(s);
    out.summary = labelName(out.label);
    return out;
}

SentimentScore SentimentAnalyzer::averageScore(const std::vector<NewsItem>& items) const {
    SentimentScore out;
    if (items.empty()) return out;
    double sum = 0.0;
    for (const auto& it : items) {
        sum += scoreItem(it.title + "\n" + it.content);
    }
    out.score = sum / static_cast<double>(items.size());
    out.label = labelFor(out.score);
    out.summary = labelName(out.label);
    return out;
}

std::optional<SentimentScore> SentimentAnalyzer::analyzeStock(const StockCode& code,
                                                              int limit) const {
    if (!provider_) return std::nullopt;
    auto news = provider_->fetchNews(code, limit);
    return averageScore(news);
}

void SentimentAnalyzer::setProvider(std::shared_ptr<ISentimentProvider> provider) {
    provider_ = std::move(provider);
}

void SentimentAnalyzer::setKeywordTable(std::vector<std::string> positive,
                                        std::vector<std::string> negative) {
    positiveWords_ = std::move(positive);
    negativeWords_ = std::move(negative);
}

} // namespace st::sentiment
