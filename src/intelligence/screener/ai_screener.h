#pragma once

#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <optional>
#include <string>
#include <vector>

namespace st::screener {

/// AI 选股配置
struct AiScreenerConfig {
    /// 分项权重（形态/情绪/技术），对齐 signal::composeSignal，默认 {0.4,0.3,0.3}
    std::vector<double> weights = {0.4, 0.3, 0.3};
    bool usePattern = true;    // false = 不计算形态分项（UI 取消「形态」勾选）
    bool useSentiment = true;  // false = 不计算情绪分项（UI 取消「情绪」勾选/离线）
};

/// 单只股票的 AI 评分（0~100，越高越强）
struct AiScore {
    StockCode code;
    double compositeScore = 0.0;            // 综合分（composeSignal score → 0~100）
    std::optional<double> patternScore;     // 形态分项（缺失 = 无形态信号）
    std::optional<double> sentimentScore;   // 情绪分项（缺失 = 无情绪数据）
    std::optional<double> technicalScore;   // 技术分项（缺失 = 指标不足）
    std::string summary;                    // 一句话结论
};

/// 股票池 AI 评分排序。纯 C++17，无 Qt 依赖，可单测。
///
/// 输入三数组与 pool 等长对齐（取最短前缀防御）；bars 为空 → 该股跳过。
/// 内部复用 signal::composeSignal（形态 detectAt(3) + RSI/MACD/动量 + 情绪）
/// 的分项逻辑，综合分与分项 = (score+1)/2×100 映射。
/// usePattern/useSentiment 控制分项参与（对应分项缺失 → 权重折减，技术指标恒参与）。
/// 输出按 compositeScore 降序。
std::vector<AiScore> runAiScreener(
    const std::vector<StockCode>& pool,
    const std::vector<std::vector<Bar>>& barsByCode,
    const std::vector<std::optional<sentiment::SentimentScore>>& sentiments,
    const AiScreenerConfig& cfg);

} // namespace st::screener
