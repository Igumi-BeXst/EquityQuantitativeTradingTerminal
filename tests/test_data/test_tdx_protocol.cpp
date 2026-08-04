#include <gtest/gtest.h>
#include "data/tdx/tdx_protocol.h"
#include "data/tdx/tdx_models.h"
#include "data/cn_encoding.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"

#include <zlib.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#ifndef ST_TEST_FIXTURE_DIR
#define ST_TEST_FIXTURE_DIR "tests/fixtures"
#endif

using namespace st;

namespace {

// ---- 测试辅助：TDX 变长编码（decodeVarInt 的逆）----
void putVar(std::vector<uint8_t>& v, int64_t val) {
    const uint64_t mag = static_cast<uint64_t>(val < 0 ? -val : val);
    uint8_t b0 = static_cast<uint8_t>(mag & 0x3F);
    if (val < 0) b0 |= 0x40;
    uint64_t rest = mag >> 6;
    if (rest > 0) b0 |= 0x80;
    v.push_back(b0);
    while (rest > 0) {
        uint8_t b = static_cast<uint8_t>(rest & 0x7F);
        rest >>= 7;
        if (rest > 0) b |= 0x80;
        v.push_back(b);
    }
}

void putU16(std::vector<uint8_t>& v, uint16_t n) {
    v.push_back(static_cast<uint8_t>(n & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
}
void putU32(std::vector<uint8_t>& v, uint32_t n) {
    v.push_back(static_cast<uint8_t>(n & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((n >> 24) & 0xFF));
}

// 构造合法响应帧：16B 头 + (可选 zlib 压缩) payload
std::vector<uint8_t> makeFrame(uint16_t type, const std::vector<uint8_t>& payload, bool compress) {
    std::vector<uint8_t> body = payload;
    std::vector<uint8_t> comp;
    if (compress) {
        uLongf dstLen = compressBound(static_cast<uLong>(payload.size()));
        comp.resize(dstLen);
        if (compress2(comp.data(), &dstLen, payload.data(),
                      static_cast<uLong>(payload.size()), Z_BEST_SPEED) != Z_OK) {
            return {};
        }
        comp.resize(dstLen);
        body = std::move(comp);
    }
    std::vector<uint8_t> f;
    // [0:4]前缀 [4]control [5]msgid [6:10]未用 [10:12]type [12:14]zipLen [14:16]length
    f.insert(f.end(), {0xB1, 0xCB, 0x74, 0x00, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00});
    putU16(f, type);
    putU16(f, static_cast<uint16_t>(body.size()));  // zipLen
    putU16(f, static_cast<uint16_t>(payload.size()));  // length（未压缩）
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

std::vector<uint8_t> readFixture(const char* name) {
    std::ifstream f(std::string(ST_TEST_FIXTURE_DIR) + "/tdx/" + name, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

// ============================================================
// 请求编码
// ============================================================
TEST(TdxProtocol, EncodeRequestHeader) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto f = tdx::encodeRequest(tdx::Cmd::Kline, data, 0x12345678);
    EXPECT_EQ(f.size(), 12u + data.size());
    EXPECT_EQ(f[0], tdx::kRequestPrefix);              // 0x0C
    EXPECT_EQ(f[1], 0x78);                              // msgid LE
    EXPECT_EQ(f[2], 0x56);
    EXPECT_EQ(f[3], 0x34);
    EXPECT_EQ(f[4], 0x12);
    EXPECT_EQ(f[5], tdx::kControl01);                  // 0x01
    EXPECT_EQ(f[6], static_cast<uint8_t>(5));           // len = data+2 = 5
    EXPECT_EQ(f[7], 0);
    EXPECT_EQ(f[8], 5);                                 // 重复 length
    EXPECT_EQ(f[9], 0);
    EXPECT_EQ(f[10], static_cast<uint8_t>(0x2D));       // type 0x052D LE
    EXPECT_EQ(f[11], static_cast<uint8_t>(0x05));
    for (size_t i = 0; i < data.size(); ++i) EXPECT_EQ(f[12 + i], data[i]);
}

TEST(TdxProtocol, BuildTransactionReq) {
    auto d = tdx::buildTransactionReq(1, "600519", 0, 1000);
    ASSERT_EQ(d.size(), 12u);
    EXPECT_EQ(d[0], 1);   // market(uint16 LE)
    EXPECT_EQ(d[1], 0);
    EXPECT_EQ(std::string(d.begin() + 2, d.begin() + 8), "600519");
    EXPECT_EQ(d[8], 0);   // start
    EXPECT_EQ(d[9], 0);
    EXPECT_EQ(d[10], 0xE8);  // count=1000
    EXPECT_EQ(d[11], 0x03);
}

TEST(TdxProtocol, BuildRequests) {
    EXPECT_EQ(tdx::buildConnectReq(), (std::vector<uint8_t>{0x01}));
    EXPECT_EQ(tdx::buildHeartReq(), (std::vector<uint8_t>{}));
    EXPECT_EQ(tdx::buildCountReq(1), (std::vector<uint8_t>{0x01, 0x00, 0x75, 0xC7, 0x33, 0x01}));
    EXPECT_EQ(tdx::buildCodeReq(1, 0), (std::vector<uint8_t>{0x01, 0x00, 0x00, 0x00}));
    EXPECT_EQ(tdx::buildMinuteReq(1, "600519"),
              (std::vector<uint8_t>{0x01, 0x00, '6', '0', '0', '5', '1', '9', 0, 0, 0, 0}));
    EXPECT_EQ(tdx::buildGbbqReq(1, "600519"),
              (std::vector<uint8_t>{0x01, 0x00, 0x01, '6', '0', '0', '5', '1', '9'}));

    // K线请求：market + 0 + code(6) + category + 0 + 0x01 + 0 + start(2) + count(2) + 10 保留
    auto k = tdx::buildKlineReq(1, "600519", 9, 0, 10);
    EXPECT_EQ(k.size(), 26u);
    EXPECT_EQ(k[0], 1);
    EXPECT_EQ(k[1], 0);
    EXPECT_EQ(std::string(k.begin() + 2, k.begin() + 8), "600519");
    EXPECT_EQ(k[8], 9);
    EXPECT_EQ(k[10], 1);
    EXPECT_EQ(k[12], 0);  // start=0
    EXPECT_EQ(k[13], 0);
    EXPECT_EQ(k[14], 10); // count=10
    EXPECT_EQ(k[15], 0);

    // 报价请求：8 字节头 + count(2) + (market, code)*
    auto q = tdx::buildQuoteReq({{1, "600519"}, {0, "000001"}});
    EXPECT_EQ(q.size(), 8u + 2u + 2u * 7u);
    EXPECT_EQ(q[8], 2);             // count
    EXPECT_EQ(q[9], 0);
    EXPECT_EQ(q[10], 1);            // market 1
    EXPECT_EQ(std::string(q.begin() + 11, q.begin() + 17), "600519");
    EXPECT_EQ(q[17], 0);            // market 0
    EXPECT_EQ(std::string(q.begin() + 18, q.begin() + 24), "000001");
}

TEST(TdxProtocol, MarketAndCategory) {
    EXPECT_EQ(tdx::tdxMarket(Market::SH), 1);
    EXPECT_EQ(tdx::tdxMarket(Market::SZ), 0);
    EXPECT_EQ(tdx::tdxMarket(Market::BJ), 2);
    EXPECT_EQ(tdx::klineCategory(BarPeriod::Daily), tdx::KlineDay);
    EXPECT_EQ(tdx::klineCategory(BarPeriod::Weekly), tdx::KlineWeek);
    EXPECT_EQ(tdx::klineCategory(BarPeriod::Monthly), tdx::KlineMonth);
    EXPECT_EQ(tdx::klineCategory(BarPeriod::Minute5), tdx::Kline5Min);
    EXPECT_EQ(tdx::klineCategory(static_cast<BarPeriod>(99)), -1);  // 不支持返回 -1
}

// ============================================================
// 响应解码
// ============================================================
TEST(TdxProtocol, DecodeResponseUncompressed) {
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
    auto frame = makeFrame(0x052D, payload, false);
    auto r = tdx::decodeResponse(frame);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.type, 0x052D);
    EXPECT_EQ(r.payload, payload);
}

TEST(TdxProtocol, DecodeResponseZlib) {
    std::vector<uint8_t> payload(300);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i * 7);
    auto frame = makeFrame(0x044E, payload, true);
    auto r = tdx::decodeResponse(frame);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.type, 0x044E);
    ASSERT_EQ(r.payload.size(), payload.size());
    EXPECT_EQ(r.payload, payload);
}

TEST(TdxProtocol, DecodeResponseRejectsBadPrefix) {
    std::vector<uint8_t> frame = {0x00, 0x11, 0x22, 0x33, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = tdx::decodeResponse(frame);
    EXPECT_FALSE(r.ok);
}

TEST(TdxProtocol, DecodeResponseRejectsShort) {
    std::vector<uint8_t> frame = {0xB1, 0xCB};  // 不足 16 字节头
    auto r = tdx::decodeResponse(frame);
    EXPECT_FALSE(r.ok);
}

// ============================================================
// 变长整数
// ============================================================
TEST(TdxProtocol, VarintRoundTrip) {
    const int64_t kValues[] = {0, 1, -1, 63, 64, -64, 100, 2258, -3062, 132836, -132836, 15172465, 37450, 2170};
    for (int64_t v : kValues) {
        std::vector<uint8_t> buf;
        putVar(buf, v);
        size_t pos = 0;
        EXPECT_EQ(tdx::readVar(buf, pos), v) << "value=" << v;
        EXPECT_EQ(pos, buf.size());
    }
}

// ============================================================
// 时间解码
// ============================================================
TEST(TdxProtocol, DecodeTimeDay) {
    std::vector<uint8_t> b;
    putU32(b, 20260801);  // 日线 YYYYMMDD 十进制小端
    auto t = tdx::decodeTime(b.data(), tdx::KlineDay);
    EXPECT_EQ(t.year, 2026);
    EXPECT_EQ(t.month, 8);
    EXPECT_EQ(t.day, 1);
    EXPECT_EQ(t.hour, 15);  // 日线固定 15:00
}

TEST(TdxProtocol, DecodeTimeMinute) {
    // 位压缩: ym = (year-2004)<<11 | (month*100+day); hm = hour*60+minute
    const uint16_t ym = static_cast<uint16_t>(((2026 - 2004) << 11) | (8 * 100 + 1));
    const uint16_t hm = static_cast<uint16_t>(9 * 60 + 30);
    std::vector<uint8_t> b;
    putU16(b, ym);
    putU16(b, hm);
    auto t = tdx::decodeTime(b.data(), tdx::Kline5Min);
    EXPECT_EQ(t.year, 2026);
    EXPECT_EQ(t.month, 8);
    EXPECT_EQ(t.day, 1);
    EXPECT_EQ(t.hour, 9);
    EXPECT_EQ(t.minute, 30);
}

// ============================================================
// 量解码
// ============================================================
TEST(TdxProtocol, DecodeVolumeBasic) {
    // logpoint=1, 其余 0 → 2^-125，极小正数
    const double tiny = tdx::decodeVolume(0x01000000u);
    EXPECT_GT(tiny, 0.0);
    EXPECT_LT(tiny, 1e-30);
    // 全 0 → 2^-127
    const double zero = tdx::decodeVolume(0x00000000u);
    EXPECT_GT(zero, 0.0);
    EXPECT_LT(zero, 1e-30);
}

// ============================================================
// 模型解码：Count / CodeList
// ============================================================
TEST(TdxModels, DecodeCount) {
    std::vector<uint8_t> p = {0xFA, 0x6B};  // 小端 0x6BFA = 27642
    EXPECT_EQ(tdx::decodeCount(p), 27642u);
}

TEST(TdxModels, DecodeCodeListSynthetic) {
    // GBK "贵州茅台"（与腾讯/TDX 实测字节一致）
    const std::string gbkMaotai = std::string("\xB9\xF3\xD6\xDD\xC3\xA9\xCC\xA8", 8);
    std::vector<uint8_t> p;
    putU16(p, 1);                                    // count
    p.insert(p.end(), {'6', '0', '0', '5', '1', '9'});  // code
    p.push_back(0x64); p.push_back(0x00);            // [6:8] 常量
    p.insert(p.end(), gbkMaotai.begin(), gbkMaotai.end());  // 名称 8B
    p.insert(p.end(), 13, 0x00);                     // [16:29] 其余

    auto recs = tdx::decodeCodeList(p, Market::SH);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].code.market(), Market::SH);    // 市场由调用方传入
    EXPECT_EQ(recs[0].code.code(), "600519");
    EXPECT_EQ(recs[0].name, "贵州茅台");
}

TEST(TdxModels, DecodeCodeListMarketFromArg) {
    std::vector<uint8_t> p;
    putU16(p, 1);
    p.insert(p.end(), {'0', '0', '0', '0', '0', '1'});
    p.push_back(0x64); p.push_back(0x00);
    p.insert(p.end(), 8, 0x00);
    p.insert(p.end(), 13, 0x00);

    auto sz = tdx::decodeCodeList(p, Market::SZ);
    ASSERT_EQ(sz.size(), 1u);
    EXPECT_EQ(sz[0].code.market(), Market::SZ);
}

TEST(TdxModels, DecodeCodeListNormalizesName) {
    // 名称含全角空格/全角字母，应被 normalizeName 清理
    std::vector<uint8_t> p;
    putU16(p, 1);
    p.insert(p.end(), {'0', '0', '0', '0', '0', '2'});
    p.push_back(0x64); p.push_back(0x00);
    // GBK "万 科Ａ" → normalize → "万科A"
    // 名称字段固定 8B（7 字节 + 1 填充 null），其后 13B 尾部 = 记录共 29B
    const std::string gbkWanka = std::string("\xCD\xF2\x20\xBF\xC6\xA3\xC1", 7);
    p.insert(p.end(), gbkWanka.begin(), gbkWanka.end());
    p.insert(p.end(), 14, 0x00);
    auto recs = tdx::decodeCodeList(p, Market::SZ);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].name, "万科A");
}

