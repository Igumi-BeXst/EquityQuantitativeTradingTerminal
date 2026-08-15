#include <gtest/gtest.h>
#include "engine/screener/factor_library.h"
#include "foundation/utils/datetime.h"
#include <set>

using namespace st;

namespace {

/// 构造单调上涨的日线序列
BarSeries makeRisingSeries(int n, double startPrice = 100.0, double step = 1.0) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        double price = startPrice + step * i;
        bar.open = price;
        bar.high = price + 0.5;
        bar.low = price - 0.5;
        bar.close = price;
        bar.volume = 10000;
        bars.push_back(bar);
    }
    return BarSeries(std::move(bars));
}

} // namespace

TEST(FactorTest, RocPositiveForRisingSeries) {
    auto series = makeRisingSeries(30, 100.0, 1.0);  // 100 → 129
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::RocFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    // 20日动量: (129/109 - 1) * 100 ≈ 18.3%
    EXPECT_GT(*value, 0.0);
}

TEST(FactorTest, RsiHighForStrongUpTrend) {
    auto series = makeRisingSeries(30, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::RsiFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    // 持续上涨 → RSI 应接近 70+
    EXPECT_GT(*value, 60.0);
    EXPECT_LE(*value, 100.0);
}

TEST(FactorTest, InsufficientDataReturnsNull) {
    auto series = makeRisingSeries(5);  // 数据不足
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::RocFactor factor;
    EXPECT_FALSE(factor.calculate(ctx).has_value());
}

TEST(FactorTest, VolatilityPositive) {
    auto series = makeRisingSeries(50);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::VolatilityFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);
}

TEST(FactorTest, ScoreMappingClamped) {
    factors::RocFactor factor;
    // 缺失 → 中性 50 分
    EXPECT_DOUBLE_EQ(factor.toScore(std::nullopt), 50.0);
    // 负值 → 0 分
    EXPECT_DOUBLE_EQ(factor.toScore(-5.0), 0.0);
    // 超限 → 100 分
    EXPECT_DOUBLE_EQ(factor.toScore(150.0), 100.0);
}

TEST(FactorTest, DefaultFactorSetNonEmpty) {
    auto factors = factors::defaultFactorSet();
    EXPECT_GE(factors.size(), 10u);
    // 因子名唯一
    std::set<std::string> names;
    for (const auto& [f, w] : factors) {
        names.insert(f->name());
    }
    EXPECT_EQ(names.size(), factors.size());
}

TEST(FactorTest, DefaultFactorSetExpandedTo23) {
    auto factors = factors::defaultFactorSet();
    EXPECT_EQ(factors.size(), 24u);
}

// ---- 扩充技术因子 ----

TEST(FactorTest, CciPositiveForRisingSeries) {
    auto series = makeRisingSeries(30, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::CciFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);                      // 上涨 → CCI 正
    EXPECT_GT(factor.toScore(value), 50.0);      // 分数 > 中性
}

TEST(FactorTest, WilliamsRHighForRisingSeries) {
    auto series = makeRisingSeries(30, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::WilliamsRFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    // 上涨 → 威廉%R 接近 0（超买强势）
    EXPECT_GT(*value, -50.0);
    EXPECT_GT(factor.toScore(value), 50.0);
}

TEST(FactorTest, BiasPositiveForRisingSeries) {
    auto series = makeRisingSeries(30, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::BiasFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);                      // 正乖离
    EXPECT_GT(factor.toScore(value), 50.0);
}

TEST(FactorTest, UpStreakCountsConsecutiveGains) {
    auto series = makeRisingSeries(10, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::UpStreakFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GE(*value, 5.0);                      // 至少 5 连涨
    EXPECT_DOUBLE_EQ(factor.toScore(value), 100.0);  // ≥5 连涨封顶 100
}

TEST(FactorTest, BollPosNearUpperForRisingSeries) {
    auto series = makeRisingSeries(30, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::BollPosFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.5);                      // 上涨 → 带内位置偏上轨
    EXPECT_LE(*value, 1.0);
}

TEST(FactorTest, AmplitudeSmallForSteadySeries) {
    // 恒定小振幅序列
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < 30; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        bar.open = bar.close = 100.0 + i;
        bar.high = bar.close + 0.2;
        bar.low = bar.close - 0.2;
        bar.volume = 10000;
        bars.push_back(bar);
    }
    BarSeries series(std::move(bars));
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::AmplitudeFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);
    EXPECT_LT(*value, 2.0);                      // 振幅 < 2%
}

