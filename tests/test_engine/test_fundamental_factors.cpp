#include <gtest/gtest.h>

#include "engine/screener/factor.h"
#include "engine/screener/fundamental_factors.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace st;
using namespace st::factors;

namespace {

/// 构造一份有效的基本面快照（各字段默认 0 = 缺失）
QuoteFundamentals makeQuote() {
    QuoteFundamentals quote;
    quote.valid = true;
    return quote;
}

FactorContext ctxOf(const QuoteFundamentals& quote) {
    FactorContext ctx;
    ctx.quote = &quote;
    return ctx;
}

}  // namespace

// ============================ 元信息 ============================

TEST(FundamentalFactorsTest, MetadataCompleteAndUnique) {
    const std::vector<std::shared_ptr<IFactor>> factors = {
        std::make_shared<FundPe>(),          std::make_shared<FundPb>(),
        std::make_shared<FundRoe>(),         std::make_shared<FundRevenueYoy>(),
        std::make_shared<FundProfitYoy>(),   std::make_shared<FundGrossMargin>(),
        std::make_shared<FundDebtRatio>(),   std::make_shared<FundMarketCap>(),
    };

    std::set<std::string> names;
    for (const auto& factor : factors) {
        EXPECT_FALSE(factor->name().empty());
        EXPECT_FALSE(factor->displayName().empty());
        EXPECT_FALSE(factor->description().empty());
        EXPECT_TRUE(factor->category() == FactorCategory::Fundamental);
        names.insert(factor->name());
    }
    EXPECT_EQ(names.size(), factors.size());

    // 关键 id 稳定性
    EXPECT_EQ(FundPe().name(), "fund_pe");
    EXPECT_EQ(FundRoe().name(), "fund_roe");
    EXPECT_EQ(FundMarketCap().name(), "fund_market_cap");

    // 方向：估值/杠杆/市值越低越好，盈利/成长越高越好
    EXPECT_FALSE(FundPe().higherIsBetter());
    EXPECT_FALSE(FundPb().higherIsBetter());
    EXPECT_FALSE(FundDebtRatio().higherIsBetter());
    EXPECT_FALSE(FundMarketCap().higherIsBetter());
    EXPECT_TRUE(FundRoe().higherIsBetter());
    EXPECT_TRUE(FundRevenueYoy().higherIsBetter());
    EXPECT_TRUE(FundProfitYoy().higherIsBetter());
    EXPECT_TRUE(FundGrossMargin().higherIsBetter());
}

TEST(FundamentalFactorsTest, MissingOrInvalidQuoteDegradesToNullopt) {
    // 无快照
    const FactorContext noQuote;
    EXPECT_FALSE(FundPe().calculate(noQuote).has_value());
    EXPECT_FALSE(FundPb().calculate(noQuote).has_value());
    EXPECT_FALSE(FundRoe().calculate(noQuote).has_value());
    EXPECT_FALSE(FundRevenueYoy().calculate(noQuote).has_value());
    EXPECT_FALSE(FundProfitYoy().calculate(noQuote).has_value());
    EXPECT_FALSE(FundGrossMargin().calculate(noQuote).has_value());
    EXPECT_FALSE(FundDebtRatio().calculate(noQuote).has_value());
    EXPECT_FALSE(FundMarketCap().calculate(noQuote).has_value());

    // 快照 valid=false，即便字段有值也不可用
    QuoteFundamentals invalidQuote;
    invalidQuote.valid = false;
    invalidQuote.peDynamic = 10.0;
    invalidQuote.pb = 1.5;
    invalidQuote.roe = 12.0;
    invalidQuote.revenueYoY = 20.0;
    invalidQuote.netProfitYoY = 30.0;
    invalidQuote.grossMargin = 40.0;
    invalidQuote.debtRatio = 35.0;
    invalidQuote.marketCap = 1e10;
    const FactorContext ctx = ctxOf(invalidQuote);
    EXPECT_FALSE(FundPe().calculate(ctx).has_value());
    EXPECT_FALSE(FundPb().calculate(ctx).has_value());
    EXPECT_FALSE(FundRoe().calculate(ctx).has_value());
    EXPECT_FALSE(FundRevenueYoy().calculate(ctx).has_value());
    EXPECT_FALSE(FundProfitYoy().calculate(ctx).has_value());
    EXPECT_FALSE(FundGrossMargin().calculate(ctx).has_value());
    EXPECT_FALSE(FundDebtRatio().calculate(ctx).has_value());
    EXPECT_FALSE(FundMarketCap().calculate(ctx).has_value());
}

// ============================ PE ============================

