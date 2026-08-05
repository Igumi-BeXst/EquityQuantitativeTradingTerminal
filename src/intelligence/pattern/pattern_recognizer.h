#pragma once

#include "intelligence/pattern/pattern_types.h"
#include "foundation/bar.h"
#include <string>
#include <vector>

namespace st::pattern {

/// K 线形态识别器 — 纯 C++ 规则，确定性、可单测
///
/// 在升序 bar 序列上逐根检测 16 种形态，复用 st::indicators::sma 判均线状态。
/// 阈值可调：均线周期、放量倍数、最小 bar 数。
class PatternRecognizer {
public:
    PatternRecognizer() = default;

    /// 均线周期（金叉/死叉/排列判断用），默认 5/20
    void setMaPeriods(int fast, int slow) {
        fastPeriod_ = fast;
        slowPeriod_ = slow;
    }

    /// 放量倍数（放量突破用），默认 2.0
    void setVolumeRatio(double ratio) { volumeRatio_ = ratio; }

    /// 最小 bar 数（不足不检测），默认 40
    void setMinBars(int minBars) { minBars_ = minBars; }

    /// 全量检测：序列中每根 bar 上触发的形态
    PatternDetectResult detect(const BarSeries& bars) const;

    /// 仅检测最后 lookback 根（默认 3 根，适合实时/选股场景）
    PatternDetectResult detectAt(const BarSeries& bars, int lookback = 3) const;

    /// 形态中文名
    static std::string typeName(PatternType type);

    /// 形态方向（Doji 为中性，两者均 false）
    static bool isBullish(PatternType type);
    static bool isBearish(PatternType type);

private:
    void detectAtBar(const BarSeries& bars, int i,
                     const std::vector<double>& maFast,
                     const std::vector<double>& maSlow,
                     std::vector<PatternSignal>& out) const;

    void addSignal(PatternType type, int index, double confidence,
                   const std::string& description,
                   std::vector<PatternSignal>& out) const;

    int fastPeriod_ = 5;
    int slowPeriod_ = 20;
    double volumeRatio_ = 2.0;
    int minBars_ = 40;
};

} // namespace st::pattern
