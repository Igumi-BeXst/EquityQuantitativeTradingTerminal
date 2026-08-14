#include "intelligence/signal/composite_signal.h"
#include "foundation/utils/indicators.h"
#include <gtest/gtest.h>
#include <limits>

namespace st {
namespace {

using st::pattern::PatternSignal;
using st::pattern::PatternType;
using st::sentiment::SentimentScore;
using st::indicators::MacdResult;
using st::signal::composeSignal;
using st::signal::SignalRating;
using st::signal::ratingName;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

PatternSignal bullSig(double conf) {
    PatternSignal s;
    s.type = PatternType::Hammer;
    s.confidence = conf;
    s.name = "锤头线";
    return s;
}

PatternSignal bearSig(double conf) {
    PatternSignal s;
    s.type = PatternType::ShootingStar;
    s.confidence = conf;
    s.name = "流星";
    return s;
}

SentimentScore senti(double score) {
    SentimentScore s;
    s.score = score;
    s.summary = score > 0 ? "积极" : score < 0 ? "消极" : "中性";
    return s;
}

MacdResult macdOf(double dif, double dea, double hist) {
    MacdResult m;
    m.dif = {dif};
    m.dea = {dea};
    m.hist = {hist};
    return m;
}

TEST(CompositeSignalTest, BullishEverythingStrongBuy) {
    auto cs = composeSignal({bullSig(0.9)}, senti(0.6), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    EXPECT_EQ(cs.rating, SignalRating::StrongBuy);
    EXPECT_GT(cs.score, 0.5);
    EXPECT_GT(cs.confidence, 0.8);
    EXPECT_EQ(cs.components.size(), 3u);
}

TEST(CompositeSignalTest, BearishEverythingStrongSell) {
    auto cs = composeSignal({bearSig(0.9)}, senti(-0.6), 75.0,
                            macdOf(-0.5, 0.5, -0.8), 9.7, 10.0);
    EXPECT_EQ(cs.rating, SignalRating::StrongSell);
    EXPECT_LT(cs.score, -0.5);
}

TEST(CompositeSignalTest, MixedSignalsNeutral) {
    auto cs = composeSignal({bearSig(0.9)}, senti(0.6), 50.0,
                            macdOf(0.1, 0.0, 0.2), 10.0, 10.0);
    EXPECT_EQ(cs.rating, SignalRating::Neutral);
    EXPECT_NEAR(cs.score, 0.0, 0.3);
}

TEST(CompositeSignalTest, MissingSentimentDeductsConfidence) {
    // 情绪同向（score 0.9）→ 分项一致度高；无新闻（summary 空）→ 情绪缺失
    // → 覆盖度 1.0 vs 0.7 → 置信度更低
    auto cs = composeSignal({bullSig(0.9)}, senti(0.9), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    auto cs2 = composeSignal({bullSig(0.9)}, SentimentScore{}, 25.0,
                             macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    EXPECT_EQ(cs.components.size(), 3u);
    EXPECT_EQ(cs2.components.size(), 2u);
    EXPECT_LT(cs2.confidence, cs.confidence);
}

TEST(CompositeSignalTest, OnlyPatternComponent) {
    auto cs = composeSignal({bullSig(1.0)}, SentimentScore{}, kNaN,
                            MacdResult{}, 0.0, 0.0);
    EXPECT_EQ(cs.components.size(), 1u);
    EXPECT_NEAR(cs.score, 1.0, 1e-9);
    EXPECT_NEAR(cs.confidence, 0.4, 1e-9);  // 仅形态权重 0.4
    EXPECT_EQ(cs.rating, SignalRating::StrongBuy);
}

TEST(CompositeSignalTest, RsiBoundaries) {
    auto techOf = [](double rsiVal) {
        auto cs = composeSignal({}, SentimentScore{}, rsiVal,
                                MacdResult{}, 0.0, 0.0);
        EXPECT_EQ(cs.components.size(), 1u);
        return cs.components[0].score;
    };
    EXPECT_NEAR(techOf(25.0), 1.0, 1e-9);    // 超卖
    EXPECT_NEAR(techOf(75.0), -1.0, 1e-9);   // 超买
    EXPECT_NEAR(techOf(50.0), 0.0, 1e-9);    // 中性
    EXPECT_NEAR(techOf(30.0), 0.5, 1e-9);    // 下沿
    EXPECT_NEAR(techOf(70.0), -0.5, 1e-9);   // 上沿
}

TEST(CompositeSignalTest, MacdStates) {
    auto techOf = [](double dif, double dea, double hist) {
        auto cs = composeSignal({}, SentimentScore{}, kNaN,
                                macdOf(dif, dea, hist), 0.0, 0.0);
        for (const auto& c : cs.components) {
            if (c.name == "技术指标") return c.score;
        }
        return -999.0;
    };
    EXPECT_NEAR(techOf(1.0, 0.5, 0.8), 0.5, 1e-9);    // 金叉 + hist>0
    EXPECT_NEAR(techOf(-0.5, 0.5, -0.8), -0.5, 1e-9);  // 死叉 + hist<0
    EXPECT_NEAR(techOf(0.5, 0.5, 0.0), 0.0, 1e-9);     // 粘合
}

TEST(CompositeSignalTest, ThresholdBoundaries) {
    auto r = [](double score) {
        // 仅形态分项 → 综合分 = contribution（confidence 精确控制）。
        // 负向用 bearSig（confidence 在引擎内 clamp [0,1]，负 confidence 无意义）
        const auto patterns = score >= 0.0
            ? std::vector<PatternSignal>{bullSig(score)}
            : std::vector<PatternSignal>{bearSig(-score)};
        return composeSignal(patterns, SentimentScore{}, kNaN,
                             MacdResult{}, 0.0, 0.0).rating;
    };
    EXPECT_EQ(r(0.5), SignalRating::StrongBuy);
    EXPECT_EQ(r(0.49), SignalRating::Buy);
    EXPECT_EQ(r(0.2), SignalRating::Buy);
    EXPECT_EQ(r(0.19), SignalRating::Neutral);
    EXPECT_EQ(r(-0.19), SignalRating::Neutral);
    EXPECT_EQ(r(-0.2), SignalRating::Sell);
    EXPECT_EQ(r(-0.49), SignalRating::Sell);
    EXPECT_EQ(r(-0.5), SignalRating::StrongSell);
}

TEST(CompositeSignalTest, NoDataAtAllNeutral) {
    auto cs = composeSignal({}, SentimentScore{}, kNaN, MacdResult{}, 0.0, 0.0);
    EXPECT_EQ(cs.rating, SignalRating::Neutral);
    EXPECT_NEAR(cs.score, 0.0, 1e-9);
    EXPECT_NEAR(cs.confidence, 0.0, 1e-9);
    EXPECT_TRUE(cs.components.empty());
    EXPECT_NE(cs.summary.find("数据不足"), std::string::npos);
}

TEST(CompositeSignalTest, MomentumContributesToTech) {
    auto techOf = [](double close) {
        auto cs = composeSignal({}, SentimentScore{}, 50.0,
                                macdOf(0.0, 0.0, 0.0), close, 10.0);
        for (const auto& c : cs.components) {
            if (c.name == "技术指标") return c.score;
        }
        return 0.0;
    };
    EXPECT_GT(techOf(10.3), techOf(10.0));  // 涨 3% 动量满分 vs 平盘
}

TEST(CompositeSignalTest, CustomWeights) {
    // 情绪权重 1.0：负面情绪主导，形态/技术不参与
    auto cs = composeSignal({bullSig(1.0)}, senti(-1.0), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0,
                            {0.0, 1.0, 0.0});
    EXPECT_EQ(cs.rating, SignalRating::StrongSell);
    EXPECT_NEAR(cs.score, -1.0, 1e-9);
}

TEST(CompositeSignalTest, SummaryContainsRatingName) {
    auto cs = composeSignal({bullSig(0.9)}, senti(0.6), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    EXPECT_NE(cs.summary.find("强烈买入"), std::string::npos);
    EXPECT_NE(cs.summary.find("K线形态看涨"), std::string::npos);
}

TEST(CompositeSignalTest, RatingNameCoverage) {
    EXPECT_EQ(ratingName(SignalRating::StrongBuy), "强烈买入");
    EXPECT_EQ(ratingName(SignalRating::Buy), "买入");
    EXPECT_EQ(ratingName(SignalRating::Neutral), "观望");
    EXPECT_EQ(ratingName(SignalRating::Sell), "卖出");
    EXPECT_EQ(ratingName(SignalRating::StrongSell), "强烈卖出");
}

}  // namespace
}  // namespace st