// ============================================================
// 模型解码：K线（合成差分）
// ============================================================
TEST(TdxModels, DecodeKlineSynthetic) {
    std::vector<uint8_t> p;
    putU16(p, 2);  // count
    // bar1: 2026-08-01 open=10.00 high=11.50 low=9.80 close=11.00 (厘 ×1000)
    putU32(p, 20260801);
    putVar(p, 10000);   // openOff
    putVar(p, 1000);    // closeOff = close - open
    putVar(p, 1500);    // highOff = high - open
    putVar(p, -200);    // lowOff = low - open
    putU32(p, 0);       // vol（decodeVolume≈0）
    putU32(p, 0);       // amount
    // bar2: 2026-08-02 open=11.20 high=11.40 low=11.10 close=11.30
    putU32(p, 20260802);
    putVar(p, 200);     // openOff = 11200 - 11000
    putVar(p, 100);     // closeOff = 11300 - 11200
    putVar(p, 200);     // highOff = 11400 - 11200
    putVar(p, -100);    // lowOff = 11100 - 11200
    putU32(p, 0);
    putU32(p, 0);

    auto bars = tdx::decodeKline(p, tdx::KlineDay);
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_NEAR(bars[0].open, 10.00, 1e-6);
    EXPECT_NEAR(bars[0].close, 11.00, 1e-6);
    EXPECT_NEAR(bars[0].high, 11.50, 1e-6);
    EXPECT_NEAR(bars[0].low, 9.80, 1e-6);
    EXPECT_EQ(st::utils::toDateString(bars[0].time), "2026-08-01");
    EXPECT_NEAR(bars[1].open, 11.20, 1e-6);
    EXPECT_NEAR(bars[1].close, 11.30, 1e-6);
}

