#include <gtest/gtest.h>
#include "intelligence/sentiment/sentiment_analyzer.h"

#include <memory>
#include <string>
#include <vector>

using namespace st;
using namespace st::sentiment;

namespace {

/// 测试数据源桩
class MockProvider : public ISentimentProvider {
public:
    std::vector<NewsItem> fetchNews(const StockCode&, int limit) override {
        if (limit > 0 && static_cast<int>(items.size()) > limit) {
            return std::vector<NewsItem>(items.begin(), items.begin() + limit);
        }
        return items;
    }
    std::vector<NewsItem> items;
};

} // namespace

TEST(SentimentAnalyzerTest, PositiveNewsScoresPositive) {
    SentimentAnalyzer analyzer;
    NewsItem item;
    item.title = "公司业绩大幅增长，发布利好公告";
    item.content = "上半年净利润同比增长50%，超预期";
    auto s = analyzer.analyze(item);
    EXPECT_GT(s.score, 0.0);
    EXPECT_EQ(s.label, SentimentLabel::Positive);
    EXPECT_FALSE(s.summary.empty());
}

TEST(SentimentAnalyzerTest, NegativeNewsScoresNegative) {
    SentimentAnalyzer analyzer;
    NewsItem item;
    item.title = "公司发布利空公告，预计全年亏损";
    item.content = "主要股东减持，股价下跌";
    auto s = analyzer.analyze(item);
    EXPECT_LT(s.score, 0.0);
    EXPECT_EQ(s.label, SentimentLabel::Negative);
}

TEST(SentimentAnalyzerTest, NeutralNewsScoresZero) {
    SentimentAnalyzer analyzer;
    NewsItem item;
    item.title = "公司召开例行股东会议";
    auto s = analyzer.analyze(item);
    EXPECT_DOUBLE_EQ(s.score, 0.0);
    EXPECT_EQ(s.label, SentimentLabel::Neutral);
}

TEST(SentimentAnalyzerTest, MixedAverageNearNeutral) {
    SentimentAnalyzer analyzer;
    NewsItem pos;
    pos.title = "公司业绩大幅增长，利好";
    NewsItem neg;
    neg.title = "公司业绩大幅亏损，利空";
    auto s = analyzer.averageScore({pos, neg});
    EXPECT_NEAR(s.score, 0.0, 0.2);
    EXPECT_EQ(s.label, SentimentLabel::Neutral);
}

TEST(SentimentAnalyzerTest, NoProviderReturnsNull) {
    SentimentAnalyzer analyzer;
    auto s = analyzer.analyzeStock(StockCode(Market::SH, "600519"));
    EXPECT_FALSE(s.has_value());
}

TEST(SentimentAnalyzerTest, UsesProviderWhenSet) {
    auto provider = std::make_shared<MockProvider>();
    NewsItem pos;
    pos.title = "业绩增长超预期，利好";
    provider->items.push_back(pos);
    SentimentAnalyzer analyzer;
    analyzer.setProvider(provider);
    auto s = analyzer.analyzeStock(StockCode(Market::SH, "600519"));
    ASSERT_TRUE(s.has_value());
    EXPECT_GT(s->score, 0.0);
}

TEST(SentimentAnalyzerTest, CustomKeywordTable) {
    SentimentAnalyzer analyzer;
    analyzer.setKeywordTable({"超预期"}, {"爆雷"});
    NewsItem good;
    good.title = "业绩超预期";
    EXPECT_GT(analyzer.analyze(good).score, 0.0);
    NewsItem bad;
    bad.title = "公司爆雷";
    EXPECT_LT(analyzer.analyze(bad).score, 0.0);
}
