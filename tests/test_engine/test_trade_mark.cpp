#include "engine/journal/trade_journal.h"
#include "foundation/stock_code.h"
#include "gtest/gtest.h"
#include <chrono>
#include <ctime>
#include <vector>

using namespace st;

namespace {

JournalEntry mkBuy(const std::string& code, double price, int vol,
                   JournalType type, DateTime time, double fees = 0.0) {
    JournalEntry e;
    e.code = StockCode(code);
    e.name = "测试";
    e.type = type;
    e.direction = Direction::Buy;
    e.price = price;
    e.volume = vol;
    e.fees = fees;
    e.time = time;
    return e;
}

JournalEntry mkSell(const std::string& code, double price, int vol,
                    JournalType type, DateTime time, double fees = 0.0) {
    auto e = mkBuy(code, price, vol, type, time, fees);
    e.direction = Direction::Sell;
    return e;
}

DateTime t(int day, int hour = 10, int min = 0) {
    // 2026-08-day hour:min（测试用固定日期）
    std::tm tm{};
    tm.tm_year = 126;
    tm.tm_mon = 7;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace

TEST(TradeMarkTest, CollectFiltersByCodeAndSorts) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(5)),
        mkBuy("000858", 120, 200, JournalType::AutoTrade, t(3)),
        mkSell("600519", 1700, 100, JournalType::AutoTrade, t(8)),
    };
    auto marks = collectTradeMarks(entries, StockCode("600519"));
    ASSERT_EQ(marks.size(), 2u);
    EXPECT_EQ(marks[0].time, t(5));  // 时间升序
    EXPECT_EQ(marks[1].direction, Direction::Sell);
    EXPECT_TRUE(marks[1].time > marks[0].time);
}

TEST(TradeMarkTest, CollectKeepsBothTypes) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkBuy("600519", 1720, 100, JournalType::ManualNote, t(2)),
    };
    auto marks = collectTradeMarks(entries, StockCode("600519"));
    ASSERT_EQ(marks.size(), 2u);
    EXPECT_EQ(marks[0].type, JournalType::AutoTrade);
    EXPECT_EQ(marks[1].type, JournalType::ManualNote);
}

TEST(TradeMarkTest, CollectEmptyInput) {
    EXPECT_TRUE(collectTradeMarks({}, StockCode("600519")).empty());
    EXPECT_TRUE(collectTradeMarks({mkBuy("000858", 1, 1, JournalType::AutoTrade, t(1))},
                                  StockCode("600519")).empty());
}

TEST(TradeMarkTest, HoldingSimBuyWithFees) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1), /*fees=*/30.0),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].type, JournalType::AutoTrade);
    EXPECT_EQ(hs[0].quantity, 100);
    // (1680*100 + 30) / 100 = 1680.30
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.30);
}

TEST(TradeMarkTest, HoldingPartialSell) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 40, JournalType::AutoTrade, t(5)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].quantity, 60);
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.0);  // 成本不变，只减量
}

TEST(TradeMarkTest, HoldingAllSold) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 100, JournalType::AutoTrade, t(5)),
    };
    EXPECT_TRUE(deriveHoldings(entries, StockCode("600519")).empty());
}

TEST(TradeMarkTest, HoldingOverSellIgnored) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkSell("600519", 1700, 200, JournalType::AutoTrade, t(5)),  // 超出
    };
    // 实现语义：卖出超量/全卖 → queue 空 → 该类型不产出（与 AllSold 一致）
    EXPECT_TRUE(deriveHoldings(entries, StockCode("600519")).empty());
}

TEST(TradeMarkTest, HoldingBothTypesIndependent) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkBuy("600519", 1720, 100, JournalType::ManualNote, t(2)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 2u);
    // 模拟线
    EXPECT_EQ(hs[0].type, JournalType::AutoTrade);
    EXPECT_EQ(hs[0].quantity, 100);
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1680.0);
    // 实盘线
    EXPECT_EQ(hs[1].type, JournalType::ManualNote);
    EXPECT_EQ(hs[1].quantity, 100);
    EXPECT_DOUBLE_EQ(hs[1].avgCost, 1720.0);
}

TEST(TradeMarkTest, HoldingManualOnlyBuy) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1720, 100, JournalType::ManualNote, t(2)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].type, JournalType::ManualNote);  // 只出实盘线
}

TEST(TradeMarkTest, HoldingMultiBatchWeighted) {
    std::vector<JournalEntry> entries = {
        mkBuy("600519", 1680, 100, JournalType::AutoTrade, t(1)),
        mkBuy("600519", 1720, 300, JournalType::AutoTrade, t(3)),
    };
    auto hs = deriveHoldings(entries, StockCode("600519"));
    ASSERT_EQ(hs.size(), 1u);
    EXPECT_EQ(hs[0].quantity, 400);
    // (1680*100 + 1720*300) / 400 = 1710.0
    EXPECT_DOUBLE_EQ(hs[0].avgCost, 1710.0);
}

// —— onChange 回调 ——
TEST(TradeMarkTest, OnChangeFiresOnMutation) {
    TradeJournalEngine eng;
    int fired = 0;
    eng.setOnChange([&fired] { ++fired; });
    const auto id = eng.addEntry(mkBuy("600519", 1680, 100, JournalType::ManualNote, t(1)));
    EXPECT_EQ(fired, 1);
    eng.updateEntry(id, mkBuy("600519", 1700, 100, JournalType::ManualNote, t(2)));
    EXPECT_EQ(fired, 2);
    eng.removeEntry(id);
    EXPECT_EQ(fired, 3);
    eng.clear();
    EXPECT_EQ(fired, 4);
}