// ============================================================
// 模型解码：报价（合成）
// ============================================================
TEST(TdxModels, DecodeQuoteSynthetic) {
    std::vector<uint8_t> p;
    putU16(p, 0);       // 前 2 字节跳过
    putU16(p, 1);       // count
    p.push_back(1);     // market SH
    p.insert(p.end(), {'6', '0', '0', '5', '1', '9'});
    putU16(p, 0);       // active
    putVar(p, 132836);  // price 分
    putVar(p, 3062);    // lastDiff → preClose = 135898
    putVar(p, 2170);    // openDiff → open = 135006
    putVar(p, 2258);    // highDiff → high = 135094
    putVar(p, 0);       // lowDiff → low = 132836
    putVar(p, 0);       // rev0
    putVar(p, 0);       // rev1
    putVar(p, 37450);   // vol 手
    putVar(p, 661);     // curVol
    putU32(p, 0);       // amount
    // 完整记录尾部字段（decodeQuote 消费推进到下一记录）：s_vol/b_vol/rev2/rev3/
    // 五档×20/rev4(2B)/rev5-8/rev9(2B)+active2(2B)
    putVar(p, 0); putVar(p, 0);  // s_vol, b_vol
    putVar(p, 0); putVar(p, 0);  // rev2, rev3
    for (int k = 0; k < 5; ++k) {
        putVar(p, 0); putVar(p, 0); putVar(p, 0); putVar(p, 0);  // 五档
    }
    putU16(p, 0);                                // rev4 (uint16)
    putVar(p, 0); putVar(p, 0); putVar(p, 0); putVar(p, 0);  // rev5-8
    putU16(p, 0); putU16(p, 0);                  // rev9 (int16) + active2

    auto recs = tdx::decodeQuote(p);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_EQ(recs[0].code.market(), Market::SH);
    EXPECT_EQ(recs[0].code.code(), "600519");
    EXPECT_NEAR(recs[0].price, 1328.36, 1e-6);
    EXPECT_NEAR(recs[0].preClose, 1358.98, 1e-6);
    EXPECT_NEAR(recs[0].open, 1350.06, 1e-6);
    EXPECT_NEAR(recs[0].high, 1350.94, 1e-6);
    EXPECT_NEAR(recs[0].low, 1328.36, 1e-6);
    EXPECT_EQ(recs[0].volume, 37450 * 100);  // 手→股
}

