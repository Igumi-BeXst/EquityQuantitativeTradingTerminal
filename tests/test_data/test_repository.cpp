#include <gtest/gtest.h>
#include "data/data_repository.h"
#include "foundation/utils/datetime.h"
#include <filesystem>

using namespace st;

namespace {
std::string makeTempDbPath() {
    static int count = 0;
    std::string path = "test_data_" + std::to_string(count++) + ".db";
    return path;
}
}

TEST(DataRepositoryTest, InitCreatesSchema) {
    auto path = makeTempDbPath();
    DataRepository repo;
    EXPECT_TRUE(repo.init(path));
    EXPECT_TRUE(repo.isOpen());

    // Verify tables exist by attempting operations
    std::vector<StockInfo> infos;
    repo.saveStockInfos(infos);
    EXPECT_TRUE(repo.loadStockInfos().empty());

    repo.close();
    std::filesystem::remove(path);
}

TEST(DataRepositoryTest, SaveAndLoadStockInfos) {
    auto path = makeTempDbPath();
    DataRepository repo;
    ASSERT_TRUE(repo.init(path));

    StockInfo info;
    info.code = StockCode(Market::SH, "600519");
    info.name = "贵州茅台";
    info.pinyinInitials = "gzmt";
    info.industry = "白酒";
    info.valid = true;

    repo.saveStockInfos({info});

    auto loaded = repo.loadStockInfos();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].code, info.code);
    EXPECT_EQ(loaded[0].name, info.name);
    EXPECT_EQ(loaded[0].pinyinInitials, "gzmt");

    auto single = repo.loadStockInfo(info.code);
    ASSERT_TRUE(single.has_value());
    EXPECT_EQ(single->name, "贵州茅台");

    repo.close();
    std::filesystem::remove(path);
}

TEST(DataRepositoryTest, SaveAndLoadDailyBars) {
    auto path = makeTempDbPath();
    DataRepository repo;
    ASSERT_TRUE(repo.init(path));

    std::vector<Bar> bars;
    StockCode code(Market::SH, "600519");
    auto start = utils::parseDate("2024-01-01");
    for (int i = 0; i < 5; ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(start, i);
        bar.open = 100.0 + i;
        bar.high = 105.0 + i;
        bar.low = 99.0 + i;
        bar.close = 102.0 + i;
        bar.volume = 1000 * (i + 1);
        bar.amount = 100000.0 * (i + 1);
        bars.push_back(bar);
    }

    repo.saveBars(bars);

    auto end = utils::parseDate("2024-12-31");
    auto loaded = repo.loadBars(code, BarPeriod::Daily, start, end);
    ASSERT_EQ(loaded.size(), 5u);
    EXPECT_DOUBLE_EQ(loaded[0].open, 100.0);
    EXPECT_EQ(loaded[0].volume, 1000);
    EXPECT_DOUBLE_EQ(loaded[4].close, 106.0);

    repo.close();
    std::filesystem::remove(path);
}

TEST(DataRepositoryTest, SyncLog) {
    auto path = makeTempDbPath();
    DataRepository repo;
    ASSERT_TRUE(repo.init(path));

    StockCode code(Market::SH, "600519");
    auto now = utils::now();
    repo.updateSyncLog(code, now, true);

    auto lastSync = repo.getLastSyncTime(code);
    ASSERT_TRUE(lastSync.has_value());

    // Check update works (overwrite)
    auto now2 = utils::addTradingDays(now, 1);
    repo.updateSyncLog(code, now2, true);
    auto lastSync2 = repo.getLastSyncTime(code);
    ASSERT_TRUE(lastSync2.has_value());
    EXPECT_NE(lastSync, lastSync2);

    repo.close();
    std::filesystem::remove(path);
}
