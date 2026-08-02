#pragma once

#include "engine/screener/screener_types.h"
#include <vector>

namespace st {

/// 条件筛选器 — 基于因子值过滤股票
class ConditionFilter {
public:
    /// 添加筛选条件
    void addCondition(const Condition& condition);

    /// 清除所有条件
    void clear();

    /// 判断股票是否满足所有条件
    /// @param factorResults 该股票的各因子结果
    bool passes(const std::vector<FactorResult>& factorResults) const;

    const std::vector<Condition>& conditions() const { return conditions_; }
    bool empty() const { return conditions_.empty(); }

private:
    std::vector<Condition> conditions_;
};

} // namespace st