// ============================================================
// 模型解码：fixture 驱动（真实服务器抓包字节）
// ============================================================
TEST(TdxModels, DecodeKlineFromFixture) {
    auto p = readFixture("kline_600519_day.bin");
    ASSERT_FALSE(p.empty()) << "fixture 缺失";
    auto bars = tdx::decodeKline(p, tdx::KlineDay);
    ASSERT_EQ(bars.size(), 5u);
    // 该服务器响应为 oldest-first；最后一根应 ≈ 2026-08-04 收盘 1328.36
    EXPECT_EQ(st::utils::toDateString(bars.back().time), "2026-08-04");
    EXPECT_NEAR(bars.back().close, 1328.36, 0.5);
    EXPECT_GT(bars.back().volume, 0);
    EXPECT_GT(bars.back().amount, 0);
}

TEST(TdxModels, DecodeQuoteFromFixture) {
    auto p = readFixture("quote_600519.bin");
    ASSERT_FALSE(p.empty()) << "fixture 缺失";
    auto recs = tdx::decodeQuote(p);
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_NEAR(recs[0].price, 1328.36, 0.5);
    EXPECT_NEAR(recs[0].preClose, 1358.98, 0.5);
    EXPECT_EQ(recs[0].code.code(), "600519");
}

TEST(TdxModels, DecodeQuoteBatchFromFixture) {
    // 6 只批量报价：完整记录消费（含五档等尾部字段）才能对齐后续记录
    auto p = readFixture("quote_6codes.bin");
    ASSERT_FALSE(p.empty()) << "fixture 缺失";
    auto recs = tdx::decodeQuote(p);
    ASSERT_EQ(recs.size(), 6u);
    EXPECT_EQ(recs[0].code.code(), "600519");
    EXPECT_NEAR(recs[0].price, 1328.36, 0.5);
    EXPECT_NEAR(recs[0].preClose, 1358.98, 0.5);
    EXPECT_EQ(recs[1].code.code(), "601318");
    EXPECT_NEAR(recs[1].price, 53.93, 0.5);
    EXPECT_EQ(recs[2].code.code(), "600036");
    EXPECT_NEAR(recs[2].price, 39.28, 0.5);
    EXPECT_EQ(recs[3].code.market(), Market::SZ);
    EXPECT_EQ(recs[3].code.code(), "000001");
    EXPECT_NEAR(recs[3].price, 11.44, 0.5);
    EXPECT_EQ(recs[4].code.code(), "300750");
    EXPECT_NEAR(recs[4].price, 395.10, 0.5);
    EXPECT_EQ(recs[5].code.code(), "000858");
    EXPECT_NEAR(recs[5].price, 76.88, 0.5);
}

