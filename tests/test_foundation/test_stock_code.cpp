#include <gtest/gtest.h>
#include "foundation/stock_code.h"

using namespace st;

TEST(StockCodeTest, DefaultIsInvalid) {
    StockCode sc;
    EXPECT_FALSE(sc.isValid());
}

TEST(StockCodeTest, ConstructWithMarketAndCode) {
    StockCode sc(Market::SH, "600519");
    EXPECT_TRUE(sc.isValid());
    EXPECT_EQ(sc.market(), Market::SH);
    EXPECT_EQ(sc.code(), "600519");
}

TEST(StockCodeTest, FullCodeWithPrefix) {
    StockCode sc(Market::SH, "600519");
    EXPECT_EQ(sc.fullCode(), "SH600519");
    EXPECT_EQ(sc.displayCode(), "600519");
}

TEST(StockCodeTest, ParseFromFullCode) {
    StockCode sc("SH600519");
    EXPECT_TRUE(sc.isValid());
    EXPECT_EQ(sc.market(), Market::SH);
    EXPECT_EQ(sc.code(), "600519");
}

TEST(StockCodeTest, ParseFromNakedSHCode) {
    StockCode sc("600519");
    EXPECT_TRUE(sc.isValid());
    EXPECT_EQ(sc.market(), Market::SH);
    EXPECT_EQ(sc.code(), "600519");
}

TEST(StockCodeTest, ParseFromNakedSZCode) {
    StockCode sc("000001");
    EXPECT_TRUE(sc.isValid());
    EXPECT_EQ(sc.market(), Market::SZ);
    EXPECT_EQ(sc.code(), "000001");
}

TEST(StockCodeTest, EqualityComparison) {
    StockCode a(Market::SH, "600519");
    StockCode b(Market::SH, "600519");
    StockCode c(Market::SZ, "600519");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(StockCodeTest, Sorting) {
    StockCode a(Market::SH, "000001");
    StockCode b(Market::SH, "600000");
    EXPECT_LT(a, b);
}
