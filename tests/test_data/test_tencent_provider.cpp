#include <gtest/gtest.h>
#include "data/tencent_provider.h"
#include "data/quote_poller.h"
#include "data/curated_stocks.h"
#include "core/event_bus.h"
#include "foundation/stock_code.h"

#include <set>

using namespace st;

namespace {

// GBK 编码的 "贵州茅台"（与腾讯 qt.gtimg.cn 实测返回字节一致）
const char* kGBKMaotai = "\xB9\xF3\xD6\xDD\xC3\xA9\xCC\xA8";

// 基于实测响应构造的单条行情记录（字段索引已验证）
std::string sampleRecord() {
    return std::string("1~") + kGBKMaotai +
        "~600519~1350.60~1361.76~1330.03~55128~27418~27709~"
        "1350.60~1~1350.55~1~1350.50~5~1350.40~1~1350.35~1~1350.67~31~"
        "1350.70~1~1350.88~11~1350.98~3~1351.00~6~~20260731161450~"
        "-11.16~-0.82~1355.72~1325.77~1350.60/55128/7373462605~55128~737346~0.44~20.41";
}

}  // namespace

// ============================================================
// parseQuoteRecord — 完整字段解析
// ============================================================
TEST(TencentQuoteTest, ParsesAllFields) {
    StockCode code(Market::SH, "600519");
    auto q = TencentProvider::parseQuoteRecord(sampleRecord(), code);

    EXPECT_EQ(q.code, code);
    EXPECT_NEAR(q.lastPrice, 1350.60, 1e-6);
    EXPECT_NEAR(q.preClose, 1361.76, 1e-6);
    EXPECT_NEAR(q.open, 1330.03, 1e-6);
    // 成交量: 手→股
    EXPECT_EQ(q.volume, 55128LL * 100);
    // 盘口
    EXPECT_NEAR(q.bidPrice1, 1350.60, 1e-6);
    EXPECT_EQ(q.bidVol1, 100);     // 1 手
    EXPECT_NEAR(q.askPrice1, 1350.67, 1e-6);
    EXPECT_EQ(q.askVol1, 3100);    // 31 手
    // 涨跌幅/高低
    EXPECT_NEAR(q.change, -0.82, 1e-6);   // [32] 涨跌幅%
    EXPECT_NEAR(q.high, 1355.72, 1e-6);
    EXPECT_NEAR(q.low, 1325.77, 1e-6);
    // 成交额: 万→元
    EXPECT_NEAR(q.amount, 737346.0 * 10000, 1e-3);
}

TEST(TencentQuoteTest, ParsesNameGbkToUtf8) {
    auto name = TencentProvider::parseQuoteName(sampleRecord());
    EXPECT_EQ(name, "贵州茅台");
}

TEST(TencentQuoteTest, NormalizesFullWidthName) {
    // "万 科Ａ" GBK 字节（含半角空格 + 全角Ａ），应规范化为 "万科A"
    const char* gbkWanka = "\xCD\xF2\x20\xBF\xC6\xA3\xC1";
    std::string record = std::string("1~") + gbkWanka + "~000002~10.0~10.0~10.0~1000~500~500~";
    auto name = TencentProvider::parseQuoteName(record);
    EXPECT_EQ(name, "万科A");
}

TEST(TencentQuoteTest, MalformedRecordReturnsEmpty) {
    StockCode code(Market::SH, "600519");
    // 字段不足
    auto q = TencentProvider::parseQuoteRecord("1~abc~600519", code);
    EXPECT_DOUBLE_EQ(q.lastPrice, 0.0);
    // 空记录
    auto q2 = TencentProvider::parseQuoteRecord("", code);
    EXPECT_DOUBLE_EQ(q2.lastPrice, 0.0);
}

// ============================================================
// parseQuotes — 批量响应解析
// ============================================================
TEST(TencentQuoteTest, ParsesBatchResponse) {
    std::string body = "v_sh600519=\"" + sampleRecord() + "\";";
    auto quotes = TencentProvider::parseQuotes(body);
    ASSERT_EQ(quotes.size(), 1u);
    EXPECT_NEAR(quotes[0].lastPrice, 1350.60, 1e-6);
    EXPECT_EQ(quotes[0].code.displayCode(), "600519");
}

TEST(TencentQuoteTest, PreservesIndexMarketFromPrefix) {
    // sh000001 是上证指数（SH），不能因代码 000001 自动识别为 SZ
    std::string body = "v_sh000001=\"1~上证指数~000001~3832.26~3804.00~3810.00~100000~0~0~"
                       "0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~"
                       "~~20260731161450~28.26~0.74~3835.00~3800.00~3832.26/100000/3832260000~"
                       "100000~383226~0.5~15.0\";";
    auto quotes = TencentProvider::parseQuotes(body);
    ASSERT_EQ(quotes.size(), 1u);
    EXPECT_EQ(quotes[0].code.market(), Market::SH);
    EXPECT_EQ(quotes[0].code.displayCode(), "000001");
    EXPECT_NEAR(quotes[0].lastPrice, 3832.26, 1e-6);
    EXPECT_NEAR(quotes[0].change, 0.74, 1e-6);
}

