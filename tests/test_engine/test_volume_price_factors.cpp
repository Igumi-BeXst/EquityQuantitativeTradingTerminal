#include <gtest/gtest.h>

#include "engine/screener/factor.h"
#include "engine/screener/volume_price_factors.h"

#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace st;
using namespace st::factors;

namespace {

/// 构造 n 根合成日线：由 fill 回调逐根填写（时间字段不参与因子计算）
BarSeries makeBars(int n, const std::function<void(int, Bar&)>& fill) {
    std::vector<Bar> bars;
    bars.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        fill(i, bar);
        bars.push_back(bar);
    }
    return BarSeries(std::move(bars));
}

/// 常用单根 bar：开=收，上下影 ±0.5，成交额默认 = 收盘价 × 成交量
void setBar(Bar& bar, double close, Volume volume) {
    bar.open = close;
    bar.high = close + 0.5;
    bar.low = close - 0.5;
    bar.close = close;
    bar.volume = volume;
    bar.amount = close * static_cast<double>(volume);
}

/// 单调上涨序列：收盘价 100 + step*i
BarSeries risingBars(int n, double step = 1.0, Volume volume = 10000) {
    return makeBars(n, [step, volume](int i, Bar& bar) {
        setBar(bar, 100.0 + step * static_cast<double>(i), volume);
    });
}

FactorContext ctxOf(const BarSeries& bars) {
    FactorContext ctx;
    ctx.bars = &bars;
    return ctx;
}

}  // namespace

// ============================ 元信息 ============================

TEST(VolumePriceFactorsTest, MetadataCompleteAndUnique) {
    const std::vector<std::shared_ptr<IFactor>> factors = {
        std::make_shared<VpVolumeRatio>(),    std::make_shared<VpTurnover>(),
        std::make_shared<VpVwapBias>(),       std::make_shared<VpAmountMomentum>(),
        std::make_shared<VpObvSlope>(),       std::make_shared<VpMfi>(),
        std::make_shared<VpPriceVolumeCorr>(), std::make_shared<VpVolumeBreakout>(),
        std::make_shared<VpAmihud>(),         std::make_shared<VpEfi>(),
    };

    std::set<std::string> names;
    for (const auto& factor : factors) {
        EXPECT_FALSE(factor->name().empty());
        EXPECT_FALSE(factor->displayName().empty());
        EXPECT_FALSE(factor->description().empty());
        EXPECT_TRUE(factor->category() == FactorCategory::VolumePrice);
        names.insert(factor->name());
    }
    EXPECT_EQ(names.size(), factors.size());

    // 关键 id 稳定性
    EXPECT_EQ(VpVolumeRatio().name(), "vp_volume_ratio");
    EXPECT_EQ(VpTurnover().name(), "vp_turnover");
    EXPECT_EQ(VpEfi().name(), "vp_efi");

    // 方向：换手率越低越好，其余量价因子越大越好
    EXPECT_FALSE(VpTurnover().higherIsBetter());
    EXPECT_TRUE(VpVolumeRatio().higherIsBetter());
    EXPECT_TRUE(VpMfi().higherIsBetter());
}

TEST(VolumePriceFactorsTest, MissingBarsDegradesToNullopt) {
    FactorContext ctx;  // bars == nullptr
    EXPECT_FALSE(VpVolumeRatio().calculate(ctx).has_value());
    EXPECT_FALSE(VpTurnover().calculate(ctx).has_value());
    EXPECT_FALSE(VpVwapBias().calculate(ctx).has_value());
    EXPECT_FALSE(VpAmountMomentum().calculate(ctx).has_value());
    EXPECT_FALSE(VpObvSlope().calculate(ctx).has_value());
    EXPECT_FALSE(VpMfi().calculate(ctx).has_value());
    EXPECT_FALSE(VpPriceVolumeCorr().calculate(ctx).has_value());
    EXPECT_FALSE(VpVolumeBreakout().calculate(ctx).has_value());
    EXPECT_FALSE(VpAmihud().calculate(ctx).has_value());
    EXPECT_FALSE(VpEfi().calculate(ctx).has_value());
}

