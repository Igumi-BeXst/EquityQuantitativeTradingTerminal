#include <gtest/gtest.h>
#include "data/akshare_provider.h"

#include <string>

using namespace st;

// 东财 ulist.np/get 单条记录解析（茅台实测 payload 结构）
TEST(AKShareFundamentalsTest, ParsesUlistItem) {
    const std::string item =
        R"({"f8":0.2,"f12":"600519","f14":"贵州茅台","f20":1635794278989,)"
        R"("f21":1635794278989,"f38":1250081601.0,"f39":1250081601.0,"f115":19.78})";
    const auto f = AKShareProvider::parseFundamentals(item, StockCode("600519"));
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->turnoverRate, 0.2);
    EXPECT_DOUBLE_EQ(f->peStatic, 19.78);
    EXPECT_DOUBLE_EQ(f->marketCap, 1635794278989.0);
    EXPECT_DOUBLE_EQ(f->floatCap, 1635794278989.0);
    EXPECT_DOUBLE_EQ(f->totalShares, 1250081601.0);
    EXPECT_DOUBLE_EQ(f->floatShares, 1250081601.0);
    EXPECT_DOUBLE_EQ(f->turnoverRateReal, 0.2);  // 全流通 → 等于换手率
    EXPECT_TRUE(f->valid);
}

// 东财 ulist.np/get 扩展字段：PB/ROE/营收/净利/毛利率/负债率（基本面因子数据源）
TEST(AKShareFundamentalsTest, ParsesExtendedFundamentalFields) {
    const std::string item =
        R"({"f8":0.48,"f9":20.24,"f12":"600519","f20":1740176092672,)"
        R"("f21":1740176092672,"f23":6.42,"f37":24.47,"f38":1250081601.0,)"
        R"("f39":1250081601.0,"f40":133283652508.95,"f41":1.8179,)"
        R"("f45":64467425981.95,"f46":-0.2465,"f49":89.5811,"f57":11.9016,)"
        R"("f115":21.18})";
    const auto f = AKShareProvider::parseFundamentals(item, StockCode("600519"));
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->peDynamic, 20.24);
    EXPECT_DOUBLE_EQ(f->peStatic, 21.18);
    EXPECT_DOUBLE_EQ(f->peTtm, 20.24);  // 动态 PE 优先，兼容旧字段
    EXPECT_DOUBLE_EQ(f->pb, 6.42);
    EXPECT_DOUBLE_EQ(f->roe, 24.47);
    EXPECT_DOUBLE_EQ(f->revenue, 133283652508.95);
    EXPECT_DOUBLE_EQ(f->revenueYoY, 1.8179);
    EXPECT_DOUBLE_EQ(f->netProfit, 64467425981.95);
    EXPECT_DOUBLE_EQ(f->netProfitYoY, -0.2465);
    EXPECT_DOUBLE_EQ(f->grossMargin, 89.5811);
    EXPECT_DOUBLE_EQ(f->debtRatio, 11.9016);
}

// 部分流通：换手率(实) = 换手率 × 流通股/总股本
TEST(AKShareFundamentalsTest, ComputesTurnoverRealForPartialFloat) {
    const std::string item =
        R"({"f8":10.0,"f12":"600000","f20":100000000000,"f21":50000000000,)"
        R"("f38":10000000000.0,"f39":5000000000.0,"f115":5.0})";
    const auto f = AKShareProvider::parseFundamentals(item, StockCode("600000"));
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->turnoverRateReal, 5.0);  // 10 × 0.5
}

TEST(AKShareFundamentalsTest, MalformedReturnsNullopt) {
    EXPECT_FALSE(AKShareProvider::parseFundamentals("not json",
                                                    StockCode("600519")).has_value());
    // 字段全缺失 → invalid
    EXPECT_FALSE(AKShareProvider::parseFundamentals("{}",
                                                    StockCode("600519")).has_value());
    // 字段全为 "-"（停牌/退市标记）→ invalid
    const std::string dashes = R"({"f8":"-","f20":"-","f38":"-"})";
    EXPECT_FALSE(AKShareProvider::parseFundamentals(dashes,
                                                    StockCode("600519")).has_value());
}

// 非沪深市场直接返回 nullopt（不触发网络）
TEST(AKShareFundamentalsTest, UnsupportedMarketNulloptNoNetwork) {
    AKShareProvider p;
    EXPECT_FALSE(p.getQuoteFundamentals(
        StockCode(Market::BJ, "830799")).has_value());
}
