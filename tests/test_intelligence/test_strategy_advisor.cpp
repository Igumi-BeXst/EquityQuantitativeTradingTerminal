#include <gtest/gtest.h>
#include "intelligence/advisor/strategy_advisor.h"
#include "engine/optimizer/grid_search.h"

#include <string>
#include <vector>

using namespace st;
using namespace st::advisor;

namespace {

GridSearchResult makeResult(std::vector<std::pair<std::string, int>> params,
                            double objValue, bool success = true) {
    GridSearchResult r;
    r.params = std::move(params);
    r.objectiveValue = objValue;
    r.success = success;
    return r;
}

AdvisorContext makeCtx(std::vector<GridSearchResult> results,
                       Objective objective = Objective::TotalReturn) {
    AdvisorContext ctx;
    ctx.strategyId = "MACross";
    ctx.results = std::move(results);
    ctx.objective = objective;
    return ctx;
}

} // namespace

TEST(StrategyAdvisorTest, SelectsBestByObjective) {
    auto ctx = makeCtx({
        makeResult({{"fast", 2}, {"slow", 20}}, 5.0),
        makeResult({{"fast", 5}, {"slow", 20}}, 10.0),
        makeResult({{"fast", 8}, {"slow", 20}}, 7.0),
    });
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    ASSERT_TRUE(sug.hasRecommendation);
    ASSERT_EQ(sug.recommendedParams.size(), 2u);
    EXPECT_EQ(sug.recommendedParams[0].first, "fast");
    EXPECT_EQ(sug.recommendedParams[0].second, 5);
    EXPECT_FALSE(sug.overfitWarning);
    EXPECT_FALSE(sug.riskWarning);
    EXPECT_GT(sug.confidence, 0.7);
    EXPECT_FALSE(sug.text.empty());
    EXPECT_FALSE(sug.rationale.empty());
}

TEST(StrategyAdvisorTest, SelectsBestForMinimizedObjective) {
    auto ctx = makeCtx({
        makeResult({{"fast", 2}, {"slow", 20}}, 5.0),
        makeResult({{"fast", 5}, {"slow", 20}}, 2.0),
        makeResult({{"fast", 8}, {"slow", 20}}, 3.0),
    }, Objective::MaxDrawdown);
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    ASSERT_TRUE(sug.hasRecommendation);
    EXPECT_EQ(sug.recommendedParams[1].second, 20);  // slow=20
    // 最优 fast=5（objectiveValue=2.0）
    EXPECT_EQ(sug.recommendedParams[0].second, 5);
}

TEST(StrategyAdvisorTest, SkipsFailedResults) {
    auto ctx = makeCtx({
        makeResult({{"fast", 2}, {"slow", 20}}, 100.0, /*success=*/false),
        makeResult({{"fast", 5}, {"slow", 20}}, 8.0),
    });
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    ASSERT_TRUE(sug.hasRecommendation);
    EXPECT_EQ(sug.recommendedParams[0].second, 5);
}

TEST(StrategyAdvisorTest, NoWarningOnFlatResults) {
    auto ctx = makeCtx({
        makeResult({{"fast", 2}, {"slow", 20}}, 10.0),
        makeResult({{"fast", 5}, {"slow", 20}}, 9.5),
        makeResult({{"fast", 8}, {"slow", 20}}, 9.0),
    });
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    EXPECT_FALSE(sug.overfitWarning);
    EXPECT_NEAR(sug.confidence, 0.85, 1e-9);
}

TEST(StrategyAdvisorTest, OverfitWarningOnSpike) {
    auto ctx = makeCtx({
        makeResult({{"fast", 5}, {"slow", 20}}, 100.0),
        makeResult({{"fast", 2}, {"slow", 20}}, 10.0),
        makeResult({{"fast", 8}, {"slow", 20}}, 9.0),
    });
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    EXPECT_TRUE(sug.overfitWarning);
    EXPECT_NEAR(sug.confidence, 0.70, 1e-9);
}

TEST(StrategyAdvisorTest, MonteCarloRiskWarning) {
    auto ctx = makeCtx({
        makeResult({{"fast", 5}, {"slow", 20}}, 12.0),
        makeResult({{"fast", 2}, {"slow", 20}}, 11.0),
    });
    MonteCarlo::Output mc;
    mc.probOfLoss = 0.6;   // > 0.4 → 风险
    mc.p5 = 0.8;
    mc.p50 = 1.1;
    mc.p95 = 1.5;
    ctx.monteCarlo = mc;
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    EXPECT_TRUE(sug.riskWarning);
}

