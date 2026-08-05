#pragma once

#include "engine/screener/factor.h"

#include <optional>
#include <string>

namespace st::screener {

/// 形态打分因子 — 统计近 lookback 根内的看涨/看跌形态，映射到 0~100 分
///
/// score = 50 + 12×(看涨-看跌形态数) + 均线排列 ±15，clamp [0,100]。
/// 数据不足（< minBars）返回 nullopt。接入方式：
///   screener.addFactor(std::make_shared<PatternFactor>(), 1.0);
class PatternFactor : public IFactor {
public:
    std::string name() const override { return "pattern_score"; }
    FactorCategory category() const override { return FactorCategory::Momentum; }

    /// 计算原始因子值（0~100），数据不足返回 nullopt
    std::optional<double> calculate(const FactorContext& ctx) const override;

    /// 最小 bar 数（默认 50）
    void setMinBars(int n) { minBars_ = n; }

    /// 形态统计回看窗口（默认 30 根）
    void setLookback(int n) { lookback_ = n; }

private:
    int minBars_ = 50;
    int lookback_ = 30;
};

} // namespace st::screener
