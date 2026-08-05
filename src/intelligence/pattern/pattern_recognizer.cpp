#include "intelligence/pattern/pattern_recognizer.h"
#include "foundation/utils/indicators.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace st::pattern {

namespace {

/// 影线类形态阈值
constexpr double kBodyRangeRatio = 0.35;     // 实体 ≤ 35% 振幅
constexpr double kShadowBodyMult = 2.0;      // 长影线 ≥ 2× 实体
constexpr double kShadowRangeRatio = 0.35;   // 对侧影线 ≤ 35% 振幅

bool isBullishBar(const Bar& b) { return b.close > b.open; }
bool isBearishBar(const Bar& b) { return b.close < b.open; }

bool isDojiBar(const Bar& b) {
    const double range = b.high - b.low;
    if (range <= 0.0) return false;
    return std::abs(b.close - b.open) <= 0.1 * range;
}

} // namespace

PatternDetectResult PatternRecognizer::detect(const BarSeries& bars) const {
    PatternDetectResult result;
    const int n = static_cast<int>(bars.size());
    if (n < minBars_) return result;
    const auto closes = bars.closes();
    const auto maFast = st::indicators::sma(closes, fastPeriod_);
    const auto maSlow = st::indicators::sma(closes, slowPeriod_);
    for (int i = minBars_ - 1; i < n; ++i) {
        detectAtBar(bars, i, maFast, maSlow, result.items);
    }
    return result;
}

PatternDetectResult PatternRecognizer::detectAt(const BarSeries& bars, int lookback) const {
    PatternDetectResult result;
    const int n = static_cast<int>(bars.size());
    if (n < minBars_) return result;
    const auto closes = bars.closes();
    const auto maFast = st::indicators::sma(closes, fastPeriod_);
    const auto maSlow = st::indicators::sma(closes, slowPeriod_);
    const int start = std::max(minBars_ - 1, n - std::max(lookback, 1));
    for (int i = start; i < n; ++i) {
        detectAtBar(bars, i, maFast, maSlow, result.items);
    }
    return result;
}

std::string PatternRecognizer::typeName(PatternType type) {
    switch (type) {
        case PatternType::Doji:               return "十字星";
        case PatternType::Hammer:             return "锤头线";
        case PatternType::InvertedHammer:     return "倒锤头";
        case PatternType::HangingMan:         return "吊颈线";
        case PatternType::ShootingStar:       return "流星";
        case PatternType::BullishEngulfing:   return "看涨吞没";
        case PatternType::BearishEngulfing:   return "看跌吞没";
        case PatternType::MorningStar:        return "早晨之星";
        case PatternType::EveningStar:        return "黄昏之星";
        case PatternType::ThreeWhiteSoldiers: return "红三兵";
        case PatternType::ThreeBlackCrows:    return "三只乌鸦";
        case PatternType::GoldenCross:        return "均线金叉";
        case PatternType::DeathCross:         return "均线死叉";
        case PatternType::BullishAlignment:   return "均线多头排列";
        case PatternType::BearishAlignment:   return "均线空头排列";
        case PatternType::VolumeBreakout:     return "放量突破";
    }
    return "未知形态";
}

bool PatternRecognizer::isBullish(PatternType type) {
    switch (type) {
        case PatternType::Hammer:
        case PatternType::InvertedHammer:
        case PatternType::BullishEngulfing:
        case PatternType::MorningStar:
        case PatternType::ThreeWhiteSoldiers:
        case PatternType::GoldenCross:
        case PatternType::BullishAlignment:
        case PatternType::VolumeBreakout:
            return true;
        default:
            return false;
    }
}

bool PatternRecognizer::isBearish(PatternType type) {
    switch (type) {
        case PatternType::HangingMan:
        case PatternType::ShootingStar:
        case PatternType::BearishEngulfing:
        case PatternType::EveningStar:
        case PatternType::ThreeBlackCrows:
        case PatternType::DeathCross:
        case PatternType::BearishAlignment:
            return true;
        default:
            return false;
    }
}