TEST(StrategyAdvisorTest, StressTestRiskWarning) {
    auto ctx = makeCtx({
        makeResult({{"fast", 5}, {"slow", 20}}, 12.0),
    });
    StressTestOutput out;
    StressTestResult win;
    win.windowId = "crash_2015";
    win.success = true;
    win.performance.maxDrawdown = 35.0;  // > 20% → 风险
    out.windows.push_back(win);
    ctx.stressTest = out;
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    EXPECT_TRUE(sug.riskWarning);
    // 无过拟合 → 置信度仅扣风险
    EXPECT_NEAR(sug.confidence, 0.70, 1e-9);
}

TEST(StrategyAdvisorTest, ConfidenceDecreasesWithBothWarnings) {
    auto ctx = makeCtx({
        makeResult({{"fast", 5}, {"slow", 20}}, 100.0),
        makeResult({{"fast", 2}, {"slow", 20}}, 10.0),
    });
    MonteCarlo::Output mc;
    mc.probOfLoss = 0.6;
    mc.p5 = 0.7;
    mc.p50 = 1.0;
    mc.p95 = 1.3;
    ctx.monteCarlo = mc;
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    EXPECT_TRUE(sug.overfitWarning);
    EXPECT_TRUE(sug.riskWarning);
    EXPECT_NEAR(sug.confidence, 0.55, 1e-9);
}

TEST(StrategyAdvisorTest, PoorGridLowersConfidence) {
    auto ctx = makeCtx({
        makeResult({{"fast", 5}, {"slow", 20}}, -8.0),
        makeResult({{"fast", 2}, {"slow", 20}}, -10.0),
    });
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    ASSERT_TRUE(sug.hasRecommendation);
    EXPECT_TRUE(sug.poorResultWarning);
    EXPECT_NEAR(sug.confidence, 0.70, 1e-9);  // 0.85 - 0.15
}

TEST(StrategyAdvisorTest, MinimizedObjectivePoorThreshold) {
    auto ctx = makeCtx({
        makeResult({{"fast", 5}, {"slow", 20}}, 18.0),  // 回撤 18% > 15
        makeResult({{"fast", 2}, {"slow", 20}}, 20.0),
    }, Objective::MaxDrawdown);
    StrategyAdvisor advisor;
    auto sug = advisor.advise(ctx);
    ASSERT_TRUE(sug.hasRecommendation);
    EXPECT_TRUE(sug.poorResultWarning);
    EXPECT_NEAR(sug.confidence, 0.70, 1e-9);
}

TEST(StrategyAdvisorTest, EmptyResultsNoRecommendation) {
    StrategyAdvisor advisor;
    auto sug = advisor.advise(makeCtx({}));
    EXPECT_FALSE(sug.hasRecommendation);
    EXPECT_FALSE(sug.text.empty());
}

TEST(StrategyAdvisorTest, RefinedRangesAroundBest) {
    auto ctx = makeCtx({
        makeResult({{"fastPeriod", 5}, {"slowPeriod", 20}}, 10.0),
        makeResult({{"fastPeriod", 2}, {"slowPeriod", 20}}, 8.0),
    });
    StrategyAdvisor advisor;
    auto ranges = advisor.suggestRefinedRanges(ctx);
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].name, "fastPeriod");
    EXPECT_EQ(ranges[0].from, 4);
    EXPECT_EQ(ranges[0].to, 6);
    EXPECT_EQ(ranges[1].name, "slowPeriod");
    EXPECT_EQ(ranges[1].from, 19);
    EXPECT_EQ(ranges[1].to, 21);
}

TEST(StrategyAdvisorTest, RefinedRangesEmptyWithoutResult) {
    auto ranges = StrategyAdvisor().suggestRefinedRanges(makeCtx({}));
    EXPECT_TRUE(ranges.empty());
}

TEST(StrategyAdvisorTest, ObjectiveNameCoverage) {
    EXPECT_EQ(StrategyAdvisor::objectiveName(Objective::TotalReturn), "总收益率");
    EXPECT_EQ(StrategyAdvisor::objectiveName(Objective::SharpeRatio), "夏普比率");
    EXPECT_EQ(StrategyAdvisor::objectiveName(Objective::MaxDrawdown), "最大回撤");
    EXPECT_EQ(StrategyAdvisor::objectiveName(Objective::CalmarRatio), "卡玛比率");
    EXPECT_EQ(StrategyAdvisor::objectiveName(Objective::ProfitFactor), "盈亏比");
}
