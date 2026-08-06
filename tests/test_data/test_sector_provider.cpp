#include <gtest/gtest.h>
#include "data/eastmoney_sector_provider.h"

using namespace st;

namespace {

/// 行业板块两记录 fixture（含缺失领涨股的默认值场景）
const char* kIndustryBody = R"(
{"rc":0,"data":{"total":2,"diff":[
 {"f2":1200.5,"f3":1.79,"f6":45620000000.0,"f8":0.85,"f12":"BK0475","f14":"银行",
  "f104":41,"f105":1,"f106":0,"f128":"招商银行","f136":3.21},
 {"f2":800.0,"f3":-2.35,"f6":12000000000.0,"f8":1.2,"f12":"BK0737","f14":"半导体",
  "f104":5,"f105":85,"f106":2}
]}})";

/// 概念板块 diff 为对象形态 fixture
const char* kConceptBody = R"(
{"data":{"diff":{
 "0":{"f12":"BK1","f14":"A股","f3":1.0,"f104":10,"f128":"甲"},
 "1":{"f12":"BK2","f14":"B股","f3":-0.5}
}}})";

}  // namespace

TEST(SectorProviderTest, ParseIndustryBoards) {
    auto boards = EastMoneySectorProvider::parseBoards(kIndustryBody);
    ASSERT_EQ(boards.size(), 2u);

    const auto& b0 = boards[0];
    EXPECT_EQ(b0.code, "BK0475");
    EXPECT_EQ(b0.name, "银行");
    EXPECT_NEAR(b0.index, 1200.5, 1e-6);
    EXPECT_NEAR(b0.changePct, 1.79, 1e-6);
    EXPECT_NEAR(b0.amount, 45620000000.0, 1.0);
    EXPECT_NEAR(b0.turnover, 0.85, 1e-6);
    EXPECT_EQ(b0.upCount, 41);
    EXPECT_EQ(b0.downCount, 1);
    EXPECT_EQ(b0.flatCount, 0);
    EXPECT_EQ(b0.leadingStock, "招商银行");
    EXPECT_NEAR(b0.leadingChangePct, 3.21, 1e-6);

    const auto& b1 = boards[1];
    EXPECT_EQ(b1.code, "BK0737");
    EXPECT_EQ(b1.name, "半导体");
    EXPECT_NEAR(b1.changePct, -2.35, 1e-6);
    EXPECT_TRUE(b1.leadingStock.empty());   // 缺失领涨股 → 默认空
    EXPECT_NEAR(b1.leadingChangePct, 0.0, 1e-9);
}

TEST(SectorProviderTest, ParseConceptBoardsObjectDiff) {
    auto boards = EastMoneySectorProvider::parseBoards(kConceptBody);
    ASSERT_EQ(boards.size(), 2u);
    EXPECT_EQ(boards[0].code, "BK1");
    EXPECT_EQ(boards[0].name, "A股");
    EXPECT_NEAR(boards[0].changePct, 1.0, 1e-6);
    EXPECT_EQ(boards[0].upCount, 10);
    EXPECT_EQ(boards[0].leadingStock, "甲");
    EXPECT_EQ(boards[1].code, "BK2");
    EXPECT_NEAR(boards[1].changePct, -0.5, 1e-6);
}

TEST(SectorProviderTest, MissingCoreFieldsSkipped) {
    // 缺 f12/f14 核心字段 → 跳过；只缺数值字段 → 默认 0
    const char* body = R"({"data":{"diff":[
        {"f12":"BK1"},
        {"f12":"BK2","f14":"只有名字"},
        {"f3":2.0,"f14":"没代码"}
    ]}})";
    auto boards = EastMoneySectorProvider::parseBoards(body);
    ASSERT_EQ(boards.size(), 1u);
    EXPECT_EQ(boards[0].code, "BK2");
    EXPECT_EQ(boards[0].name, "只有名字");
    EXPECT_NEAR(boards[0].changePct, 0.0, 1e-9);
}

TEST(SectorProviderTest, MalformedOrNoDataReturnsEmpty) {
    EXPECT_TRUE(EastMoneySectorProvider::parseBoards("not json").empty());
    EXPECT_TRUE(EastMoneySectorProvider::parseBoards("").empty());
    EXPECT_TRUE(EastMoneySectorProvider::parseBoards(R"({"data":{}})").empty());
    EXPECT_TRUE(EastMoneySectorProvider::parseBoards(R"({"data":{"diff":"oops"}})").empty());
}

