#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <string>
#include <optional>
#include <vector>

namespace st {

/// 因子分类
enum class FactorCategory : uint8_t {
    Momentum = 0,   // 动量
    Volatility = 1, // 波动
    Quality = 2,    // 质量
    VolumePrice = 3,// 量价
    Valuation = 4,  // 估值（预留，待 Fundamental）
    Growth = 5,     // 成长（预留）
};

/// 因子上下文 — 计算因子所需的输入
struct FactorContext {
    const StockCode* code = nullptr;
    const BarSeries* bars = nullptr;  // 日线序列
    // 财务数据指针（预留，Fundamental 接入后使用）
};

/// 单只股票的因子明细
struct FactorResult {
    std::string name;
    std::optional<double> rawValue;   // 原始值
    double score = 0.0;               // 映射后分数 (0~100)
};

/// 选股结果 — 单只股票的排名信息
struct ScreenResult {
    StockCode code;
    double totalScore = 0.0;
    std::vector<FactorResult> factorResults;
};

/// 筛选条件
struct Condition {
    std::string factorName;
    std::optional<double> minValue;
    std::optional<double> maxValue;
};

} // namespace st
