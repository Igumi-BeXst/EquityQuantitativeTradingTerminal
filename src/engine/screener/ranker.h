#pragma once

#include "engine/screener/screener_types.h"
#include <vector>

namespace st {

/// 排名器 — 基于因子分数加权求和，对股票排序
class Ranker {
public:
    /// 计算综合得分（各因子分数 × 权重 之和）
    /// @param factorResults 因子结果
    /// @param weights 因子名 → 权重
    static double computeTotalScore(const std::vector<FactorResult>& factorResults,
                                    const std::vector<std::pair<std::string, double>>& weights);

    /// 对结果按总分降序排序（原地）
    static void sortByScore(std::vector<ScreenResult>& results);

    /// 取前 N 名
    static std::vector<ScreenResult> topN(std::vector<ScreenResult> results, int n);
};

} // namespace st
