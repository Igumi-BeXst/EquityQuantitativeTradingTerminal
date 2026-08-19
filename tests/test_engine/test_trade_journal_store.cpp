#include <gtest/gtest.h>
#include "engine/journal/trade_journal_store.h"
#include "foundation/utils/datetime.h"
#include <filesystem>
#include <fstream>

using namespace st;

namespace {

/// 在临时目录生成唯一测试文件路径（每次测试前 remove 避免残留）
std::string tmpPath(const char* suffix = ".json") {
    auto p = std::filesystem::temp_directory_path();
    p /= "trade_journal_test_" +
         std::to_string(reinterpret_cast<uintptr_t>(::operator new(0))) +
         suffix;
    return p.string();
}

/// 构造一条手工录入
JournalEntry mk(const std::string& code, Direction d, double price, long vol,
                const std::string& t, const std::string& note = "") {
    JournalEntry e;
    e.code      = StockCode(code);
    e.type      = JournalType::ManualNote;
    e.direction = d;
    e.price     = price;
    e.volume    = vol;
    e.fees      = 5.0;
    e.note      = note;
    e.time      = utils::parseDateTime(t);
    return e;
}

}  // namespace

/// 保存后加载 → 字段一致
TEST(TradeJournalStoreTest, RoundTrip) {
    const std::string path = tmpPath();
    std::remove(path.c_str());
    {
        TradeJournalEngine j;
        j.addEntry(mk("SH600519", Direction::Buy, 10.0, 100,
                      "2026-08-01 09:30:00", "第一笔"));
        TradeJournalStore store;
        EXPECT_TRUE(store.save(path, j));
    }
    {
        TradeJournalEngine j2;
        TradeJournalStore store;
        EXPECT_TRUE(store.load(path, j2));
        ASSERT_EQ(j2.entries().size(), 1u);
        EXPECT_EQ(j2.entries()[0].code.code(), "600519");
        EXPECT_EQ(j2.entries()[0].note, "第一笔");
        EXPECT_DOUBLE_EQ(j2.entries()[0].fees, 5.0);
    }
    std::remove(path.c_str());
}

/// 文件不存在 → 返回 false 且 engine 为空
TEST(TradeJournalStoreTest, MissingFileReturnsEmpty) {
    TradeJournalEngine j;
    TradeJournalStore store;
    EXPECT_FALSE(store.load("/nonexistent/trade_journal.json", j));
    EXPECT_TRUE(j.entries().empty());
}

/// 文件内容损坏 → 返回 false 且 engine 为空
TEST(TradeJournalStoreTest, CorruptFileFallsBackEmpty) {
    const std::string path = tmpPath();
    std::remove(path.c_str());
    { std::ofstream ofs(path); ofs << "not json{"; }
    TradeJournalEngine j;
    TradeJournalStore store;
    EXPECT_FALSE(store.load(path, j));
    EXPECT_TRUE(j.entries().empty());
    std::remove(path.c_str());
}

/// 费率配置保存后加载 → 字段一致
TEST(TradeJournalStoreTest, FeeConfigRoundTrip) {
    const std::string path = tmpPath(".cfg.json");
    std::remove(path.c_str());
    FeeConfig cfg;
    cfg.commissionRate = 0.0001;   // 万1
    cfg.minCommission  = 3.0;
    cfg.stampTaxRate   = 0.0005;
    cfg.transferFeeRate = 0.00002;
    EXPECT_TRUE(TradeJournalStore::saveFeeConfig(path, cfg));
    auto loaded = TradeJournalStore::loadFeeConfig(path);
    EXPECT_DOUBLE_EQ(loaded.commissionRate,  0.0001);
    EXPECT_DOUBLE_EQ(loaded.minCommission,   3.0);
    EXPECT_DOUBLE_EQ(loaded.stampTaxRate,    0.0005);
    EXPECT_DOUBLE_EQ(loaded.transferFeeRate, 0.00002);
    std::remove(path.c_str());
}

/// 超过 maxEntries 时，最旧条目自动归档到 .archive，且重复保存不产生重复归档
TEST(TradeJournalStoreTest, SaveArchivesOldestWhenOverLimit) {
    const std::string path = tmpPath();
    const std::string archive = path + ".archive";
    std::remove(path.c_str());
    std::remove(archive.c_str());

    {
        TradeJournalEngine j;
        for (int i = 0; i < 6; ++i) {
            j.addEntry(mk("SH600519", Direction::Buy, 10.0, 100,
                          "2026-08-01 09:30:00", ""));
        }
        TradeJournalStore store;
        EXPECT_TRUE(store.save(path, j, 5));
    }
    {
        TradeJournalEngine main;
        TradeJournalStore store;
        EXPECT_TRUE(store.load(path, main));
        EXPECT_EQ(main.entries().size(), 5u);
    }
    {
        TradeJournalEngine arch;
        TradeJournalStore store;
        EXPECT_TRUE(store.load(archive, arch));
        EXPECT_EQ(arch.entries().size(), 1u);
    }

    // 再次用相同 id 集合保存，归档文件不应重复累积
    {
        TradeJournalEngine j;
        for (int i = 0; i < 6; ++i) {
            j.addEntry(mk("SH600519", Direction::Buy, 10.0, 100,
                          "2026-08-01 09:30:00", ""));
        }
        TradeJournalStore store;
        EXPECT_TRUE(store.save(path, j, 5));
    }
    {
        TradeJournalEngine arch;
        TradeJournalStore store;
        EXPECT_TRUE(store.load(archive, arch));
        EXPECT_EQ(arch.entries().size(), 1u);
    }

    std::remove(path.c_str());
    std::remove(archive.c_str());
}

/// 费率文件缺失 → 使用标准 A 股默认费率
TEST(TradeJournalStoreTest, MissingFeeConfigUsesStandard) {
    auto cfg = TradeJournalStore::loadFeeConfig("/nonexistent/journal_config.json");
    EXPECT_DOUBLE_EQ(cfg.commissionRate,  0.00025);
    EXPECT_DOUBLE_EQ(cfg.stampTaxRate,    0.0005);
    EXPECT_DOUBLE_EQ(cfg.minCommission,   5.0);
    EXPECT_DOUBLE_EQ(cfg.transferFeeRate, 0.00002);
}