TEST(FundamentalFactorsTest, PePrefersDynamicThenFallsBack) {
    QuoteFundamentals quote = makeQuote();
    quote.peDynamic = 12.0;
    quote.peTtm = 20.0;
    quote.peStatic = 25.0;
    auto value = FundPe().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 12.0);

    // 动态 PE 缺失 → 回退 TTM
    quote.peDynamic = 0.0;
    value = FundPe().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 20.0);

    // 动态/TTM 缺失 → 回退静态
    quote.peTtm = 0.0;
    value = FundPe().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 25.0);
}

TEST(FundamentalFactorsTest, PeLossCompanyDegradesToNullopt) {
    // 亏损股动态 PE 为负 → 视为缺失
    QuoteFundamentals quote = makeQuote();
    quote.peDynamic = -8.0;
    quote.peTtm = -12.0;
    EXPECT_FALSE(FundPe().calculate(ctxOf(quote)).has_value());

    // 全部为 0（数据源无值）→ 缺失
    const QuoteFundamentals emptyQuote = makeQuote();
    EXPECT_FALSE(FundPe().calculate(ctxOf(emptyQuote)).has_value());
}

// ============================ PB ============================

TEST(FundamentalFactorsTest, PbRequiresPositiveValue) {
    QuoteFundamentals quote = makeQuote();
    quote.pb = 2.5;
    auto value = FundPb().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 2.5);

    quote.pb = 0.0;  // 缺失
    EXPECT_FALSE(FundPb().calculate(ctxOf(quote)).has_value());

    quote.pb = -1.0;  // 净资产为负的极端情况，视为不可用
    EXPECT_FALSE(FundPb().calculate(ctxOf(quote)).has_value());
}

// ============================ ROE ============================

TEST(FundamentalFactorsTest, RoeAllowsNegativeValue) {
    QuoteFundamentals quote = makeQuote();
    quote.roe = 15.2;
    auto value = FundRoe().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 15.2);

    // 亏损（负 ROE）是有效数据，不能当缺失
    quote.roe = -3.5;
    value = FundRoe().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, -3.5);

    // 0 为缺失哨兵
    quote.roe = 0.0;
    EXPECT_FALSE(FundRoe().calculate(ctxOf(quote)).has_value());
}

// ============================ 营收同比 ============================

TEST(FundamentalFactorsTest, RevenueYoyAllowsNegativeGrowth) {
    QuoteFundamentals quote = makeQuote();
    quote.revenueYoY = 23.4;
    auto value = FundRevenueYoy().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 23.4);

    quote.revenueYoY = -12.5;  // 营收下滑是有效数据
    value = FundRevenueYoy().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, -12.5);

    quote.revenueYoY = 0.0;
    EXPECT_FALSE(FundRevenueYoy().calculate(ctxOf(quote)).has_value());
}

// ============================ 净利同比 ============================

TEST(FundamentalFactorsTest, ProfitYoyAllowsNegativeGrowth) {
    QuoteFundamentals quote = makeQuote();
    quote.netProfitYoY = 45.6;
    auto value = FundProfitYoy().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 45.6);

    quote.netProfitYoY = -80.0;  // 净利大幅下滑/转亏仍是有效数据
    value = FundProfitYoy().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, -80.0);

    quote.netProfitYoY = 0.0;
    EXPECT_FALSE(FundProfitYoy().calculate(ctxOf(quote)).has_value());
}

// ============================ 毛利率 ============================

TEST(FundamentalFactorsTest, GrossMarginRequiresPositiveValue) {
    QuoteFundamentals quote = makeQuote();
    quote.grossMargin = 35.6;
    auto value = FundGrossMargin().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 35.6);

    quote.grossMargin = 0.0;   // 缺失
    EXPECT_FALSE(FundGrossMargin().calculate(ctxOf(quote)).has_value());
    quote.grossMargin = -5.0;  // 负毛利率视为不可用
    EXPECT_FALSE(FundGrossMargin().calculate(ctxOf(quote)).has_value());
}

// ============================ 资产负债率 ============================

TEST(FundamentalFactorsTest, DebtRatioZeroTreatedAsMissing) {
    QuoteFundamentals quote = makeQuote();
    quote.debtRatio = 45.0;
    auto value = FundDebtRatio().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 45.0);

    quote.debtRatio = 0.0;  // 数据源无值时为 0
    EXPECT_FALSE(FundDebtRatio().calculate(ctxOf(quote)).has_value());
}

// ============================ 总市值 ============================

TEST(FundamentalFactorsTest, MarketCapRequiresPositiveValue) {
    QuoteFundamentals quote = makeQuote();
    quote.marketCap = 1e10;  // 100 亿
    auto value = FundMarketCap().calculate(ctxOf(quote));
    ASSERT_TRUE(value.has_value());
    EXPECT_DOUBLE_EQ(*value, 1e10);

    quote.marketCap = 0.0;
    EXPECT_FALSE(FundMarketCap().calculate(ctxOf(quote)).has_value());
}
