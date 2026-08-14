#include "intelligence/screener/ai_screener.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"
#include <gtest/gtest.h>
#include <string_view>

namespace st {
namespace {

using st::screener::AiScore;
using st::screener::AiScreenerConfig;
using st::screener::runAiScreener;
using st::sentiment::SentimentScore;

/// 合成 60 根日线：up=true 持续上涨（多头排列），false 持续下跌（空头排列）
std::vector<Bar> makeBars(bool up, double base = 10.0) {
    std::vector<Bar> bars;
    bars.reserve(60);
    double close = base;
    for (int i = 0; i < 60; ++i) {
        Bar b;
        b.code = StockCode(std::string_view("SH600000"));
        b.time = utils::addTradingDays(utils::parseDate("2026-01-01"), i);
        b.period = BarPeriod::Daily;
        const double step = up ? 0.16 : -0.16;
        b.open = close;
        b.close = close + step;
        b.high = std::max(b.open, b.close) + 0.3;
        b.low = std::min(b.open, b.close) - 0.3;
        b.volume = static_cast<Volume>(100000 + i * 1000);
        bars.push_back(b);
        close = b.close;
    }
    return bars;
}

SentimentScore senti(double score) {
    SentimentScore s;
    s.score = score;
    s.summary = score > 0 ? "积极" : score < 0 ? "消极" : "中性";
    return s;
}

TEST(AiScreenerTest, RanksByCompositeScoreDescending) {
    // 股票 A 强多头 + 正面情绪；股票 B 强空头 + 负面情绪 → A 在前
    const StockCode a(std::string_view("SH600519"));
    const StockCode b(std::string_view("SZ000001"));
    auto scores = runAiScreener(
        {a, b},
        {makeBars(true, 10.0), makeBars(false, 20.0)},
        {senti(0.6), senti(-0.6)},
        AiScreenerConfig{});
    ASSERT_EQ(scores.size(), 2u);
    EXPECT_EQ(scores[0].code, a);
    EXPECT_EQ(scores[1].code, b);
    EXPECT_GT(scores[0].compositeScore, scores[1].compositeScore);
}

TEST(AiScreenerTest, ScoreMappingRange) {
    const StockCode a(std::string_view("SH600519"));
    const StockCode b(std::string_view("SZ000001"));
    auto scores = runAiScreener(
        {a, b},
        {makeBars(true, 10.0), makeBars(false, 20.0)},
        {senti(0.6), senti(-0.6)},
        AiScreenerConfig{});
    ASSERT_EQ(scores.size(), 2u);
    EXPECT_GT(scores[0].compositeScore, 70.0);   // 强多头 → 高分
    EXPECT_LT(scores[1].compositeScore, 30.0);   // 强空头 → 低分
    EXPECT_GE(scores[0].compositeScore, 0.0);
    EXPECT_LE(scores[0].compositeScore, 100.0);
}

TEST(AiScreenerTest, SentimentDisabled) {
    const StockCode a(std::string_view("SH600519"));
    auto scores = runAiScreener(
        {a}, {makeBars(true)}, {senti(0.6)},
        AiScreenerConfig{/*weights=*/{}, /*useSentiment=*/false});
    ASSERT_EQ(scores.size(), 1u);
    EXPECT_FALSE(scores[0].sentimentScore.has_value());
    EXPECT_TRUE(scores[0].patternScore.has_value());
    EXPECT_TRUE(scores[0].technicalScore.has_value());
}

TEST(AiScreenerTest, MissingSentimentDegrades) {
    // 无情绪数据 → 情绪分项缺失，不阻塞综合分
    const StockCode a(std::string_view("SH600519"));
    auto scores = runAiScreener(
        {a}, {makeBars(true)},
        {std::nullopt},
        AiScreenerConfig{});
    ASSERT_EQ(scores.size(), 1u);
    EXPECT_FALSE(scores[0].sentimentScore.has_value());
    EXPECT_FALSE(std::isnan(scores[0].compositeScore));
    EXPECT_GT(scores[0].compositeScore, 50.0);  // 形态+技术仍偏多
}

TEST(AiScreenerTest, CustomWeightsSentimentDominates) {
    // 情绪权重 1.0：A 形态好但情绪差 → 综合分低；B 形态差但情绪好 → 综合分高
    const StockCode a(std::string_view("SH600519"));
    const StockCode b(std::string_view("SZ000001"));
    AiScreenerConfig cfg;
    cfg.weights = {0.0, 1.0, 0.0};
    auto scores = runAiScreener(
        {a, b},
        {makeBars(true, 10.0), makeBars(false, 20.0)},
        {senti(-0.8), senti(0.8)},
        cfg);
    ASSERT_EQ(scores.size(), 2u);
    EXPECT_EQ(scores[0].code, b);   // 情绪主导 → B 在前
    EXPECT_LT(scores[1].compositeScore, scores[0].compositeScore);
}

TEST(AiScreenerTest, EmptyPoolReturnsEmpty) {
    EXPECT_TRUE(runAiScreener({}, {}, {}, AiScreenerConfig{}).empty());
}

TEST(AiScreenerTest, EmptyBarsSkipped) {
    // B 无数据 → 跳过，只输出 A
    const StockCode a(std::string_view("SH600519"));
    const StockCode b(std::string_view("SZ000001"));
    auto scores = runAiScreener(
        {a, b},
        {makeBars(true), {}},
        {senti(0.6), std::nullopt},
        AiScreenerConfig{});
    ASSERT_EQ(scores.size(), 1u);
    EXPECT_EQ(scores[0].code, a);
}

TEST(AiScreenerTest, ComponentsMappedToSignal) {
    // 分项分数 = composeSignal 分项 score 映射 (score+1)/2*100
    const StockCode a(std::string_view("SH600519"));
    auto scores = runAiScreener(
        {a}, {makeBars(true)}, {senti(0.6)}, AiScreenerConfig{});
    ASSERT_EQ(scores.size(), 1u);
    ASSERT_TRUE(scores[0].patternScore.has_value());
    ASSERT_TRUE(scores[0].sentimentScore.has_value());
    ASSERT_TRUE(scores[0].technicalScore.has_value());
    // 情绪分项 = (0.6+1)/2*100 = 80
    EXPECT_NEAR(*scores[0].sentimentScore, 80.0, 1e-9);
    // 分项均落在 [0,100]
    EXPECT_GE(*scores[0].patternScore, 0.0);
    EXPECT_LE(*scores[0].patternScore, 100.0);
    EXPECT_GE(*scores[0].technicalScore, 0.0);
    EXPECT_LE(*scores[0].technicalScore, 100.0);
    EXPECT_FALSE(scores[0].summary.empty());
}

}  // namespace
}  // namespace st
