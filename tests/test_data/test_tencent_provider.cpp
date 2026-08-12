#include <gtest/gtest.h>
#include "data/tencent_provider.h"
#include "data/quote_poller.h"
#include "data/curated_stocks.h"
#include "core/event_bus.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"

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

TEST(TencentQuoteTest, ParsesFqKlineWeekly) {
    std::string json = R"({"data":{"sh600519":{"qfqweek":[
        ["2026-07-17","1197.120","1253.000","1269.330","1190.190","263482.000"],
        ["2026-07-24","1270.000","1297.410","1344.700","1266.000","318097.000"]
    ]}}})";
    StockCode code(Market::SH, "600519");
    auto bars = TencentProvider::parseFqKline(json, code, BarPeriod::Weekly);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].period, BarPeriod::Weekly);
    EXPECT_NEAR(bars[0].open, 1197.12, 1e-6);
    EXPECT_NEAR(bars[0].close, 1253.0, 1e-6);
    EXPECT_NEAR(bars[0].high, 1269.33, 1e-6);
    EXPECT_EQ(bars[1].volume, 318097);
    EXPECT_EQ(st::utils::toDateString(bars[0].time), "2026-07-17");
}

TEST(TencentQuoteTest, ParsesFqKlineMonthlyAndPeriod) {
    std::string json = R"({"data":{"sh600519":{"qfqmonth":[
        ["2026-07-01","1400.000","1350.600","1420.000","1280.000","1000000.000"]
    ]}}})";
    StockCode code(Market::SH, "600519");
    auto bars = TencentProvider::parseFqKline(json, code, BarPeriod::Monthly);
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(bars[0].period, BarPeriod::Monthly);
    EXPECT_NEAR(bars[0].close, 1350.6, 1e-6);
}

TEST(TencentQuoteTest, ParsesMinuteKline) {
    std::string json = R"({"data":{"sh600519":{"m5":[
        ["202607311440","1350.78","1353.60","1353.69","1350.00","1039.00",{},"0.83"],
        ["202607311445","1353.49","1351.95","1355.54","1351.00","1298.00",{},"1.04"]
    ]}}})";
    StockCode code(Market::SH, "600519");
    auto bars = TencentProvider::parseMinuteKline(json, code, BarPeriod::Minute5);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].period, BarPeriod::Minute5);
    // 列序: 开, 收, 高, 低
    EXPECT_NEAR(bars[0].open, 1350.78, 1e-6);
    EXPECT_NEAR(bars[0].close, 1353.60, 1e-6);
    EXPECT_NEAR(bars[0].high, 1353.69, 1e-6);
    EXPECT_NEAR(bars[0].low, 1350.00, 1e-6);
    // 量: 手→股
    EXPECT_EQ(bars[0].volume, 103900);
    EXPECT_EQ(st::utils::toDateTimeString(bars[0].time), "2026-07-31 14:40:00");
}

TEST(TencentQuoteTest, ParsesIntraday) {
    std::string json = R"({"data":{"sh600519":{"data":{
        "data":["0930 1330.03 1191 158406573.03","0931 1327.77 3547 471549408.00"],
        "date":"20240802"
    },"qt":{"sh600519":["1","贵州茅台","600519","1330.03","1361.76"]}}}})";
    StockCode code(Market::SH, "600519");
    auto data = TencentProvider::parseIntraday(json, code);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->points.size(), 2u);
    EXPECT_EQ(data->date, st::utils::parseDate("2024-08-02"));
    EXPECT_NEAR(data->preClose, 1361.76, 1e-6);
    // 第一点: 09:30 价格 1330.03, 量 119100 股, 额 158406573.03 元
    EXPECT_NEAR(data->points[0].price, 1330.03, 1e-6);
    EXPECT_EQ(data->points[0].volume, 119100);
    EXPECT_NEAR(data->points[0].amount, 158406573.03, 1e-3);
    EXPECT_EQ(st::utils::toDateTimeString(data->points[0].time), "2024-08-02 09:30:00");
}

