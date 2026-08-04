#include "data/tdx/tdx_models.h"
#include "data/tdx/tdx_protocol.h"
#include "data/cn_encoding.h"
#include "foundation/utils/datetime.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace st {
namespace tdx {

namespace {

constexpr double kLiToYuan = 0.001;    // 厘 → 元
constexpr double kHandToShare = 100.0; // 手 → 股

std::tm tmFromTdx(const TdxTime& t) {
    std::tm tm{};
    tm.tm_year = t.year - 1900;
    tm.tm_mon = t.month - 1;
    tm.tm_mday = t.day;
    tm.tm_hour = t.hour;
    tm.tm_min = t.minute;
    tm.tm_sec = 0;
    return tm;
}

}  // namespace

DateTime tdxTimeToDateTime(const TdxTime& t) {
    const std::tm tm = tmFromTdx(t);
    const std::time_t tt = std::mktime(const_cast<std::tm*>(&tm));
    return std::chrono::system_clock::from_time_t(tt);
}

// 变长价格读取（返回厘值）
int64_t readPriceVar(const std::vector<uint8_t>& buf, size_t& pos) {
    return readVar(buf, pos);
}

Market marketFromByte(uint8_t m) {
    switch (m) {
        case 1: return Market::SH;
        case 0: return Market::SZ;
        case 2: return Market::BJ;
        default: return Market::Unknown;
    }
}

bool isTradableAShare(const StockCode& code) {
    const std::string& c = code.code();
    if (c.size() < 3) return false;
    if (code.market() == Market::SH) {
        return c.compare(0, 3, "600") == 0 || c.compare(0, 3, "601") == 0 ||
               c.compare(0, 3, "603") == 0 || c.compare(0, 3, "605") == 0 ||
               c.compare(0, 3, "688") == 0;
    }
    if (code.market() == Market::SZ) {
        return c.compare(0, 3, "000") == 0 || c.compare(0, 3, "001") == 0 ||
               c.compare(0, 3, "002") == 0 || c.compare(0, 3, "003") == 0 ||
               c.compare(0, 3, "300") == 0 || c.compare(0, 3, "301") == 0;
    }
    return false;
}

Market marketFromTdx(uint8_t m) {
    return marketFromByte(m);
}

bool isIndexCode(const StockCode& code) {
    const std::string& c = code.code();
    if (c.size() < 3) return false;
    if (code.market() == Market::SH) return c[0] == '0' && c[1] == '0' && c[2] == '0';
    if (code.market() == Market::SZ) return c[0] == '3' && c[1] == '9' && c[2] == '9';
    return false;
}

std::vector<TdxKlineRec> decodeKline(const std::vector<uint8_t>& payload,
                                     uint8_t klineCategory, bool isIndex) {
    std::vector<TdxKlineRec> out;
    if (payload.size() < 2) return out;
    const uint16_t count = rdU16(payload.data());
    size_t pos = 2;
    double lastLi = 0.0;  // 上一条收盘（厘）

    for (uint16_t i = 0; i < count && pos + 4 <= payload.size(); ++i) {
        const uint8_t* tp = payload.data() + pos;
        TdxTime t = decodeTime(tp, klineCategory);
        pos += 4;

        const int64_t openOff = readPriceVar(payload, pos);
        const int64_t closeOff = readPriceVar(payload, pos);
        const int64_t highOff = readPriceVar(payload, pos);
        const int64_t lowOff = readPriceVar(payload, pos);

        if (pos + 8 > payload.size()) break;
        const double volRaw = decodeVolume(rdU32(payload.data() + pos));
        pos += 4;
        const double amtRaw = decodeVolume(rdU32(payload.data() + pos));
        pos += 4;
        // 指数 K线记录比个股多 4 字节（上涨/下跌家数），跳过以对齐下一条
        if (isIndex) pos += 4;

        const double openLi = openOff + lastLi;
        const double closeLi = lastLi + openOff + closeOff;
        const double highLi = openOff + lastLi + highOff;
        const double lowLi = openOff + lastLi + lowOff;
        lastLi = lastLi + openOff + closeOff;

        TdxKlineRec r;
        r.time = tdxTimeToDateTime(t);
        r.open = openLi * kLiToYuan;
        r.high = highLi * kLiToYuan;
        r.low = lowLi * kLiToYuan;
        r.close = closeLi * kLiToYuan;
        // 指数 volume 原始单位为「万股」，个股为「手」→ 统一换算成股：
        //   个股 ×100（手→股），指数 ×10000（万股→股）
        // 分钟线另有 ÷100 的 injoyai 标定（个股/指数同套用）
        const bool isMinute = (klineCategory == Kline5Min || klineCategory == Kline15Min ||
                               klineCategory == Kline30Min || klineCategory == Kline60Min ||
                               klineCategory == KlineMin1 || klineCategory == KlineMin12);
        const double volShares = (isMinute ? volRaw / 100.0 : volRaw)
                                 * (isIndex ? 10000.0 : kHandToShare);
        r.volume = volShares;              // 股
        r.amount = amtRaw;                 // 元
        out.push_back(r);
    }
    return out;
}

std::vector<TdxQuoteRec> decodeQuote(const std::vector<uint8_t>& payload) {
    std::vector<TdxQuoteRec> out;
    if (payload.size() < 4) return out;
    size_t pos = 2;  // 跳过 2 字节
    const uint16_t number = rdU16(payload.data() + pos);
    pos += 2;

    for (uint16_t i = 0; i < number && pos + 9 <= payload.size(); ++i) {
        const uint8_t mkt = payload[pos];
        const std::string code(payload.begin() + pos + 1, payload.begin() + pos + 7);
        pos += 9;  // exchange + code6 + active1(2B)

        TdxQuoteRec r;
        r.code = StockCode(marketFromByte(mkt), code);

        // 价格字段单位分（÷100 得元），后 4 个字段相对 price 差分
        const int64_t priceFen = readPriceVar(payload, pos);
        const int64_t lastDiff = readPriceVar(payload, pos);
        const int64_t openDiff = readPriceVar(payload, pos);
        const int64_t highDiff = readPriceVar(payload, pos);
        const int64_t lowDiff = readPriceVar(payload, pos);
        readPriceVar(payload, pos);  // reversed_bytes0 (服务时间)
        readPriceVar(payload, pos);  // reversed_bytes1
        const int64_t volHands = readPriceVar(payload, pos);  // 成交量（手）
        readPriceVar(payload, pos);  // cur_vol 现量

        if (pos + 4 > payload.size()) break;
        const double amountYuan = decodeVolume(rdU32(payload.data() + pos));
        pos += 4;

        // 完整记录剩余字段（pytdx 字段序列），必须全部消费才能推进到下一记录：
        // s_vol / b_vol / reversed2 / reversed3 / 五档 bid1-5·ask1-5·量 /
        // reversed4(2B) / reversed5-8 / reversed9(2B)+active2(2B)
        readPriceVar(payload, pos);  // s_vol
        readPriceVar(payload, pos);  // b_vol
        readPriceVar(payload, pos);  // reversed_bytes2
        readPriceVar(payload, pos);  // reversed_bytes3
        for (int k = 0; k < 5; ++k) {  // 五档（暂不消费，仅推进）
            readPriceVar(payload, pos);  // bid_k
            readPriceVar(payload, pos);  // ask_k
            readPriceVar(payload, pos);  // bid_vol_k
            readPriceVar(payload, pos);  // ask_vol_k
        }
        if (pos + 2 > payload.size()) break;
        pos += 2;  // reversed_bytes4 (uint16)
        readPriceVar(payload, pos);  // reversed_bytes5
        readPriceVar(payload, pos);  // reversed_bytes6
        readPriceVar(payload, pos);  // reversed_bytes7
        readPriceVar(payload, pos);  // reversed_bytes8
        if (pos + 4 > payload.size()) break;
        pos += 4;  // reversed_bytes9 (int16) + active2 (uint16)

        r.price = static_cast<double>(priceFen) / 100.0;
        r.preClose = static_cast<double>(priceFen + lastDiff) / 100.0;
        r.open = static_cast<double>(priceFen + openDiff) / 100.0;
        r.high = static_cast<double>(priceFen + highDiff) / 100.0;
        r.low = static_cast<double>(priceFen + lowDiff) / 100.0;
        r.volume = static_cast<double>(volHands) * kHandToShare;  // 手 → 股
        r.amount = amountYuan;  // 元
        out.push_back(r);
    }
    return out;
}

std::vector<TdxMinuteRec> decodeMinute(const std::vector<uint8_t>& payload) {
    std::vector<TdxMinuteRec> out;
    if (payload.size() < 13) return out;
    const uint16_t count = rdU16(payload.data());
    // 实测：响应头 = count(2) + 11 字节（market/code/未知），数据从 [13] 起
    size_t pos = 13;
    double lastPriceFen = 0.0;  // 差分累积（单位分）

    for (uint16_t i = 0; i < count && pos < payload.size(); ++i) {
        const int64_t priceDiff = readPriceVar(payload, pos);
        readPriceVar(payload, pos);  // 跳过（均价或未知）
        const int64_t volHands = readPriceVar(payload, pos);
        lastPriceFen += static_cast<double>(priceDiff);

        TdxMinuteRec r;
        r.minute = static_cast<int>(i);
        if (i >= 120) r.minute += 30;  // 午休：11:30→13:00 中间 90 分钟
        r.price = lastPriceFen / 100.0;  // 分 → 元
        r.volume = static_cast<double>(volHands) * kHandToShare;  // 手 → 股（累计）
        out.push_back(r);
    }
    return out;
}

std::vector<TdxGbbqRec> decodeGbbq(const std::vector<uint8_t>& payload) {
    std::vector<TdxGbbqRec> out;
    if (payload.size() < 11) return out;
    const uint16_t count = rdU16(payload.data() + 9);
    size_t pos = 11;

    for (uint16_t i = 0; i < count && pos + 13 <= payload.size(); ++i) {
        // [0] exchange [1:7] code [7:8] ? [8:12] time [12] category
        const uint8_t* tp = payload.data() + pos + 8;
        TdxTime t = decodeTime(tp, 100);  // 日线格式（YYYYMMDD）
        const int category = payload[pos + 12];
        pos += 13;

        TdxGbbqRec r;
        r.date = static_cast<uint32_t>(t.year * 10000 + t.month * 100 + t.day);
        r.category = category;

        if (pos + 16 > payload.size()) break;
        if (category == 1) {
            // 4×float32：分红/配股价/送转/配股
            for (int k = 0; k < 4; ++k) {
                const float f = *reinterpret_cast<const float*>(payload.data() + pos + k * 4);
                if (k == 0) r.fenHong = f;
                else if (k == 1) r.peiGuJia = f;
                else if (k == 2) r.songZhuanGu = f;
                else r.peiGu = f;
            }
        } else if (category == 11 || category == 12) {
            const float f = *reinterpret_cast<const float*>(payload.data() + pos + 8);
            r.songZhuanGu = f;
        } else if (category == 13 || category == 14) {
            r.fenHong = *reinterpret_cast<const float*>(payload.data() + pos);
            r.songZhuanGu = *reinterpret_cast<const float*>(payload.data() + pos + 8);
        } else {
            // 股本变化：getVolume×1e4
            for (int k = 0; k < 4; ++k) {
                const double v = decodeVolume(rdU32(payload.data() + pos + k * 4)) * 1e4;
                if (k == 0) r.fenHong = v;
                else if (k == 1) r.peiGuJia = v;
                else if (k == 2) r.songZhuanGu = v;
                else r.peiGu = v;
            }
        }
        pos += 16;
        out.push_back(r);
    }
    return out;
}

uint32_t decodeCount(const std::vector<uint8_t>& payload) {
    if (payload.size() < 2) return 0;
    return rdU16(payload.data());
}

std::vector<TdxTickRec> decodeTransaction(const std::vector<uint8_t>& payload) {
    std::vector<TdxTickRec> out;
    if (payload.size() < 2) return out;
    const uint16_t count = rdU16(payload.data());
    size_t pos = 2;
    double lastPriceFen = 0.0;
    for (uint16_t i = 0; i < count && pos + 2 <= payload.size(); ++i) {
        const uint16_t minutes = rdU16(payload.data() + pos);  // 自 00:00 起的分钟数
        pos += 2;
        const int64_t priceDiff = readPriceVar(payload, pos);
        const int64_t volHands = readPriceVar(payload, pos);  // 成交量（手，实测 全日记合计≈报价手数）
        const int64_t num = readPriceVar(payload, pos);
        const int64_t buyorsell = readPriceVar(payload, pos);
        readPriceVar(payload, pos);  // 未知字段
        lastPriceFen += static_cast<double>(priceDiff);

        TdxTickRec r;
        r.hour = minutes / 60;
        r.minute = minutes % 60;
        r.price = lastPriceFen / 100.0;
        r.volume = static_cast<double>(volHands);  // 手（getIntraday 再 ×100 转股）
        r.num = static_cast<int>(num);
        r.buyorsell = static_cast<int>(buyorsell);
        out.push_back(r);
    }
    return out;
}

std::vector<TdxStockRec> decodeCodeList(const std::vector<uint8_t>& payload, Market market) {
    std::vector<TdxStockRec> out;
    if (payload.size() < 2) return out;
    const uint16_t count = rdU16(payload.data());
    size_t pos = 2;

    for (uint16_t i = 0; i < count && pos + 29 <= payload.size(); ++i) {
        // 记录布局（实测）：[0:6] 代码ASCII | [6:8]=0x0064 常量 | [8:16] 名称GBK(8B) |
        // [16:20] skip | [20] decimal | [21:25] lastprice | [25:29] skip
        // 记录内【不含】市场字段（由请求隐含），市场由调用方传入
        const std::string code(payload.begin() + pos, payload.begin() + pos + 6);
        std::string gbkName(payload.begin() + pos + 8, payload.begin() + pos + 16);
        // 名称字段固定 8 字节，短名以 null 填充 → 修剪尾部 null 再转码
        while (!gbkName.empty() && gbkName.back() == '\0') gbkName.pop_back();
        pos += 29;

        TdxStockRec r;
        r.code = StockCode(market, code);
        r.name = normalizeName(gbkToUtf8(gbkName));
        out.push_back(r);
    }
    return out;
}

} // namespace tdx
} // namespace st
