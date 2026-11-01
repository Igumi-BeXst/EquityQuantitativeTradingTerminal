#pragma once

#include "engine/screener/factor.h"

#include <memory>
#include <string>
#include <vector>

namespace st::factors {

using FactorPtr = std::shared_ptr<IFactor>;
using WeightedFactor = std::pair<FactorPtr, double>;

/// Alpha191 全部 191 个官方公式因子（面板计算；组内等权，组总权重 1.0）
std::vector<WeightedFactor> alpha191FactorSet();

/// 量价因子（组内等权，组总权重 1.0）
std::vector<WeightedFactor> volumePriceFactorSet();

/// 基本面因子（组内等权，组总权重 1.0；缺失字段因子降级）
std::vector<WeightedFactor> fundamentalFactorSet();

/// 三组合并（默认因子集：Alpha191 + 量价 + 基本面，组间等权）
std::vector<WeightedFactor> defaultFactorSet();

/// 分类中文名（选股面板分组标题）
std::string categoryName(FactorCategory category);

} // namespace st::factors
