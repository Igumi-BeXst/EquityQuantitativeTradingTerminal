#include <gtest/gtest.h>
#include "data/stock_search_index.h"

using namespace st;

namespace {
std::vector<StockInfo> makeInfos() {
    std::vector<StockInfo> infos;
    auto mk = [](const std::string& code, const std::string& name, const std::string& py) {
        StockInfo info;
        info.code = StockCode(code);
        info.name = name;
        info.pinyinInitials = py;
        info.valid = true;
        return info;
    };
    infos.push_back(mk("600519", "贵州茅台", "gzmt"));
    infos.push_back(mk("000858", "五粮液", "wly"));
    infos.push_back(mk("601318", "中国平安", "zgpa"));
    infos.push_back(mk("000001", "平安银行", "payh"));
    return infos;
}
}

TEST(StockSearchIndexTest, SearchByCode) {
    StockSearchIndex idx;
    idx.build(makeInfos());

    auto result = idx.search("600519");
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result[0].name, "贵州茅台");
}

TEST(StockSearchIndexTest, SearchByName) {
    StockSearchIndex idx;
    idx.build(makeInfos());

    auto result = idx.search("茅台");
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result[0].code.code(), "600519");
}

TEST(StockSearchIndexTest, SearchByPinyinInitials) {
    StockSearchIndex idx;
    idx.build(makeInfos());

    auto result = idx.search("gzmt");
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result[0].name, "贵州茅台");
}

TEST(StockSearchIndexTest, SearchNoResult) {
    StockSearchIndex idx;
    idx.build(makeInfos());

    auto result = idx.search("zzzz");
    EXPECT_TRUE(result.empty());
}

TEST(StockSearchIndexTest, SearchLimit) {
    StockSearchIndex idx;
    idx.build(makeInfos());

    auto result = idx.search("", 10);
    EXPECT_TRUE(result.empty());
}

TEST(StockSearchIndexTest, CaseInsensitive) {
    StockSearchIndex idx;
    idx.build(makeInfos());

    auto upper = idx.search("GZMT");
    auto lower = idx.search("gzmt");
    ASSERT_EQ(upper.size(), lower.size());
    EXPECT_FALSE(upper.empty());
}
