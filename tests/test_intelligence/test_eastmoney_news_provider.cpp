#include <gtest/gtest.h>
#include "intelligence/sentiment/eastmoney_news_provider.h"

#include <string>
#include <vector>

using namespace st;
using namespace st::sentiment;

namespace {

/// 组装一条 JSONP 响应（与东财返回格式一致：result.cmsArticleWebOld[]）
std::string jsonpWith(const std::string& inner) {
    return "jQuery112308461504899713896_1722835800000({" + inner + "})";
}

} // namespace

TEST(EastMoneyNewsProviderTest, ParsesJsonpWrapper) {
    const std::string body = jsonpWith(
        R"("result":{"cmsArticleWebOld":[
            {"title":"茅台业绩超预期","content":"公司发布三季度财报，净利润创新高","mediaName":"证券时报","date":"2026-08-03 18:43:00"},
            {"title":"白酒板块大涨","content":"板块整体走高","mediaName":"东财快讯","date":"2026-08-02 09:00:00"}
        ]})");
    const auto items = EastMoneyNewsProvider::parseNews(body, 10);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].title, "茅台业绩超预期");
    EXPECT_EQ(items[0].content, "公司发布三季度财报，净利润创新高");
    EXPECT_EQ(items[0].source, "证券时报");
    EXPECT_EQ(items[0].date, "2026-08-03");  // 只取前 10 位日期
    EXPECT_EQ(items[1].title, "白酒板块大涨");
    EXPECT_EQ(items[1].date, "2026-08-02");
}

TEST(EastMoneyNewsProviderTest, StripsAnyCallbackName) {
    // 回调名可变（cb=jQuery 后面带时间戳），解析只依赖首 '(' 与末 ')' 剥离。
    // 注意结尾的 JSONP ')' 必须保留（普通字符串字面量，避免 raw literal 吃掉它）
    const std::string body =
        "jQuery321.unique_callback({\"result\":{\"cmsArticleWebOld\":["
        "{\"title\":\"A\",\"content\":\"b\",\"mediaName\":\"src\","
        "\"date\":\"2026-08-01 10:00:00\"}]}})";
    const auto items = EastMoneyNewsProvider::parseNews(body, 10);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].title, "A");
    EXPECT_EQ(items[0].source, "src");
}

TEST(EastMoneyNewsProviderTest, TruncatesContentTo200) {
    std::string longContent(500, 'x');
    const std::string body = jsonpWith(
        R"("result":{"cmsArticleWebOld":[{"title":"T","content":")" + longContent +
        R"(","mediaName":"s","date":"2026-08-01 10:00:00"}]})");
    const auto items = EastMoneyNewsProvider::parseNews(body, 10);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].content.size(), 200u);
}

TEST(EastMoneyNewsProviderTest, RespectsLimit) {
    const std::string body = jsonpWith(
        R"("result":{"cmsArticleWebOld":[
            {"title":"一","content":"a","mediaName":"s","date":"2026-08-01 10:00:00"},
            {"title":"二","content":"b","mediaName":"s","date":"2026-08-01 10:00:00"},
            {"title":"三","content":"c","mediaName":"s","date":"2026-08-01 10:00:00"}
        ]})");
    const auto items = EastMoneyNewsProvider::parseNews(body, 2);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].title, "一");
    EXPECT_EQ(items[1].title, "二");
}

TEST(EastMoneyNewsProviderTest, EmptyResultReturnsEmpty) {
    const std::string body = jsonpWith(R"("result":{"cmsArticleWebOld":[]})");
    EXPECT_TRUE(EastMoneyNewsProvider::parseNews(body, 10).empty());
}

TEST(EastMoneyNewsProviderTest, MalformedReturnsEmpty) {
    EXPECT_TRUE(EastMoneyNewsProvider::parseNews("not json at all", 10).empty());
    EXPECT_TRUE(EastMoneyNewsProvider::parseNews("", 10).empty());
    EXPECT_TRUE(EastMoneyNewsProvider::parseNews("jQuery({})", 10).empty());  // 无 result.cmsArticleWebOld
    EXPECT_TRUE(EastMoneyNewsProvider::parseNews("jQuery({broken", 10).empty());  // 截断
}
