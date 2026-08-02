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
