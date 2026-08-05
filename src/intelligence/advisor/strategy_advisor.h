#pragma once

#include "intelligence/advisor/advisor_types.h"
#include <string>
#include <vector>

namespace st::advisor {

/// 策略参数优化建议
///
/// 消费 GridSearchOptimizer 的排序结果 + 可选蒙特卡洛/压力测试输出，
/// 按 objective 方向选优（复用 GridSearchOptimizer::objectiveMinimized），
/// 不重跑回测。输出参数建议、置信度、过拟合/风险警告与中文解释。
class StrategyAdvisor {
public:
    /// 生成参数优化建议
    AdvisorSuggestion advise(const AdvisorContext& ctx) const;

    /// 围绕推荐参数生成精化网格（value±1，step 1）
    std::vector<st::ParamRange> suggestRefinedRanges(const AdvisorContext& ctx) const;

    /// objective 中文名
    static std::string objectiveName(st::Objective o);

private:
    /// 取最优（success 且目标值最佳）结果
    static const st::GridSearchResult* bestResult(const AdvisorContext& ctx);

    /// 过拟合探测：最优目标值相对其余组合中位值差 > 50%
    static bool detectOverfit(const std::vector<st::GridSearchResult>& results,
                              st::Objective objective);

    /// 风险探测：蒙特卡洛 probOfLoss>0.4 / p5<0.85，或任一压力窗口回撤 >20%
    static bool detectRisk(const AdvisorContext& ctx);

    /// 网格整体不佳探测：最大化目标 best<=0，或 MaxDrawdown best>15
    static bool poorResult(double objectiveValue, st::Objective objective);
};

} // namespace st::advisor