TEST(TdxModels, DecodeKlineIndexFromFixture) {
    // 上证指数 K线：记录多 4 字节涨跌家数，需跳过
    auto p = readFixture("kline_000001_day.bin");
    ASSERT_FALSE(p.empty()) << "fixture 缺失";
    const StockCode idx(Market::SH, "000001");
    auto bars = tdx::decodeKline(p, tdx::KlineDay, tdx::isIndexCode(idx));
    ASSERT_EQ(bars.size(), 5u);
    EXPECT_EQ(st::utils::toDateString(bars.front().time), "2026-07-29");
    EXPECT_EQ(st::utils::toDateString(bars.back().time), "2026-08-04");
    EXPECT_NEAR(bars.back().open, 3816.37, 1.0);
    for (const auto& b : bars) {
        EXPECT_GT(b.close, 1000);   // 指数点位量级（非垃圾值）
        EXPECT_GT(b.high, b.low);
    }
}

TEST(TdxModels, DecodeTransactionFromFixture) {
    auto p = readFixture("transaction_600519.bin");
    ASSERT_FALSE(p.empty()) << "fixture 缺失";
    auto ticks = tdx::decodeTransaction(p);
    ASSERT_EQ(ticks.size(), 1000u);
    EXPECT_NEAR(ticks[0].price, 1332.35, 0.5);       // 页首笔
    EXPECT_NEAR(ticks.back().price, 1328.36, 0.5);   // 收盘
    EXPECT_GT(ticks[0].volume, 0);                    // 量为手
    EXPECT_LE(ticks[0].hour * 60 + ticks[0].minute,
              ticks.back().hour * 60 + ticks.back().minute);  // 页内按时间序
    // 收盘前后应接近（差分累积连续）
    for (size_t i = 1; i < ticks.size(); ++i) {
        EXPECT_NEAR(ticks[i].price, ticks[i - 1].price, 20.0);
    }
}

TEST(TdxModels, IsIndexCode) {
    EXPECT_TRUE(tdx::isIndexCode(StockCode(Market::SH, "000001")));   // 上证指数
    EXPECT_TRUE(tdx::isIndexCode(StockCode(Market::SH, "000300")));   // 沪深300
    EXPECT_TRUE(tdx::isIndexCode(StockCode(Market::SZ, "399001")));   // 深证成指
    EXPECT_TRUE(tdx::isIndexCode(StockCode(Market::SZ, "399006")));   // 创业板指
    EXPECT_FALSE(tdx::isIndexCode(StockCode(Market::SH, "600519")));
    EXPECT_FALSE(tdx::isIndexCode(StockCode(Market::SZ, "000001")));  // 平安银行
    EXPECT_FALSE(tdx::isIndexCode(StockCode(Market::SZ, "300750")));
}