// ============================ 量比 ============================

TEST(VolumePriceFactorsTest, VolumeRatioDoublesWhenVolumeSpikes) {
    // 前 9 根量 10000，最后一根放量 20000 → 前 5 日均量 10000 → 量比 2.0
    BarSeries bars = makeBars(10, [](int i, Bar& bar) {
        setBar(bar, 100.0 + static_cast<double>(i), i == 9 ? 20000 : 10000);
    });
    const FactorContext ctx = ctxOf(bars);
    const auto value = VpVolumeRatio().calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 2.0, 1e-9);
}

TEST(VolumePriceFactorsTest, VolumeRatioInsufficientData) {
    const BarSeries bars = risingBars(5);  // 当日 + 前 5 日均量需要 6 根
    const FactorContext ctx = ctxOf(bars);
    EXPECT_FALSE(VpVolumeRatio().calculate(ctx).has_value());
}

// ============================ 换手率 ============================

TEST(VolumePriceFactorsTest, TurnoverPrefersQuoteSnapshot) {
    BarSeries bars = makeBars(1, [](int, Bar& bar) {
        setBar(bar, 100.0, 10000);
        bar.turnoverRate = 0.035;  // K 线换手率 3.5%（小数）
    });
    QuoteFundamentals quote;
    quote.valid = true;
    quote.turnoverRate = 2.4;  // 基本面快照 2.4%
    FactorContext ctx = ctxOf(bars);
    ctx.quote = &quote;

    const auto value = VpTurnover().calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 2.4);
}

TEST(VolumePriceFactorsTest, TurnoverFallsBackToBarTurnover) {
    BarSeries bars = makeBars(1, [](int, Bar& bar) {
        setBar(bar, 100.0, 10000);
        bar.turnoverRate = 0.035;  // 3.5%
    });
    FactorContext ctx = ctxOf(bars);
    // 无快照 → 回退 K 线换手率（小数转百分比）
    auto value = VpTurnover().calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 3.5, 1e-9);

    // 快照有效但字段缺失（0）→ 仍回退 K 线换手率
    QuoteFundamentals emptyQuote;
    emptyQuote.valid = true;
    ctx.quote = &emptyQuote;
    value = VpTurnover().calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 3.5, 1e-9);
}

TEST(VolumePriceFactorsTest, TurnoverMissingReturnsNull) {
    BarSeries bars = makeBars(1, [](int, Bar& bar) { setBar(bar, 100.0, 10000); });
    EXPECT_FALSE(VpTurnover().calculate(ctxOf(bars)).has_value());

    // 快照无效时同样降级
    QuoteFundamentals invalidQuote;
    invalidQuote.valid = false;
    invalidQuote.turnoverRate = 5.0;
    FactorContext ctx = ctxOf(bars);
    ctx.quote = &invalidQuote;
    EXPECT_FALSE(VpTurnover().calculate(ctx).has_value());
}

// ============================ VWAP 乖离 ============================

TEST(VolumePriceFactorsTest, VwapBiasPositiveWhenCloseAboveVwap) {
    // 成交额按「量 × (收 - 0.5)」成交 → VWAP = 收 - 0.5，现价高于 VWAP
    BarSeries bars = makeBars(10, [](int i, Bar& bar) {
        const double close = 100.0 + static_cast<double>(i);
        bar.open = close;
        bar.high = close + 0.5;
        bar.low = close - 0.5;
        bar.close = close;
        bar.volume = 10000;
        bar.amount = (close - 0.5) * static_cast<double>(bar.volume);
    });
    const auto value = VpVwapBias().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    // 最新一根：close = 109，VWAP = 108.5
    const double expected = (109.0 - 108.5) / 108.5 * 100.0;
    EXPECT_NEAR(*value, expected, 1e-9);
    EXPECT_GT(*value, 0.0);
}

