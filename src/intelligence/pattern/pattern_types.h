#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace st::pattern {

/// K 线形态枚举（16 种）
enum class PatternType : uint8_t {
    Doji,                // 十字星
    Hammer,              // 锤头线
    InvertedHammer,      // 倒锤头
    HangingMan,          // 吊颈线
    ShootingStar,        // 流星
    BullishEngulfing,    // 看涨吞没
    BearishEngulfing,    // 看跌吞没
    MorningStar,         // 早晨之星
    EveningStar,         // 黄昏之星
    ThreeWhiteSoldiers,  // 红三兵
    ThreeBlackCrows,     // 三只乌鸦
    GoldenCross,         // 均线金叉
    DeathCross,          // 均线死叉
    BullishAlignment,    // 均线多头排列
    BearishAlignment,    // 均线空头排列
    VolumeBreakout,      // 放量突破
};

/// 单条形态信号
struct PatternSignal {
    PatternType type = PatternType::Doji;
    int index = 0;                 // 触发所在 bar 的索引（0 = 最早）
    double confidence = 0.0;       // 置信度 (0~1)
    std::string name;              // 中文名
    std::string description;       // 形态描述
};

/// 检测结果
struct PatternDetectResult {
    std::vector<PatternSignal> items;
};

} // namespace st::pattern
