#include <gtest/gtest.h>
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"

using namespace st;

TEST(DataCacheTest, CacheAndGet) {
    DataCache cache;
    StockCode code(Market::SH, "600519");
    auto start = utils::parseDate("2024-01-01");

    std::vector<Bar> bars;
    for (int i = 0; i < 3; ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(start, i);
        bar.close = 10.0 + i;
        bars.push_back(bar);
    }

    cache.cacheBars(code, BarPeriod::Daily, bars);

    EXPECT_TRUE(cache.has(code, BarPeriod::Daily));
    EXPECT_EQ(cache.size(), 1u);

    auto* series = cache.get(code, BarPeriod::Daily);
    ASSERT_NE(series, nullptr);
    EXPECT_EQ(series->size(), 3u);

    auto loaded = cache.getBars(code, BarPeriod::Daily);
    EXPECT_EQ(loaded.size(), 3u);
}

TEST(DataCacheTest, MissReturnsNull) {
    DataCache cache;
    StockCode code(Market::SH, "600519");
    EXPECT_FALSE(cache.has(code, BarPeriod::Daily));
    EXPECT_EQ(cache.get(code, BarPeriod::Daily), nullptr);
}

TEST(DataCacheTest, DifferentPeriodsAreSeparate) {
    DataCache cache;
    StockCode code(Market::SH, "600519");

    std::vector<Bar> daily;
    daily.push_back({});
    daily[0].period = BarPeriod::Daily;

    cache.cacheBars(code, BarPeriod::Daily, daily);

    EXPECT_TRUE(cache.has(code, BarPeriod::Daily));
    EXPECT_FALSE(cache.has(code, BarPeriod::Weekly));
    EXPECT_EQ(cache.size(), 1u);
}

TEST(DataCacheTest, ClearEmpties) {
    DataCache cache;
    StockCode code(Market::SH, "600519");
    cache.cacheBars(code, BarPeriod::Daily, {});
    EXPECT_EQ(cache.size(), 1u);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
}