TEST(VolumePriceFactorsTest, VwapBiasFallsBackToTypicalPrice) {
    // 成交额缺失（0）→ VWAP 回退典型价 (H+L+C)/3，对称影线时典型价 = 收盘价 → 乖离 0
    BarSeries bars = makeBars(3, [](int, Bar& bar) {
        bar.open = bar.close = 100.0;
        bar.high = 101.0;
        bar.low = 99.0;
        bar.volume = 10000;
        bar.amount = 0.0;
    });
    const auto value = VpVwapBias().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 0.0, 1e-12);
}

// ============================ 成交额动量 ============================

TEST(VolumePriceFactorsTest, AmountMomentumPositiveWhenAmountSpikes) {
    // 25 根成交额 1 亿，最后一根 2 亿 → 20 日均额 1.05 亿 → 动量 = 2/1.05 - 1
    BarSeries bars = makeBars(25, [](int i, Bar& bar) {
        setBar(bar, 100.0, 10000);
        bar.amount = (i == 24) ? 2e8 : 1e8;
    });
    const auto value = VpAmountMomentum().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 2.0 / 1.05 - 1.0, 1e-9);
    EXPECT_GT(*value, 0.0);
}

TEST(VolumePriceFactorsTest, AmountMomentumInsufficientData) {
    const BarSeries bars = risingBars(19);
    EXPECT_FALSE(VpAmountMomentum().calculate(ctxOf(bars)).has_value());
}

// ============================ OBV 斜率 ============================

TEST(VolumePriceFactorsTest, ObvSlopePositiveForRisingSeries) {
    // 每日涨 1、量 10000 → 近 20 日 OBV 增加 20×10000，均量 10000 → 斜率 20
    const BarSeries bars = risingBars(30, 1.0, 10000);
    const auto value = VpObvSlope().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 20.0, 1e-9);
}

TEST(VolumePriceFactorsTest, ObvSlopeNegativeForFallingSeries) {
    BarSeries bars = makeBars(30, [](int i, Bar& bar) {
        setBar(bar, 130.0 - static_cast<double>(i), 10000);
    });
    const auto value = VpObvSlope().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, -20.0, 1e-9);
}

TEST(VolumePriceFactorsTest, ObvSlopeInsufficientData) {
    const BarSeries bars = risingBars(20);  // 20 个 OBV 变化需要 21 根
    EXPECT_FALSE(VpObvSlope().calculate(ctxOf(bars)).has_value());
}

// ============================ MFI ============================

TEST(VolumePriceFactorsTest, MfiMaxOnVolumeAccumulation) {
    // 持续上涨且放量：只有资金流入 → MFI = 100
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 100.0 + static_cast<double>(i),
               10000 + static_cast<Volume>(i) * 500);
    });
    const auto value = VpMfi().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 100.0);
}

TEST(VolumePriceFactorsTest, MfiMidRangeForMixedTrend) {
    // 先涨后跌：正负资金流并存 → MFI 处于 (0, 100)
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        const double close =
            i < 10 ? 100.0 + static_cast<double>(i) : 110.0 - static_cast<double>(i - 10);
        setBar(bar, close, 10000);
    });
    const auto value = VpMfi().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);
    EXPECT_LT(*value, 100.0);
}

TEST(VolumePriceFactorsTest, MfiInsufficientData) {
    const BarSeries bars = risingBars(14);  // MFI(14) 需要 15 根
    EXPECT_FALSE(VpMfi().calculate(ctxOf(bars)).has_value());
}

// ============================ 量价相关 ============================

TEST(VolumePriceFactorsTest, PriceVolumeCorrPerfectPositive) {
    // 价量同步线性上升 → 相关系数 = 1
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 100.0 + static_cast<double>(i),
               10000 + static_cast<Volume>(i) * 500);
    });
    const auto value = VpPriceVolumeCorr().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 1.0, 1e-9);
}

TEST(VolumePriceFactorsTest, PriceVolumeCorrPerfectNegative) {
    // 价升量减 → 相关系数 = -1
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 100.0 + static_cast<double>(i),
               20000 - static_cast<Volume>(i) * 500);
    });
    const auto value = VpPriceVolumeCorr().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, -1.0, 1e-9);
}

