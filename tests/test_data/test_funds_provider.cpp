#include <gtest/gtest.h>
#include "data/eastmoney_funds_provider.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {
// 用真实接口返回的结构做夹具（字段名/格式与线上一致）
const char* kLhbBody = R"(
{"result":{"count":1,"data":[
 {"SECURITY_CODE":"000779","SECURITY_NAME_ABBR":"甘肃能投","TRADE_DATE":"2026-08-07 00:00:00",
  "CLOSE_PRICE":14.28,"CHANGE_RATE":10.04,"BILLBOARD_NET_AMT":-11975946.7,
  "BILLBOARD_BUY_AMT":52310000.0,"BILLBOARD_SELL_AMT":64285946.7,
  "TURNOVER_RATE":12.5,"EXPLANATION":"日涨幅偏离值达到7%的前5只证券"}
]}}
)";

const char* kRzrqBody = R"(
{"result":{"count":2,"data":[
 {"DATE":"2026-08-07 00:00:00","MARKET":"融资融券_沪证","SCODE":"600519","SECNAME":"贵州茅台",
  "RZYE":17544302364.0,"RQYE":130500431.16,"RZRQYE":17674802795.16,"RZMRE":1123456.0},
 {"DATE":"2026-08-06 00:00:00","MARKET":"融资融券_沪证","SCODE":"600519","SECNAME":"贵州茅台",
  "RZYE":17526638429.0,"RQYE":134490151.9,"RZRQYE":17661128580.9,"RZMRE":987654.0}
]}}
)";

const char* kLsBody = R"(
{"result":{"count":1,"data":[
 {"DIM_DATE":"2026-08-07 00:00:00","RZYE":17800000000.0,"RZRQYE":18100000000.0}
]}}
)";
}  // namespace

TEST(FundsProviderTest, DragonTigerParseFields) {
    auto rows = EastMoneyFundsProvider::parseDragonTiger(kLhbBody);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].code, "000779");
    EXPECT_EQ(rows[0].name, "甘肃能投");
    EXPECT_NEAR(rows[0].netAmt, -11975946.7, 1e-6);
    EXPECT_NEAR(rows[0].buyAmt, 52310000.0, 1e-6);
    EXPECT_NEAR(rows[0].sellAmt, 64285946.7, 1e-6);
    EXPECT_NEAR(rows[0].changeRate, 10.04, 1e-9);
    EXPECT_NEAR(rows[0].turnoverRate, 12.5, 1e-9);
    EXPECT_NEAR(rows[0].closePrice, 14.28, 1e-9);
    EXPECT_EQ(utils::toDateTimeString(rows[0].date), "2026-08-07 00:00:00");
    EXPECT_EQ(rows[0].reason, "日涨幅偏离值达到7%的前5只证券");
}

TEST(FundsProviderTest, DragonTigerMalformedReturnsEmpty) {
    EXPECT_TRUE(EastMoneyFundsProvider::parseDragonTiger("not json{").empty());
    EXPECT_TRUE(EastMoneyFundsProvider::parseDragonTiger("").empty());
}

TEST(FundsProviderTest, MarginParseFields) {
    auto rows = EastMoneyFundsProvider::parseMargin(kRzrqBody);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].code, "600519");
    EXPECT_EQ(rows[0].name, "贵州茅台");
    EXPECT_EQ(rows[0].market, "融资融券_沪证");
    EXPECT_NEAR(rows[0].financeBalance, 17544302364.0, 1e-3);
    EXPECT_NEAR(rows[0].shortBalance, 130500431.16, 1e-3);
    EXPECT_NEAR(rows[0].marginBalance, 17674802795.16, 1e-3);
    EXPECT_NEAR(rows[0].financeBuy, 1123456.0, 1e-3);
    EXPECT_EQ(utils::toDateTimeString(rows[0].date), "2026-08-07 00:00:00");
}

TEST(FundsProviderTest, MarginMarketParse) {
    auto rows = EastMoneyFundsProvider::parseMarginMarket(kLsBody);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NEAR(rows[0].financeBalance, 17800000000.0, 1e-3);
    EXPECT_NEAR(rows[0].marginBalance, 18100000000.0, 1e-3);
}

TEST(FundsProviderTest, UrlsContainKeyParts) {
    EXPECT_NE(EastMoneyFundsProvider::dragonTigerUrl("2026-08-07").find(
                  "RPT_DAILYBILLBOARD_DETAILSNEW"), std::string::npos);
    EXPECT_NE(EastMoneyFundsProvider::dragonTigerUrl("2026-08-07").find(
                  "%272026-08-07%27"), std::string::npos);
    EXPECT_NE(EastMoneyFundsProvider::marginUrl("600519").find(
                  "RPTA_WEB_RZRQ_GGMX"), std::string::npos);
    EXPECT_NE(EastMoneyFundsProvider::marginUrl("600519").find(
                  "%22600519%22"), std::string::npos);
    EXPECT_NE(EastMoneyFundsProvider::marginMarketUrl().find(
                  "RPTA_RZRQ_LSHJ"), std::string::npos);
}