TEST(FactorTest, MaCrossGoldenForRisingSeries) {
    auto series = makeRisingSeries(40, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::MaCrossFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 100.0);             // 上涨 → MA5 > MA20 → 金叉态
}

TEST(FactorTest, PricePosHighForRisingSeries) {
    auto series = makeRisingSeries(60, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::PricePosFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 90.0);                     // 现价接近区间顶
}

TEST(FactorTest, MaSlopePositiveForRisingSeries) {
    auto series = makeRisingSeries(40, 100.0, 1.0);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::MaSlopeFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);
    EXPECT_GT(factor.toScore(value), 50.0);
}

TEST(FactorTest, MfiHighForVolumeAccumulation) {
    // 上涨且放量序列
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    double price = 100.0;
    double vol = 10000.0;
    for (int i = 0; i < 30; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        bar.open = bar.close = price;
        bar.high = price + 0.5;
        bar.low = price - 0.5;
        bar.volume = vol;
        bars.push_back(bar);
        price += 1.0;
        vol += 2000.0;   // 放量
    }
    BarSeries series(std::move(bars));
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::MfiFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 50.0);                     // 资金流入 → MFI 高
}

TEST(FactorTest, VolPriceHealthyForRisingVolume) {
    // 涨放量序列 → 量价配合度 100
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < 10; ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        bar.open = bar.close = 100.0 + i;
        bar.high = bar.close + 0.5;
        bar.low = bar.close - 0.5;
        bar.volume = 10000 + i * 500;   // 放量上涨
        bars.push_back(bar);
    }
    BarSeries series(std::move(bars));
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;

    factors::VolPriceFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 100.0);
}

// ---- 估值因子（依赖 ctx.quote） ----

TEST(FactorTest, PeTtmLowGetsHighScore) {
    auto series = makeRisingSeries(30);
    StockCode code(Market::SH, "600519");
    QuoteFundamentals quote;
    quote.code = code;
    quote.valid = true;
    quote.peTtm = 15.0;          // 低估
    quote.marketCap = 1e10;      // 100 亿

    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    ctx.quote = &quote;

    factors::PeTtmFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(factor.toScore(value), 70.0);      // PE 15 → 85 分
}

TEST(FactorTest, PeTtmMissingQuoteDegrades) {
    auto series = makeRisingSeries(30);
    StockCode code(Market::SH, "600519");
    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    // ctx.quote 未设置 → 缺失

    factors::PeTtmFactor factor;
    EXPECT_FALSE(factor.calculate(ctx).has_value());
    EXPECT_DOUBLE_EQ(factor.toScore(std::nullopt), 50.0);  // 中性降级
}

TEST(FactorTest, MarketCapSmallPrefersHighScore) {
    auto series = makeRisingSeries(30);
    StockCode code(Market::SH, "600519");
    QuoteFundamentals quote;
    quote.code = code;
    quote.valid = true;
    quote.marketCap = 1e9;       // 10 亿（小盘）

    FactorContext ctx;
    ctx.code = &code;
    ctx.bars = &series;
    ctx.quote = &quote;

    factors::MarketCapFactor factor;
    auto value = factor.calculate(ctx);
    ASSERT_TRUE(value.has_value());
    double smallScore = factor.toScore(value);

    // 大盘股分数应更低
    QuoteFundamentals big;
    big.code = code;
    big.valid = true;
    big.marketCap = 1e12;        // 万亿
    FactorContext ctxBig;
    ctxBig.code = &code;
    ctxBig.bars = &series;
    ctxBig.quote = &big;
    auto bigValue = factor.calculate(ctxBig);
    ASSERT_TRUE(bigValue.has_value());
    EXPECT_GT(smallScore, factor.toScore(bigValue));
}
