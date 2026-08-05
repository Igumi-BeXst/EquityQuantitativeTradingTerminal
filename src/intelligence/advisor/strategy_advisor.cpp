#include "intelligence/advisor/strategy_advisor.h"
#include "engine/optimizer/grid_search.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace st::advisor {

namespace {

double medianOf(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

} // namespace

std::string StrategyAdvisor::objectiveName(st::Objective o) {
    switch (o) {
        case st::Objective::TotalReturn:  return "总收益率";
        case st::Objective::SharpeRatio:  return "夏普比率";
        case st::Objective::MaxDrawdown:  return "最大回撤";
        case st::Objective::CalmarRatio:  return "卡玛比率";
        case st::Objective::ProfitFactor: return "盈亏比";
    }
    return "未知目标";
}

const st::GridSearchResult* StrategyAdvisor::bestResult(const AdvisorContext& ctx) {
    std::vector<const st::GridSearchResult*> ok;
    ok.reserve(ctx.results.size());
    for (const auto& r : ctx.results) {
        if (r.success) ok.push_back(&r);
    }
    if (ok.empty()) return nullptr;
    const bool minimized = st::GridSearchOptimizer::objectiveMinimized(ctx.objective);
    std::stable_sort(ok.begin(), ok.end(),
                     [minimized](const st::GridSearchResult* a,
                                 const st::GridSearchResult* b) {
                         if (minimized) return a->objectiveValue < b->objectiveValue;
                         return a->objectiveValue > b->objectiveValue;
                     });
    return ok.front();
}

bool StrategyAdvisor::detectOverfit(const std::vector<st::GridSearchResult>& results,
                                    st::Objective objective) {
    std::vector<double> values;
    values.reserve(results.size());
    for (const auto& r : results) {
        if (r.success) values.push_back(r.objectiveValue);
    }
    if (values.size() < 2) return false;  // 少于 2 组无法判断
    const double med = medianOf(values);
    const bool minimized = st::GridSearchOptimizer::objectiveMinimized(objective);
    const auto it = minimized ? std::min_element(values.begin(), values.end())
                              : std::max_element(values.begin(), values.end());
    const double spread = minimized ? (med - *it) : (*it - med);
    if (med == 0.0) return spread > 1e-6;
    return spread / std::abs(med) > 0.5;
}

bool StrategyAdvisor::poorResult(double objectiveValue, st::Objective objective) {
    if (objective == st::Objective::MaxDrawdown) return objectiveValue > 15.0;
    return objectiveValue <= 0.0;
}

bool StrategyAdvisor::detectRisk(const AdvisorContext& ctx) {
    if (ctx.monteCarlo.has_value()) {
        const auto& mc = *ctx.monteCarlo;
        if (mc.probOfLoss > 0.4 || mc.p5 < 0.85) return true;
    }
    if (ctx.stressTest.has_value()) {
        for (const auto& w : ctx.stressTest->windows) {
            if (w.success && w.performance.maxDrawdown > 20.0) return true;
        }
    }
    return false;
}

AdvisorSuggestion StrategyAdvisor::advise(const AdvisorContext& ctx) const {
    AdvisorSuggestion sug;
    const st::GridSearchResult* best = bestResult(ctx);
    if (!best) {
        sug.hasRecommendation = false;
        sug.text = "无可用的回测结果，无法给出参数建议。";
        return sug;
    }

    sug.hasRecommendation = true;
    sug.recommendedParams = best->params;
    sug.overfitWarning = detectOverfit(ctx.results, ctx.objective);
    sug.riskWarning = detectRisk(ctx);
    sug.poorResultWarning = poorResult(best->objectiveValue, ctx.objective);

    sug.confidence = 0.85;
    if (sug.overfitWarning) sug.confidence -= 0.15;
    if (sug.riskWarning) sug.confidence -= 0.15;
    if (sug.poorResultWarning) sug.confidence -= 0.15;
    sug.confidence = std::clamp(sug.confidence, 0.0, 1.0);

    std::ostringstream text;
    text << "策略 " << ctx.strategyId << " 建议参数：";
    for (size_t i = 0; i < best->params.size(); ++i) {
        if (i > 0) text << ", ";
        text << best->params[i].first << "=" << best->params[i].second;
    }
    text << "（按" << objectiveName(ctx.objective) << "选优）";
    if (sug.poorResultWarning) text << "，但网格整体盈利不佳，谨慎采用";

    std::ostringstream rationale;
    rationale << "网格搜索共 " << ctx.results.size() << " 组参数";
    if (sug.poorResultWarning) {
        rationale << "；最优组合目标值为负（或回撤偏高），网格整体无盈利，建议更换策略或标的";
    }
    if (sug.overfitWarning) {
        rationale << "；最优组合相对其余组合中位值差 >50%，可能存在过拟合";
    }
    if (sug.riskWarning) {
        rationale << "；蒙特卡洛/压力测试显示回撤或亏损风险偏高";
    }
    rationale << "。";

    sug.text = text.str();
    sug.rationale = rationale.str();
    return sug;
}

std::vector<st::ParamRange> StrategyAdvisor::suggestRefinedRanges(
    const AdvisorContext& ctx) const {
    std::vector<st::ParamRange> ranges;
    const st::GridSearchResult* best = bestResult(ctx);
    if (!best) return ranges;
    for (const auto& [name, value] : best->params) {
        st::ParamRange r;
        r.name = name;
        r.from = std::max(1, value - 1);
        r.to = value + 1;
        r.step = 1;
        ranges.push_back(r);
    }
    return ranges;
}

} // namespace st::advisor