void PatternRecognizer::addSignal(PatternType type, int index, double confidence,
                                  const std::string& description,
                                  std::vector<PatternSignal>& out) const {
    out.push_back({type, index, confidence, typeName(type), description});
}

void PatternRecognizer::detectAtBar(const BarSeries& bars, int i,
                                    const std::vector<double>& maFast,
                                    const std::vector<double>& maSlow,
                                    std::vector<PatternSignal>& out) const {
    const Bar& bar = bars[i];
    if (!bar.isValid()) return;
    const double range = bar.high - bar.low;
    if (range <= 0.0) return;

    const bool bullish = isBullishBar(bar);
    const bool bearish = isBearishBar(bar);
    const bool doji = isDojiBar(bar);
    const double body = std::abs(bar.close - bar.open);

    // 十字星
    if (doji) {
        addSignal(PatternType::Doji, i, 0.6, "开盘价与收盘价几乎相等，多空力量均衡", out);
    }

    if (i > 0) {
        const Bar& prev = bars[i - 1];
        const double prevBody = std::abs(prev.close - prev.open);
        const bool prevBullish = isBullishBar(prev);
        const bool prevBearish = isBearishBar(prev);

        // 影线类形态（不与十字星重复）
        if (!doji && body > 0.0) {
            const double upper = bar.high - std::max(bar.open, bar.close);
            const double lower = std::min(bar.open, bar.close) - bar.low;
            const bool lowerLong = lower >= kShadowBodyMult * body;
            const bool upperLong = upper >= kShadowBodyMult * body;
            const bool lowerShort = lower <= kShadowRangeRatio * range;
            const bool upperShort = upper <= kShadowRangeRatio * range;
            const bool closeHigh = bar.close >= bar.low + 0.6 * range;
            const bool closeLow = bar.close <= bar.high - 0.6 * range;

            // 锤头线（下跌末端 + 长下影）
            if (prevBearish && lowerLong && upperShort && closeHigh) {
                addSignal(PatternType::Hammer, i, 0.75,
                          "下跌末端出现长下影线，可能见底回升", out);
            }
            // 倒锤头（下跌末端 + 长上影）
            if (prevBearish && upperLong && lowerShort && closeLow) {
                addSignal(PatternType::InvertedHammer, i, 0.75,
                          "下跌末端出现长上影线，多方试探反攻", out);
            }
            // 吊颈线（上涨末端 + 长下影）
            if (prevBullish && lowerLong && upperShort && closeHigh) {
                addSignal(PatternType::HangingMan, i, 0.75,
                          "上涨末端出现长下影线，警惕见顶回落", out);
            }
            // 流星（上涨末端 + 长上影）
            if (prevBullish && upperLong && lowerShort && closeLow) {
                addSignal(PatternType::ShootingStar, i, 0.75,
                          "上涨末端出现长上影线，上攻受阻", out);
            }
        }

        // 吞没形态
        if (prevBody > 0.0) {
            if (prevBearish && bullish && bar.open <= prev.close && bar.close >= prev.open) {
                double conf = 0.75;
                if (body >= 2.0 * prevBody) conf += 0.1;
                addSignal(PatternType::BullishEngulfing, i, std::min(conf, 0.95),
                          "阳线实体完全吞没前一根阴线，反转向上", out);
            }
            if (prevBullish && bearish && bar.open >= prev.close && bar.close <= prev.open) {
                double conf = 0.75;
                if (body >= 2.0 * prevBody) conf += 0.1;
                addSignal(PatternType::BearishEngulfing, i, std::min(conf, 0.95),
                          "阴线实体完全吞没前一根阳线，转向下", out);
            }
        }

        // 星形组合（早晨/黄昏之星）需要 3 根
        if (i >= 2) {
            const Bar& prev2 = bars[i - 2];
            const double prev2Body = std::abs(prev2.close - prev2.open);
            if (prev2Body > 0.0 && prevBody <= 0.3 * prev2Body) {
                const double mid2 = (prev2.open + prev2.close) / 2.0;
                // 早晨之星
                if (isBearishBar(prev2) && bullish && prev.close < prev2.close &&
                    bar.close > mid2) {
                    addSignal(PatternType::MorningStar, i, 0.8,
                              "下跌后小实体星线止跌，随后阳线收复，见底反转", out);
                }
                // 黄昏之星
                if (isBullishBar(prev2) && bearish && prev.close > prev2.close &&
                    bar.close < mid2) {
                    addSignal(PatternType::EveningStar, i, 0.8,
                              "上涨后小实体星线滞涨，随后阴线下杀，见顶反转", out);
                }
            }
        }
    }

    // 红三兵 / 三只乌鸦（连续 3 根）
    if (i >= 2) {
        const Bar& b0 = bars[i - 2];
        const Bar& b1 = bars[i - 1];
        const double r0 = b0.high - b0.low;
        const double r1 = b1.high - b1.low;
        if (r0 > 0.0 && r1 > 0.0) {
            if (isBullishBar(b0) && isBullishBar(b1) && bullish &&
                b1.close > b0.close && bar.close > b1.close &&
                b0.close >= b0.low + 0.6 * r0 &&
                b1.close >= b1.low + 0.6 * r1 &&
                bar.close >= bar.low + 0.6 * range) {
                addSignal(PatternType::ThreeWhiteSoldiers, i, 0.8,
                          "连续三根阳线稳步上攻，多方强势", out);
            }
            if (isBearishBar(b0) && isBearishBar(b1) && bearish &&
                b1.close < b0.close && bar.close < b1.close &&
                b0.close <= b0.low + 0.4 * r0 &&
                b1.close <= b1.low + 0.4 * r1 &&
                bar.close <= bar.low + 0.4 * range) {
                addSignal(PatternType::ThreeBlackCrows, i, 0.8,
                          "连续三根阴线步步下探，空方主导", out);
            }
        }
    }

    // 均线金叉/死叉 + 排列
    const bool maCurValid = !std::isnan(maFast[i]) && !std::isnan(maSlow[i]);
    const bool maPrevValid =
        i > 0 && !std::isnan(maFast[i - 1]) && !std::isnan(maSlow[i - 1]);
    if (maCurValid && maPrevValid) {
        if (maFast[i - 1] <= maSlow[i - 1] && maFast[i] > maSlow[i]) {
            addSignal(PatternType::GoldenCross, i, 0.8, "短均线上穿长均线，趋势转多", out);
        }
        if (maFast[i - 1] >= maSlow[i - 1] && maFast[i] < maSlow[i]) {
            addSignal(PatternType::DeathCross, i, 0.8, "短均线下穿长均线，趋势转空", out);
        }
        if (maFast[i] > maSlow[i] && maFast[i - 1] > maSlow[i - 1] &&
            bar.close > maSlow[i]) {
            addSignal(PatternType::BullishAlignment, i, 0.7,
                      "短均线持续位于长均线上方，多头排列", out);
        }
        if (maFast[i] < maSlow[i] && maFast[i - 1] < maSlow[i - 1] &&
            bar.close < maSlow[i]) {
            addSignal(PatternType::BearishAlignment, i, 0.7,
                      "短均线持续位于长均线下方，空头排列", out);
        }
    }

    // 放量突破（收盘突破前 10 根高点 + 放量）
    constexpr int kBreakoutWindow = 10;
    if (i >= kBreakoutWindow && bullish) {
        double prevHigh = bars[i - 1].high;
        for (int j = i - 2; j >= i - kBreakoutWindow; --j) {
            prevHigh = std::max(prevHigh, bars[j].high);
        }
        double avgVol = 0.0;
        for (int j = i - kBreakoutWindow; j < i; ++j) {
            avgVol += static_cast<double>(bars[j].volume);
        }
        avgVol /= kBreakoutWindow;
        if (avgVol > 0.0 && bar.close > prevHigh &&
            static_cast<double>(bar.volume) >= volumeRatio_ * avgVol) {
            addSignal(PatternType::VolumeBreakout, i, 0.75,
                      "放量突破前期高点，资金进场", out);
        }
    }
}

} // namespace st::pattern
