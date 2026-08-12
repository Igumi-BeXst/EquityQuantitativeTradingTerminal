#include "foundation/utils/watchlist_store.h"
#include "foundation/stock_code.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>

namespace st {
namespace {

TEST(WatchlistStoreTest, SaveLoadRoundTrip) {
    const std::string path = "watchlist_test_roundtrip.json";
    std::remove(path.c_str());
    std::vector<WatchlistStore::Entry> entries;
    entries.push_back({StockCode(std::string_view("SH600000")), "浦发银行"});
    entries.push_back({StockCode(std::string_view("SZ000001")), "平安银行"});
    WatchlistStore::save(path, entries);
    auto loaded = WatchlistStore::load(path);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].code.fullCode(), "SH600000");
    EXPECT_EQ(loaded[0].name, "浦发银行");
    EXPECT_EQ(loaded[1].code.fullCode(), "SZ000001");
    EXPECT_EQ(loaded[1].name, "平安银行");
    std::remove(path.c_str());
}

TEST(WatchlistStoreTest, LoadLegacyCodesFormat) {
    // 旧格式 {"codes":[...]}（无名称）→ 兼容解析，name 为空
    const std::string path = "watchlist_test_legacy.json";
    std::remove(path.c_str());
    { std::ofstream f(path); f << "{\"codes\":[\"SH600000\",\"SZ000001\"]}" << std::endl; }
    auto loaded = WatchlistStore::load(path);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].code.fullCode(), "SH600000");
    EXPECT_TRUE(loaded[0].name.empty());
    std::remove(path.c_str());
}

TEST(WatchlistStoreTest, LoadEmptyWhenMissingOrBad) {
    EXPECT_TRUE(WatchlistStore::load("watchlist_does_not_exist.json").empty());
    const std::string path = "watchlist_test_bad.json";
    { std::ofstream f(path); f << "not json{" << std::endl; }
    EXPECT_TRUE(WatchlistStore::load(path).empty());
    std::remove(path.c_str());
}

TEST(WatchlistStoreTest, SaveEmptyProducesValidFile) {
    const std::string path = "watchlist_test_empty.json";
    std::remove(path.c_str());
    WatchlistStore::save(path, {});
    EXPECT_TRUE(WatchlistStore::load(path).empty());
    std::remove(path.c_str());
}

}  // namespace
}  // namespace st
