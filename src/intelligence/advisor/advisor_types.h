#pragma once

#include "engine/optimizer/grid_search.h"
#include "engine/analyzer/monte_carlo.h"
#include "engine/analyzer/stress_test.h"
#include <optional>
#include <string>
#include <vector>

namespace st::advisor {

/// 建议上下文 — 网格搜索结果 + 可选风险分析
///
/// results 通常来自 GridSearchOptimizer::run()（按目标值排序），
/// 也可由调用方直接构造。advisor 不重跑回测，只消费已有结果。
struct AdvisorContext {
    std::string strategyId;
    std::vector<st::GridSearchResult> results;         // 参数网格回测结果
    std::optional<st::MonteCarlo::Output> monteCarlo;  // 最优参数下的蒙特卡洛模拟
    std::optional<st::StressTestOutput> stressTest;    // 最优参数下的压力测试
    st::Objective objective = st::Objective::TotalReturn;
    int topN = 5;                                      // 展示/对比的前 N 组
};

/// 建议输出
struct AdvisorSuggestion {
    bool hasRecommendation = false;
    std::vector<std::pair<std::string, int>> recommendedParams;
    double confidence = 0.0;      // 0~1，随过拟合/风险/整体不佳警告下调
    bool overfitWarning = false;  // 最优参数相对其余组合差异过大，可能过拟合
    bool riskWarning = false;     // 蒙特卡洛/压力测试显示回撤或亏损风险偏高
    bool poorResultWarning = false;  // 最优组合目标值不佳（网格整体无盈利）
    std::string text;             // 建议正文（中文）
    std::string rationale;        // 推荐理由（中文）
};

} // namespace st::advisor
