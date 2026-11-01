#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include "data/quote_fundamentals.h"
#include <string>
#include <optional>
#include <vector>

namespace st {

/// 因子分类（选股面板按此三分组展示）
enum class FactorCategory : uint8_t {
    Alpha191 = 0,    // Alpha191 因子（国泰君安 191 因子子集）
    VolumePrice = 1, // 量价因子（成交量/成交额与价格的关系）
    Fundamental = 2, // 基本面因子（估值/盈利/成长/财务质量）
};

/// 因子上下文 — 计算因子所需的输入
struct FactorContext {
    const StockCode* code = nullptr;
    const BarSeries* bars = nullptr;  // 日线序列（含 amount，用于 VWAP）
    /// 基本面快照（估值/财务因子用；数据源不支持或未拉取时为 nullptr）
    const QuoteFundamentals* quote = nullptr;
};

/// 单只股票的因子明细
struct FactorResult {
    std::string name;
    std::optional<double> rawValue;   // 原始值
    double score = 0.0;               // 截面标准化后分数 (0~100)
};

/// 选股结果 — 单只股票的排名信息
struct ScreenResult {
    StockCode code;
    double totalScore = 0.0;
    std::vector<FactorResult> factorResults;
};

/// 筛选条件（作用于截面标准化后的因子分数 0~100）
struct Condition {
    std::string factorName;
    std::optional<double> minValue;
    std::optional<double> maxValue;
};

} // namespace st
