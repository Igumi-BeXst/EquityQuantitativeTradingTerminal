#include "engine/screener/condition_filter.h"
#include <algorithm>

namespace st {

void ConditionFilter::addCondition(const Condition& condition) {
    conditions_.push_back(condition);
}

void ConditionFilter::clear() {
    conditions_.clear();
}

bool ConditionFilter::passes(const std::vector<FactorResult>& factorResults) const {
    if (conditions_.empty()) return true;

    for (const auto& cond : conditions_) {
        // 查找对应因子值
        auto it = std::find_if(factorResults.begin(), factorResults.end(),
            [&cond](const FactorResult& fr) { return fr.name == cond.factorName; });
        if (it == factorResults.end()) {
            return false;  // 因子缺失则不通过
        }
        if (!it->rawValue.has_value()) {
            return false;
        }
        double v = *it->rawValue;
        if (cond.minValue.has_value() && v < *cond.minValue) return false;
        if (cond.maxValue.has_value() && v > *cond.maxValue) return false;
    }
    return true;
}

} // namespace st
