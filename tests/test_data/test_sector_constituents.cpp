#include <gtest/gtest.h>
#include "data/eastmoney_sector_constituents.h"

using namespace st;

namespace {

// 真实接口返回结构（字段名/格式与线上一致）
const char* kBody = R"(
{"version":"x","result":{"pages":2,"data":[
 {"SECURITY_CODE":"000552","SECURITY_NAME_ABBR":"甘肃能化"},
 {"SECURITY_CODE":"000571","SECURITY_NAME_ABBR":"新大洲A"},
 {"SECURITY_CODE":"000723","SECURITY_NAME_ABBR":"美锦能源"}
],"count":34},"success":true}
)";

}  // namespace

TEST(SectorConstituentsTest, ParseFields) {
    auto codes = EastMoneySectorConstituents::parseConstituents(kBody);
    ASSERT_EQ(codes.size(), 3u);
    EXPECT_EQ(codes[0].code(), "000552");
    EXPECT_EQ(codes[1].code(), "000571");
    EXPECT_EQ(codes[2].code(), "000723");
}

TEST(SectorConstituentsTest, ParseEmptyReturnsEmpty) {
    EXPECT_TRUE(EastMoneySectorConstituents::parseConstituents("").empty());
    EXPECT_TRUE(EastMoneySectorConstituents::parseConstituents("not json{").empty());
    EXPECT_TRUE(EastMoneySectorConstituents::parseConstituents("{}").empty());
    // result 为 null
    EXPECT_TRUE(EastMoneySectorConstituents::parseConstituents(
        R"({"result":null})").empty());
}

TEST(SectorConstituentsTest, UrlContainsKeyParts) {
    const auto url = EastMoneySectorConstituents::constituentsUrl("煤炭", 1, 100);
    EXPECT_NE(url.find("RPT_F10_CORETHEME_BOARDTYPE"), std::string::npos);
    EXPECT_NE(url.find("%E7%85%A4%E7%82%AD"), std::string::npos);  // URL 编码"煤炭"
    EXPECT_NE(url.find("pageNumber=1"), std::string::npos);
    EXPECT_NE(url.find("pageSize=100"), std::string::npos);
}

TEST(SectorConstituentsTest, MalformedSkipsBadEntries) {
    // 缺 SECURITY_CODE 或非法代码的条目跳过
    const char* body = R"(
    {"result":{"data":[
      {"SECURITY_NAME_ABBR":"无名"},
      {"SECURITY_CODE":"000552","SECURITY_NAME_ABBR":"甘肃能化"},
      {"SECURITY_CODE":"short","SECURITY_NAME_ABBR":"坏代码"}
    ]}}
    )";
    auto codes = EastMoneySectorConstituents::parseConstituents(body);
    ASSERT_EQ(codes.size(), 1u);
    EXPECT_EQ(codes[0].code(), "000552");
}
