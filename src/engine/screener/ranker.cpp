#include "engine/screener/ranker.h"
#include <algorithm>

namespace st {

double Ranker::computeTotalScore(const std::vector<FactorResult>& factorResults,
                                 const std::vector<std::pair<std::string, double>>& weights) {
    double total = 0.0;
    double weightSum = 0.0;

    for (const auto& [name, weight] : weights) {
        auto it = std::find_if(factorResults.begin(), factorResults.end(),
            [&name](const FactorResult& fr) { return fr.name == name; });
        if (it != factorResults.end()) {
            total += it->score * weight;
            weightSum += weight;
        }
    }
    // 归一化：权重和不为 0 时除以权重和
    return weightSum > 0 ? total / weightSum : total;
}

void Ranker::sortByScore(std::vector<ScreenResult>& results) {
    std::sort(results.begin(), results.end(),
        [](const ScreenResult& a, const ScreenResult& b) {
            return a.totalScore > b.totalScore;  // 降序
        });
}

std::vector<ScreenResult> Ranker::topN(std::vector<ScreenResult> results, int n) {
    sortByScore(results);
    if (n <= 0 || static_cast<size_t>(n) >= results.size()) {
        return results;
    }
    results.resize(static_cast<size_t>(n));
    return results;
}

} // namespace st