TEST(TencentQuoteTest, ParsesMultipleRecords) {
    std::string body = "v_sh600519=\"" + sampleRecord() + "\";";
    body += "v_sz000001=\"1~平安银行~000001~12.34~12.20~12.25~1000~"
            "500~500~12.34~100~12.33~200~12.32~300~12.31~400~12.30~500~"
            "12.35~100~12.36~200~12.37~300~12.38~400~12.39~500~~20260731161450~"
            "0.14~1.15~12.40~12.20~12.34/1000/12340000~1000~1234~0.5~5.0\";";
    auto quotes = TencentProvider::parseQuotes(body);
    ASSERT_EQ(quotes.size(), 2u);
    // 平安银行
    EXPECT_NEAR(quotes[1].lastPrice, 12.34, 1e-6);
    EXPECT_NEAR(quotes[1].change, 1.15, 1e-6);
}

// ============================================================
// toTencentCode
// ============================================================
TEST(TencentQuoteTest, ToTencentCode) {
    EXPECT_EQ(TencentProvider::toTencentCode(StockCode(Market::SH, "600519")), "sh600519");
    EXPECT_EQ(TencentProvider::toTencentCode(StockCode(Market::SZ, "000001")), "sz000001");
}

// ============================================================
// curated 精选股票池
// ============================================================
TEST(CuratedStocksTest, TableSizes) {
    EXPECT_GE(kCuratedSH.size(), 60u);
    EXPECT_GE(kCuratedSZ.size(), 60u);
    EXPECT_GE(kCuratedSH.size() + kCuratedSZ.size(), 120u);
}

TEST(CuratedStocksTest, CodesUniqueAndValid) {
    for (Market m : {Market::SH, Market::SZ}) {
        const auto& table = (m == Market::SH) ? kCuratedSH : kCuratedSZ;
        std::set<std::string> seen;
        for (const auto& c : table) {
            EXPECT_TRUE(StockCode(m, c.code).isValid())
                << "invalid code: " << c.code;
            EXPECT_FALSE(std::string(c.initials).empty())
                << "empty initials: " << c.code;
            auto [it, inserted] = seen.insert(c.code);
            EXPECT_TRUE(inserted) << "duplicate code: " << c.code;
        }
    }
}

// ============================================================
// QuotePoller — 注入 fetcher 的轮询逻辑
// ============================================================
TEST(QuotePollerTest, PublishesOnRefresh) {
    EventBus* bus = EventBus::instance();
    int count = 0;
    double lastPrice = 0.0;
    int subId = bus->subscribeCallback(events::QuoteReceived,
        [&](const QVariantMap& data) {
            ++count;
            lastPrice = data[QStringLiteral("lastPrice")].toDouble();
        });

    std::string body = "v_sh600519=\"" + sampleRecord() + "\";";
    QuotePoller poller;
    poller.setFetcher([body](const QString&) { return QByteArray(body.c_str()); });
    poller.addCode(StockCode(Market::SH, "600519"));
    poller.refreshNow();  // 注入模式同步发布

    EXPECT_EQ(count, 1);
    EXPECT_NEAR(lastPrice, 1350.60, 1e-6);

    bus->unsubscribeCallback(subId);
}

TEST(QuotePollerTest, NoPublishAfterRemoveCode) {
    EventBus* bus = EventBus::instance();
    int count = 0;
    int subId = bus->subscribeCallback(events::QuoteReceived,
        [&](const QVariantMap&) { ++count; });

    std::string body = "v_sh600519=\"" + sampleRecord() + "\";";
    QuotePoller poller;
    poller.setFetcher([body](const QString&) { return QByteArray(body.c_str()); });
    poller.addCode(StockCode(Market::SH, "600519"));
    poller.refreshNow();
    EXPECT_EQ(count, 1);

    poller.removeCode(StockCode(Market::SH, "600519"));
    poller.refreshNow();  // 已空，不再发布
    EXPECT_EQ(count, 1);

    bus->unsubscribeCallback(subId);
}

TEST(QuotePollerTest, DedupAddCode) {
    QuotePoller poller;
    poller.addCode(StockCode(Market::SH, "600519"));
    poller.addCode(StockCode(Market::SH, "600519"));
    EXPECT_EQ(poller.codeCount(), 1);
    poller.addCode(StockCode(Market::SZ, "000001"));
    EXPECT_EQ(poller.codeCount(), 2);
}
