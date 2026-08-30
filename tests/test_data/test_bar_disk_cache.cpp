#include "data/bar_disk_cache.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>

using namespace st;

namespace {

DateTime day(int y, int m, int d) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string tempDir() {
    return (std::filesystem::temp_directory_path() / "st_bar_disk_cache_test").string();
}

} // namespace

TEST(BarDiskCacheTest, SaveAndLoadRoundTrip) {
    const std::string dir = tempDir();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    BarDiskCache cache(dir);

    const StockCode code(Market::SH, "600000");
    std::vector<Bar> bars;
    for (int i = 1; i <= 3; ++i) {
        Bar b;
        b.code = code;
        b.period = BarPeriod::Daily;
        b.time = day(2024, 1, i);
        b.open = 10.0 + i;
        b.high = 11.0 + i;
        b.low = 9.0 + i;
        b.close = 10.5 + i;
        b.volume = 1000 * i;
        b.amount = 10000.0 * i;
        b.turnoverRate = 0.01 * i;
        bars.push_back(b);
    }
    cache.save(code, BarPeriod::Daily, bars);

    auto all = cache.loadAll(code, BarPeriod::Daily);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[1].close, 12.5);
    EXPECT_EQ(all[2].volume, 3000);

    auto filtered = cache.load(code, BarPeriod::Daily,
                               day(2024, 1, 2), day(2024, 1, 3));
    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered.front().time, day(2024, 1, 2));
    EXPECT_EQ(filtered.back().time, day(2024, 1, 3));

    std::filesystem::remove_all(dir, ec);
}
