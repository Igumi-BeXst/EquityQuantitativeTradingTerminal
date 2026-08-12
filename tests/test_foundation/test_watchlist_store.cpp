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
    std::vector<StockCode> codes;
    codes.emplace_back(std::string_view("SH600000"));
    codes.emplace_back(std::string_view("SZ000001"));
    WatchlistStore::save(path, codes);
    auto loaded = WatchlistStore::load(path);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].fullCode(), "SH600000");
    EXPECT_EQ(loaded[1].fullCode(), "SZ000001");
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
