#include <gtest/gtest.h>
#include "engine/analyzer/custom_index_store.h"
#include "foundation/utils/datetime.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace st;

namespace {
std::string tempPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}
}  // namespace

TEST(CustomIndexStoreTest, SaveAndLoadRoundTrip) {
    const std::string path = tempPath("ci_roundtrip.json");
    std::filesystem::remove(path);

    CustomIndex idx;
    idx.id = "ci_1";
    idx.name = "白酒组合";
    idx.baseValue = 1000.0;
    idx.baseDate = utils::parseDate("2024-01-02");
    idx.constituents = {{StockCode(Market::SH, "600519"), "贵州茅台", 0.6},
                        {StockCode(Market::SZ, "000858"), "五粮液", 0.4}};

    CustomIndexStore store;
    ASSERT_TRUE(store.save(path, {idx}));

    auto loaded = store.load(path);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].id, "ci_1");
    EXPECT_EQ(loaded[0].name, "白酒组合");
    EXPECT_NEAR(loaded[0].baseValue, 1000.0, 1e-9);
    ASSERT_TRUE(loaded[0].baseDate.has_value());
    EXPECT_EQ(utils::toDateString(*loaded[0].baseDate), "2024-01-02");
    ASSERT_EQ(loaded[0].constituents.size(), 2u);
    EXPECT_EQ(loaded[0].constituents[0].code.fullCode(), "SH600519");
    EXPECT_EQ(loaded[0].constituents[0].name, "贵州茅台");
    EXPECT_NEAR(loaded[0].constituents[0].weight, 0.6, 1e-9);
    EXPECT_EQ(loaded[0].constituents[1].code.fullCode(), "SZ000858");

    std::filesystem::remove(path);
}

TEST(CustomIndexStoreTest, MissingFileReturnsEmpty) {
    CustomIndexStore store;
    EXPECT_TRUE(store.load(tempPath("ci_not_exist.json")).empty());
}

TEST(CustomIndexStoreTest, CorruptJsonReturnsEmpty) {
    const std::string path = tempPath("ci_corrupt.json");
    {
        std::ofstream ofs(path);
        ofs << "not json {[";
    }
    CustomIndexStore store;
    EXPECT_TRUE(store.load(path).empty());
    std::filesystem::remove(path);
}

TEST(CustomIndexStoreTest, EmptyBaseDateOmittedAndDefault) {
    const std::string path = tempPath("ci_nodefault.json");
    std::filesystem::remove(path);

    CustomIndex idx;
    idx.id = "ci_2";
    idx.name = "等权组合";
    idx.baseValue = 100.0;  // 非默认基点
    idx.constituents = {{StockCode(Market::SH, "600519"), "A", 0.0}};

    CustomIndexStore store;
    ASSERT_TRUE(store.save(path, {idx}));
    auto loaded = store.load(path);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_FALSE(loaded[0].baseDate.has_value());
    EXPECT_NEAR(loaded[0].baseValue, 100.0, 1e-9);

    std::filesystem::remove(path);
}

TEST(CustomIndexStoreTest, MultipleIndexesPreserved) {
    const std::string path = tempPath("ci_multi.json");
    std::filesystem::remove(path);

    CustomIndex a;
    a.id = "ci_a"; a.name = "A"; a.constituents = {{StockCode(Market::SH, "600519"), "A", 0.0}};
    CustomIndex b;
    b.id = "ci_b"; b.name = "B"; b.constituents = {{StockCode(Market::SZ, "000001"), "B", 0.0}};

    CustomIndexStore store;
    ASSERT_TRUE(store.save(path, {a, b}));
    auto loaded = store.load(path);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].id, "ci_a");
    EXPECT_EQ(loaded[1].id, "ci_b");
    std::filesystem::remove(path);
}
