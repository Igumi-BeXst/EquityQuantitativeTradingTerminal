#include <gtest/gtest.h>
#include "engine/journal/trade_journal.h"
#include "foundation/utils/datetime.h"

using namespace st;

namespace {
JournalEntry mkManual(const std::string& code, Direction dir,
                      double price, long vol, const std::string& time) {
    JournalEntry e;
    e.code = StockCode(code);
    e.name = "测试";
    e.type = JournalType::ManualNote;
    e.direction = dir;
    e.price = price;
    e.volume = vol;
    e.time = utils::parseDateTime(time);
    return e;
}
}  // namespace

TEST(TradeJournalTest, CrudAddUpdateRemoveClear) {
    TradeJournalEngine j;
    const auto id = j.addEntry(mkManual("SH600519", Direction::Buy, 1500.0, 100,
                                        "2026-08-01 10:00:00"));
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(j.entries().size(), 1u);

    auto e = mkManual("SH600519", Direction::Sell, 1600.0, 100, "2026-08-02 10:00:00");
    EXPECT_TRUE(j.updateEntry(id, e));
    ASSERT_EQ(j.entries().size(), 1u);
    EXPECT_EQ(j.entries()[0].id, id);
    EXPECT_EQ(j.entries()[0].direction, Direction::Sell);

    EXPECT_TRUE(j.removeEntry(id));
    EXPECT_TRUE(j.entries().empty());

    j.addEntry(mkManual("SH000001", Direction::Buy, 3.0, 1000, "2026-08-03 09:30:00"));
    j.clear();
    EXPECT_TRUE(j.entries().empty());
}

TEST(TradeJournalTest, AppendAutoFields) {
    TradeJournalEngine j;
    Trade t;
    t.id = "T1";
    t.code = StockCode("SZ000001");
    t.direction = Direction::Buy;
    t.price = 10.0;
    t.volume = 500;
    t.amount = 5000.0;
    t.totalFee = 7.5;
    t.time = utils::parseDateTime("2026-08-03 09:30:00");

    const auto id = j.appendAuto(t, "MACross");
    ASSERT_FALSE(id.empty());
    ASSERT_EQ(j.entries().size(), 1u);
    EXPECT_EQ(j.entries()[0].type, JournalType::AutoTrade);
    EXPECT_EQ(j.entries()[0].strategy, "MACross");
    EXPECT_EQ(j.entries()[0].fees, 7.5);
    EXPECT_EQ(j.entries()[0].volume, 500);
}

TEST(TradeJournalTest, AppendAutoDedup) {
    TradeJournalEngine j;
    Trade t;
    t.code = StockCode("SZ000001");
    t.direction = Direction::Buy;
    t.price = 10.0;
    t.volume = 500;
    t.time = utils::parseDateTime("2026-08-03 09:30:00");

    EXPECT_FALSE(j.appendAuto(t, "MACross").empty());
    // 同代码/时间/方向/数量 → 指纹相同 → 跳过
    EXPECT_TRUE(j.appendAuto(t, "MACross").empty());
    EXPECT_EQ(j.entries().size(), 1u);

    // 价格不同但其他相同 → 指纹不含价格 → 仍重复
    t.price = 10.5;
    EXPECT_TRUE(j.appendAuto(t, "MACross").empty());

    // 数量不同 → 指纹不同 → 落库
    t.volume = 600;
    EXPECT_FALSE(j.appendAuto(t, "MACross").empty());
    EXPECT_EQ(j.entries().size(), 2u);
}

TEST(TradeJournalTest, FingerprintIgnoresPriceAndName) {
    TradeJournalEngine j;
    auto a = mkManual("SH600519", Direction::Buy, 1500.0, 100, "2026-08-01 10:00:00");
    auto b = a;
    b.price = 9999.0;
    b.name = "另一个名字";
    EXPECT_EQ(j.entryFingerprint(a), j.entryFingerprint(b));
}

TEST(TradeJournalTest, DefaultFeesStandardAShare) {
    TradeJournalEngine j;
    EXPECT_DOUBLE_EQ(j.fees().commissionRate, 0.00025);
    EXPECT_DOUBLE_EQ(j.fees().minCommission, 5.0);
    EXPECT_DOUBLE_EQ(j.fees().stampTaxRate, 0.0005);   // 2023 减半后标准
    EXPECT_DOUBLE_EQ(j.fees().transferFeeRate, 0.00002);
}