TEST(TencentQuoteTest, ParsesTurnover) {
    // 茅台换手率在时间戳 +8 = [38]
    StockCode code(Market::SH, "600519");
    auto q = TencentProvider::parseQuoteRecord(sampleRecord(), code);
    EXPECT_NEAR(q.turnover, 0.44, 1e-6);
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

// ============================================================
// parseFundamentals — 基本面字段解析（东财 ulist 不可用时的腾讯备源）
// ============================================================
namespace {

// 构造 74 字段腾讯行情记录（时间戳@30）。
// shifted=true 模拟茅台式布局（t+10 空字段 → 市值 +1 偏移）：
//   换手率@t+8 市盈TTM@t+9 流通市值@t+13+s 总市值@t+14+s
//   流通股本@t+39（固定） 总股本@t+40-s
std::string fundamentalsRecord(bool shifted) {
    const int s = shifted ? 1 : 0;
    std::string rec;
    for (int i = 0; i < 74; ++i) {
        if (i == 30) rec += "20260812161432";
        else if (i == 38) rec += "0.28";
        else if (i == 39) rec += "20.30";
        else if (i == 40) rec += shifted ? "" : "7.59";          // t+10 条件字段
        else if (i == 43 + s) rec += "16788.60";                 // 流通市值(亿)
        else if (i == 44 + s) rec += "16788.60";                 // 总市值(亿)
        else if (i == 69) rec += "500000000";                    // 流通股本 5亿
        else if (i == 70 - s) rec += "1000000000";               // 总股本 10亿（s=1 时与流通同格→同值）
        else rec += "0";
        if (i != 73) rec += "~";
    }
    return rec;
}

}  // namespace

TEST(TencentFundamentalsTest, ParsesTurnoverPeCapShares) {
    // s=0（工行式，t+10 非空）：流通/总股本分列不同格
    auto f = TencentProvider::parseFundamentals(
        fundamentalsRecord(false), StockCode(Market::SH, "601398"));
    EXPECT_TRUE(f.valid);
    EXPECT_NEAR(f.turnoverRate, 0.28, 1e-6);
    EXPECT_NEAR(f.peTtm, 20.30, 1e-6);
    EXPECT_NEAR(f.floatCap, 16788.60 * 1e8, 1e-3);
    EXPECT_NEAR(f.marketCap, 16788.60 * 1e8, 1e-3);
    EXPECT_NEAR(f.floatShares, 500000000.0, 1e-3);
    EXPECT_NEAR(f.totalShares, 1000000000.0, 1e-3);
    // 换手率(实) = 换手率 × 流通/总 = 0.28 × 5亿/10亿 = 0.14
    EXPECT_NEAR(f.turnoverRateReal, 0.14, 1e-6);
    // 市盈(静) 未解析（偏移不可靠），保持 0
    EXPECT_EQ(f.peStatic, 0.0);
}

TEST(TencentFundamentalsTest, ParsesShiftedLayout) {
    // s=1（茅台式，t+10 空字段 → 市值 +1 偏移；流通=总股本同格同值）
    auto f = TencentProvider::parseFundamentals(
        fundamentalsRecord(true), StockCode(Market::SH, "600519"));
    EXPECT_TRUE(f.valid);
    EXPECT_NEAR(f.turnoverRate, 0.28, 1e-6);
    EXPECT_NEAR(f.floatCap, 16788.60 * 1e8, 1e-3);
    EXPECT_NEAR(f.marketCap, 16788.60 * 1e8, 1e-3);
    EXPECT_NEAR(f.floatShares, 500000000.0, 1e-3);
    EXPECT_NEAR(f.totalShares, 500000000.0, 1e-3);  // s=1 时总股本落在流通股本田
    EXPECT_NEAR(f.turnoverRateReal, 0.28, 1e-6);    // 流通=总 → 实=名义
}

TEST(TencentFundamentalsTest, InvalidWhenNoTimestampOrAllZero) {
    StockCode code(Market::SH, "600519");
    // 无时间戳 → invalid
    auto f1 = TencentProvider::parseFundamentals("1~x~600519~9.17", code);
    EXPECT_FALSE(f1.valid);
    // 全 0 字段（有时间戳但无行情值）→ invalid
    std::string rec;
    for (int i = 0; i < 74; ++i) {
        rec += (i == 30) ? "20260812161432" : "0";
        if (i != 73) rec += "~";
    }
    auto f2 = TencentProvider::parseFundamentals(rec, code);
    EXPECT_FALSE(f2.valid);
}
