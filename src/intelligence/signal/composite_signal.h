#pragma once

#include "foundation/utils/indicators.h"
#include "intelligence/pattern/pattern_types.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace st::signal {

/// 综合信号评级
enum class SignalRating : uint8_t {
    StrongBuy,   // 强烈买入
    Buy,         // 买入
    Neutral,     // 观望
    Sell,        // 卖出
    StrongSell,  // 强烈卖出
};

/// 评级中文名
std::string ratingName(SignalRating rating);

/// 综合信号分项（K线形态 / 舆情情绪 / 技术指标）
struct SignalComponent {
    std::string name;       // 中文名
    double score = 0.0;     // -1 ~ +1
    double weight = 0.0;    // 该分项权重（名义权重）
    std::string detail;     // 分项说明
};

/// 综合信号
struct CompositeSignal {
    SignalRating rating = SignalRating::Neutral;
    double score = 0.0;              // 加权综合 -1 ~ +1
    double confidence = 0.0;         // 0~1（分项覆盖度 × 一致度）
    std::vector<SignalComponent> components;  // 仅含「存在」的分项
    std::string summary;             // 中文一句话结论
};

/// 融合 K线形态 + 舆情情绪 + 技术指标 → 综合信号。纯 C++17，无 Qt 依赖，可单测。
///
/// 默认权重 形态 0.4 / 情绪 0.3 / 技术 0.3（weights 可覆盖，长度≠3 时回退默认；
/// 权重内部归一化）。分项「存在」判定：
///   - 形态：patterns 非空（无形态信号 = 无证据 → 缺失折减）
///   - 情绪：summary 非空（无新闻传入默认 SentimentScore{} → 缺失）
///   - 技术：RSI/MACD/动量 任一子分可用；子分平均
/// 缺失分项按权重折减覆盖度 → 置信度下降，不阻塞综合信号。
/// 评级阈值：|score| ≥ 0.5 Strong、≥ 0.2 普通、其余 Neutral。
CompositeSignal composeSignal(
    const std::vector<pattern::PatternSignal>& patterns,
    const sentiment::SentimentScore& sentiment,
    double rsi,
    const indicators::MacdResult& macd,
    double close, double prevClose,
    const std::vector<double>& weights = {});

} // namespace st::signal