TEST(SectorProviderTest, FsForMapsType) {
    EXPECT_EQ(EastMoneySectorProvider::fsFor(SectorType::Industry), "m:90+t:2");
    EXPECT_EQ(EastMoneySectorProvider::fsFor(SectorType::Concept), "m:90+t:3");
}

TEST(SectorProviderTest, SinaUrlForMapsType) {
    EXPECT_EQ(EastMoneySectorProvider::sinaUrlFor(SectorType::Industry),
              "http://vip.stock.finance.sina.com.cn/q/view/newSinaHy.php");
    EXPECT_EQ(EastMoneySectorProvider::sinaUrlFor(SectorType::Concept),
              "http://money.finance.sina.com.cn/q/view/newFLJK.php?param=class");
}

TEST(SectorProviderTest, ParseSinaIndustryBoards) {
    // 新浪板块行情: var xxx = {"code":"code,名称,公司家数,平均价,涨跌额,涨跌幅,量,额,领涨代码,?,?,领涨涨跌幅,领涨名称",...}
    // 名称用 GBK 字节验证整体转码路径：玻璃行业 B2A3 C1A7 D0D0 D2B5；再升科技 D4D9 C9FD BFC6 BCBC
    const char* body =
        "var S_Finance_bankuai_sinaindustry = {"
        "\"new_blhy\":\"new_blhy,\xB2\xA3\xC1\xA7\xD0\xD0\xD2\xB5,19,16.35,"
        "0.23578947368421,1.4632393768168,943190154,21632449097,sh603601,5.895,9.700,"
        "0.540,\xD4\xD9\xC9\xFD\xBF\xC6\xBC\xBC\","
        "\"new_cbzz\":\"new_cbzz,ASCII-Sector,8,13.79,-0.13857142857143,"
        "-0.99487179487179,228667729,3609609554,sz300123,2.500,4.510,0.110,LeadStock\"};";
    auto page = EastMoneySectorProvider::parseSinaPage(body);
    ASSERT_EQ(page.boards.size(), 2u);

    const auto& b0 = page.boards[0];
    EXPECT_EQ(b0.code, "new_blhy");
    EXPECT_EQ(b0.name, "玻璃行业");            // GBK → UTF-8 转码
    EXPECT_NEAR(b0.index, 16.35, 1e-6);
    EXPECT_NEAR(b0.changePct, 1.4632393768168, 1e-9);
    EXPECT_NEAR(b0.amount, 21632449097.0, 1.0);
    EXPECT_EQ(b0.turnover, 0.0);               // 新浪无换手率
    EXPECT_EQ(b0.upCount, 0);                  // 新浪无涨跌平家数
    EXPECT_EQ(b0.downCount, 0);
    EXPECT_EQ(b0.flatCount, 0);
    EXPECT_EQ(b0.leadingStock, "再升科技");    // 领涨股名称（GBK 转码）
    EXPECT_NEAR(b0.leadingChangePct, 0.540, 1e-9);

    const auto& b1 = page.boards[1];
    EXPECT_EQ(b1.code, "new_cbzz");
    EXPECT_EQ(b1.name, "ASCII-Sector");
    EXPECT_NEAR(b1.changePct, -0.99487179487179, 1e-9);
    EXPECT_EQ(b1.leadingStock, "LeadStock");
}

TEST(SectorProviderTest, ParseSinaTooFewFieldsSkipped) {
    // 字段不足 8 个（拿不到涨跌幅/成交额）→ 跳过
    const char* body = "var x = {\"a\":\"a,只有名字\"};";
    EXPECT_TRUE(EastMoneySectorProvider::parseSinaPage(body).boards.empty());
}

TEST(SectorProviderTest, ParseSinaMalformedReturnsEmpty) {
    EXPECT_TRUE(EastMoneySectorProvider::parseSinaPage("").boards.empty());
    EXPECT_TRUE(EastMoneySectorProvider::parseSinaPage("not json").boards.empty());
    EXPECT_TRUE(EastMoneySectorProvider::parseSinaPage("var x = notjson;").boards.empty());
    EXPECT_TRUE(EastMoneySectorProvider::parseSinaPage("var x = {\"a\":123};").boards.empty());
}