TEST(VolumePriceFactorsTest, PriceVolumeCorrInsufficientData) {
    const BarSeries bars = risingBars(9);  // 窗口 10
    EXPECT_FALSE(VpPriceVolumeCorr().calculate(ctxOf(bars)).has_value());
}

// ============================ 量能突破 ============================

TEST(VolumePriceFactorsTest, VolumeBreakoutAtWindowMaxIsOne) {
    // 最后一根量 25000 为 20 日最大 → 比值 1.0
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 100.0, i == 19 ? 25000 : 10000);
    });
    const auto value = VpVolumeBreakout().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 1.0);
}

TEST(VolumePriceFactorsTest, VolumeBreakoutHalfWhenVolumeShrinks) {
    // 最后一根量 5000，20 日最大仍为 10000 → 比值 0.5
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 100.0, i == 19 ? 5000 : 10000);
    });
    const auto value = VpVolumeBreakout().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 0.5);
}

TEST(VolumePriceFactorsTest, VolumeBreakoutInsufficientData) {
    const BarSeries bars = risingBars(19);
    EXPECT_FALSE(VpVolumeBreakout().calculate(ctxOf(bars)).has_value());
}

// ============================ Amihud 非流动性 ============================

TEST(VolumePriceFactorsTest, AmihudPositiveForSteadyRise) {
    // 每日涨 1%、成交额 10 亿 → |收益| / 成交额(亿) = 0.01 / 10 = 0.001
    BarSeries bars = makeBars(25, [](int i, Bar& bar) {
        setBar(bar, 100.0 * std::pow(1.01, static_cast<double>(i)), 10000);
        bar.amount = 1e9;
    });
    const auto value = VpAmihud().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 0.001, 1e-9);
}

TEST(VolumePriceFactorsTest, AmihudSkipsZeroAmountBars) {
    // 最后 10 根成交额缺失：20 日窗口内有效样本恰好 10 个 → 仍可计算
    BarSeries bars = makeBars(25, [](int i, Bar& bar) {
        setBar(bar, 100.0 * std::pow(1.01, static_cast<double>(i)), 10000);
        bar.amount = (i >= 15) ? 0.0 : 1e9;
    });
    const auto value = VpAmihud().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 0.001, 1e-9);
}

TEST(VolumePriceFactorsTest, AmihudInsufficientValidSamples) {
    // 20 日窗口内仅 5 个有效样本（< 10）→ 降级
    BarSeries bars = makeBars(25, [](int i, Bar& bar) {
        setBar(bar, 100.0 + static_cast<double>(i), 10000);
        bar.amount = (i >= 10) ? 0.0 : 1e9;
    });
    EXPECT_FALSE(VpAmihud().calculate(ctxOf(bars)).has_value());
}

TEST(VolumePriceFactorsTest, AmihudInsufficientBars) {
    // 20 个日收益需要 21 根 K 线
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 100.0 + static_cast<double>(i), 10000);
        bar.amount = 1e9;
    });
    EXPECT_FALSE(VpAmihud().calculate(ctxOf(bars)).has_value());
}

// ============================ Elder 力度指数 ============================

TEST(VolumePriceFactorsTest, EfiPositiveForRisingSeries) {
    // 每日涨 1、量 10000 → EFI = 10000
    const BarSeries bars = risingBars(20, 1.0, 10000);
    const auto value = VpEfi().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 10000.0);
}

TEST(VolumePriceFactorsTest, EfiNegativeForFallingSeries) {
    BarSeries bars = makeBars(20, [](int i, Bar& bar) {
        setBar(bar, 130.0 - static_cast<double>(i), 10000);
    });
    const auto value = VpEfi().calculate(ctxOf(bars));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, -10000.0);
}

TEST(VolumePriceFactorsTest, EfiInsufficientData) {
    const BarSeries bars = risingBars(13);  // 13 个差分需要 14 根
    EXPECT_FALSE(VpEfi().calculate(ctxOf(bars)).has_value());
}
