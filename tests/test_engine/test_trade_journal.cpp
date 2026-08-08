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

// =============================================================================
// TradeJournalStatsTest — computeStats 统计核心
// =============================================================================

namespace {
/// 买入 100 股 10 元 → 卖出 100 股 12 元（卖出一笔，费用忽略）
std::vector<JournalEntry> roundTripDone() {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00"));
    return v;
}
}  // namespace

TEST(TradeJournalStatsTest, WinRateAndProfitFactor) {
    auto stats = computeStats(roundTripDone());
    EXPECT_EQ(stats.overall.count, 1);        // 1 个回合
    EXPECT_EQ(stats.overall.wins, 1);
    EXPECT_DOUBLE_EQ(stats.overall.winRate, 1.0);
    EXPECT_NEAR(stats.overall.totalPnl, 200.0, 1e-6);   // (12-10)*100
    EXPECT_DOUBLE_EQ(stats.overall.profitFactor, 0.0);   // 无亏损回合，factor=0
}

TEST(TradeJournalStatsTest, LossIsLoss) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 8.0, 100, "2026-08-02 09:30:00"));
    auto stats = computeStats(v);
    EXPECT_EQ(stats.overall.wins, 0);
    EXPECT_EQ(stats.overall.losses, 1);
    EXPECT_NEAR(stats.overall.totalPnl, -200.0, 1e-6);
}

TEST(TradeJournalStatsTest, FeeIncludedInCost) {
    std::vector<JournalEntry> v;
    auto b = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    b.fees = 7.5;   // 买入费计入成本
    v.push_back(b);
    auto s = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    s.fees = 7.5;   // 卖出费从收益扣
    v.push_back(s);
    auto stats = computeStats(v);
    EXPECT_NEAR(stats.overall.totalPnl, 200.0 - 15.0, 1e-6);  // 毛利200 − 费用15
}

TEST(TradeJournalStatsTest, SimVsManualGroups) {
    std::vector<JournalEntry> v;
    // 模拟组内完整回合（买+卖 AutoTrade）
    auto sb = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    sb.type = JournalType::AutoTrade;
    auto ss = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    ss.type = JournalType::AutoTrade;
    // 实盘组内完整回合（买+卖 ManualNote）
    auto mb = mkManual("SZ000001", Direction::Buy, 5.0, 200, "2026-08-01 09:31:00");
    auto ms = mkManual("SZ000001", Direction::Sell, 6.0, 200, "2026-08-03 09:31:00");
    v.push_back(sb); v.push_back(ss); v.push_back(mb); v.push_back(ms);
    auto stats = computeStats(v);
    EXPECT_EQ(stats.sim.count, 1);
    EXPECT_EQ(stats.manual.count, 1);
    EXPECT_EQ(stats.overall.count, 2);
    EXPECT_NEAR(stats.sim.totalPnl, 200.0, 1e-6);
    EXPECT_NEAR(stats.manual.totalPnl, 200.0, 1e-6);
}

TEST(TradeJournalStatsTest, CrossGroupBuySellNotPaired) {
    // 买入在实盘组、卖出在模拟组 → 两组都不构成回合
    std::vector<JournalEntry> v;
    auto b = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    auto s = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    s.type = JournalType::AutoTrade;
    v.push_back(b);
    v.push_back(s);
    auto stats = computeStats(v);
    EXPECT_EQ(stats.sim.count, 0);
    EXPECT_EQ(stats.manual.count, 0);
    EXPECT_EQ(stats.overall.count, 0);   // 跨组不配对
}

TEST(TradeJournalStatsTest, MonthlyGrouping) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-07-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-07-02 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 11.0, 100, "2026-08-02 09:30:00"));
    auto stats = computeStats(v);
    ASSERT_EQ(stats.monthly.size(), 2u);
    EXPECT_EQ(stats.monthly[0].ym, "2026-07");
    EXPECT_NEAR(stats.monthly[0].pnl, 200.0, 1e-6);
    EXPECT_NEAR(stats.monthly[1].pnl, 100.0, 1e-6);
}

TEST(TradeJournalStatsTest, CumPnlRises) {
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-07-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-07-02 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 11.0, 100, "2026-08-02 09:30:00"));
    auto stats = computeStats(v);
    ASSERT_EQ(stats.overall.cumPnl.size(), 2u);
    EXPECT_NEAR(stats.overall.cumPnl.back(), 300.0, 1e-6);
}

TEST(TradeJournalStatsTest, ProfitFactorMixedWinLoss) {
    // 两回合：+200（盈）和 -100（亏），profitFactor = 200/100 = 2.0
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-03 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 9.0, 100, "2026-08-04 09:30:00"));
    auto stats = computeStats(v);
    EXPECT_EQ(stats.overall.count, 2);
    EXPECT_EQ(stats.overall.wins, 1);
    EXPECT_EQ(stats.overall.losses, 1);
    EXPECT_DOUBLE_EQ(stats.overall.profitFactor, 2.0);
}

TEST(TradeJournalStatsTest, MaxDrawdownFromPeak) {
    // 先盈 +200，后亏 -300 → 峰值200，低点-100，回撤 = 200 - (-100) = 300
    std::vector<JournalEntry> v;
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-01 10:00:00"));
    v.push_back(mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-02 09:30:00"));
    v.push_back(mkManual("SH600519", Direction::Sell, 7.0, 100, "2026-08-02 10:00:00"));
    auto stats = computeStats(v);
    EXPECT_EQ(stats.overall.count, 2);
    EXPECT_NEAR(stats.overall.maxDrawdown, 300.0, 1e-6);
}

TEST(TradeJournalStatsTest, ByStrategyGroups) {
    // 带 strategy 的回合归入 byStrategy；无 strategy 的不计入
    std::vector<JournalEntry> v;
    auto b1 = mkManual("SH600519", Direction::Buy, 10.0, 100, "2026-08-01 09:30:00");
    b1.strategy = "MACross";
    v.push_back(b1);
    auto s1 = mkManual("SH600519", Direction::Sell, 12.0, 100, "2026-08-02 09:30:00");
    s1.strategy = "MACross";
    v.push_back(s1);
    // 无 strategy 的完整回合（不出现在 byStrategy 中）
    v.push_back(mkManual("SZ000001", Direction::Buy, 5.0, 200, "2026-08-03 09:30:00"));
    v.push_back(mkManual("SZ000001", Direction::Sell, 6.0, 200, "2026-08-04 09:30:00"));
    auto stats = computeStats(v);
    ASSERT_EQ(stats.byStrategy.size(), 1u);
    EXPECT_EQ(stats.byStrategy[0].first, "MACross");
    EXPECT_EQ(stats.byStrategy[0].second.count, 1);
    EXPECT_NEAR(stats.byStrategy[0].second.totalPnl, 200.0, 1e-6);
}
