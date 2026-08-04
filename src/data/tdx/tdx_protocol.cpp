#include "data/tdx/tdx_protocol.h"

#include <zlib.h>
#include <cmath>
#include <cstring>

namespace st {
namespace tdx {

namespace {

constexpr int kMsgId = 0x0208D301;  // 固定消息 ID（与参考实现一致）

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

}  // namespace

int tdxMarket(Market m) {
    switch (m) {
        case Market::SH: return 1;
        case Market::SZ: return 0;
        case Market::BJ: return 2;
        default: return -1;
    }
}

int klineCategory(BarPeriod period) {
    switch (period) {
        case BarPeriod::Minute1:  return KlineMin1;
        case BarPeriod::Minute5:  return Kline5Min;
        case BarPeriod::Minute15: return Kline15Min;
        case BarPeriod::Minute30: return Kline30Min;
        case BarPeriod::Minute60: return Kline60Min;
        case BarPeriod::Daily:    return KlineDay;
        case BarPeriod::Weekly:   return KlineWeek;
        case BarPeriod::Monthly:  return KlineMonth;
        case BarPeriod::Quarterly:return KlineQuarter;
        case BarPeriod::Yearly:   return KlineYear;
        default: return -1;
    }
}

std::vector<uint8_t> encodeRequest(Cmd type, const std::vector<uint8_t>& data,
                                   uint32_t msgId) {
    std::vector<uint8_t> f(kRequestHeader + data.size());
    f[0] = kRequestPrefix;
    f[1] = static_cast<uint8_t>(msgId & 0xFF);
    f[2] = static_cast<uint8_t>((msgId >> 8) & 0xFF);
    f[3] = static_cast<uint8_t>((msgId >> 16) & 0xFF);
    f[4] = static_cast<uint8_t>((msgId >> 24) & 0xFF);
    f[5] = kControl01;
    const uint16_t len = static_cast<uint16_t>(data.size() + 2);
    f[6] = static_cast<uint8_t>(len & 0xFF);
    f[7] = static_cast<uint8_t>((len >> 8) & 0xFF);
    f[8] = static_cast<uint8_t>(len & 0xFF);
    f[9] = static_cast<uint8_t>((len >> 8) & 0xFF);
    f[10] = static_cast<uint8_t>(static_cast<uint16_t>(type) & 0xFF);
    f[11] = static_cast<uint8_t>(static_cast<uint16_t>(type) >> 8);
    std::copy(data.begin(), data.end(), f.begin() + kRequestHeader);
    return f;
}

ResponseFrame decodeResponse(const std::vector<uint8_t>& frame) {
    ResponseFrame r;
    if (frame.size() < static_cast<size_t>(kResponseHeader)) return r;
    // 响应前缀是固定字节序列 0xB1CB7400（大端字节序），逐字节比较
    static const uint8_t kRespPrefix[4] = {0xB1, 0xCB, 0x74, 0x00};
    if (std::memcmp(frame.data(), kRespPrefix, 4) != 0) return r;
    r.control = frame[4];
    r.type = rdU16(frame.data() + 10);
    const uint16_t zipLen = rdU16(frame.data() + 12);
    const uint16_t length = rdU16(frame.data() + 14);
    const std::vector<uint8_t> data(frame.begin() + kResponseHeader, frame.end());
    if (data.size() != zipLen) return r;
    if (zipLen == length) {
        r.payload = data;
    } else {
        if (!zlibInflate(data, r.payload)) return r;
        if (r.payload.size() != length) return r;
    }
    r.ok = true;
    return r;
}

// ---- 请求构造 ----
std::vector<uint8_t> buildConnectReq() {
    return {0x01};
}
std::vector<uint8_t> buildHeartReq() {
    return {};
}
std::vector<uint8_t> buildCountReq(uint8_t market) {
    return {market, 0x00, 0x75, 0xC7, 0x33, 0x01};
}
std::vector<uint8_t> buildCodeReq(uint8_t market, uint16_t start) {
    return {market, 0x00, static_cast<uint8_t>(start & 0xFF),
            static_cast<uint8_t>((start >> 8) & 0xFF)};
}
std::vector<uint8_t> buildQuoteReq(const std::vector<std::pair<uint8_t, std::string>>& mc) {
    std::vector<uint8_t> d{0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    putU16(d, static_cast<uint16_t>(mc.size()));
    for (const auto& [mkt, code] : mc) {
        d.push_back(mkt);
        d.insert(d.end(), code.begin(), code.end());
    }
    return d;
}
std::vector<uint8_t> buildMinuteReq(uint8_t market, const std::string& code) {
    std::vector<uint8_t> d{market, 0x00};
    d.insert(d.end(), code.begin(), code.end());
    d.push_back(0x00);
    d.push_back(0x00);
    d.push_back(0x00);
    d.push_back(0x00);
    return d;
}
std::vector<uint8_t> buildKlineReq(uint8_t market, const std::string& code,
                                   uint8_t category, uint16_t start, uint16_t count) {
    std::vector<uint8_t> d;
    d.push_back(market);
    d.push_back(0x00);
    d.insert(d.end(), code.begin(), code.end());
    d.push_back(category);
    d.push_back(0x00);
    d.push_back(0x01);
    d.push_back(0x00);
    putU16(d, start);
    putU16(d, count);
    d.insert(d.end(), 10, 0x00);  // 保留
    return d;
}
std::vector<uint8_t> buildGbbqReq(uint8_t market, const std::string& code) {
    std::vector<uint8_t> d{0x01, 0x00, market};
    d.insert(d.end(), code.begin(), code.end());
    return d;
}

// ---- 变长解码 ----
int32_t decodeVarInt(const uint8_t*& p, const uint8_t* end) {
    if (p >= end) return 0;
    int32_t data = 0;
    size_t i = 0;
    const uint8_t* start = p;
    while (p < end) {
        if (i == 0) {
            data += static_cast<int32_t>(*p & 0x3F);
        } else {
            data += static_cast<int32_t>(*p & 0x7F) << (6 + (i - 1) * 7);
        }
        const bool cont = (*p & 0x80) != 0;
        ++p;
        ++i;
        if (!cont) break;
    }
    if (p > start && (*start & 0x40)) data = -data;
    return data;
}

int64_t readVar(const std::vector<uint8_t>& buf, size_t& pos) {
    const uint8_t* p = buf.data() + pos;
    const uint8_t* end = buf.data() + buf.size();
    const int32_t v = decodeVarInt(p, end);
    pos = static_cast<size_t>(p - buf.data());
    return v;
}

double decodeVolume(uint32_t val) {
    const int32_t ivol = static_cast<int32_t>(val);
    const int32_t logpoint = ivol >> 24;         // [3]
    const int32_t hleax = (ivol >> 16) & 0xFF;   // [2]
    const int32_t lheax = (ivol >> 8) & 0xFF;    // [1]
    const int32_t lleax = ivol & 0xFF;           // [0]

    const int dwEcx = logpoint * 2 - 0x7F;
    const int dwEdx = logpoint * 2 - 0x86;

    const double dbl_xmm6 = std::pow(2.0, static_cast<double>(dwEcx));

    double dbl_xmm4 = 0.0;
    if (hleax > 0x80) {
        dbl_xmm4 = std::pow(2.0, static_cast<double>(dwEdx + 1)) * (64.0 + (hleax & 0x7F));
    } else {
        dbl_xmm4 = dbl_xmm6 * static_cast<double>(hleax) / 128.0;
    }

    double scale = 1.0;
    if ((hleax & 0x80) != 0) scale = 2.0;

    const double dbl_xmm3 = dbl_xmm6 * static_cast<double>(lheax) / 32768.0 * scale;
    const double dbl_xmm1 = dbl_xmm6 * static_cast<double>(lleax) / 8388608.0 * scale;

    return dbl_xmm6 + dbl_xmm4 + dbl_xmm3 + dbl_xmm1;
}

TdxTime decodeTime(const uint8_t bs4[4], uint8_t klineType) {
    TdxTime t;
    const bool minute = (klineType == KlineMin1 || klineType == KlineMin12 ||
                         klineType == Kline5Min || klineType == Kline15Min ||
                         klineType == Kline30Min || klineType == Kline60Min);
    if (minute) {
        const uint16_t ym = rdU16(bs4);
        const uint16_t hm = rdU16(bs4 + 2);
        t.year = static_cast<int>((ym >> 11) + 2004);
        const uint16_t rest = ym % 2048;
        t.month = static_cast<int>(rest / 100);
        t.day = static_cast<int>(rest % 100);
        t.hour = static_cast<int>(hm / 60);
        t.minute = static_cast<int>(hm % 60);
    } else {
        const uint32_t ymd = rdU32(bs4);
        t.year = static_cast<int>(ymd / 10000);
        t.month = static_cast<int>((ymd % 10000) / 100);
        t.day = static_cast<int>(ymd % 100);
        t.hour = 15;
        t.minute = 0;
    }
    return t;
}

bool zlibInflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    if (in.empty()) return false;
    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) return false;
    strm.next_in = const_cast<Bytef*>(in.data());
    strm.avail_in = static_cast<uInt>(in.size());
    std::vector<uint8_t> buf(65536);
    int ret = Z_OK;
    do {
        strm.next_out = buf.data();
        strm.avail_out = static_cast<uInt>(buf.size());
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_OK || ret == Z_STREAM_END) {
            out.insert(out.end(), buf.begin(), buf.end() - strm.avail_out);
        }
        if (ret != Z_OK && ret != Z_STREAM_END) break;
    } while (ret != Z_STREAM_END);
    inflateEnd(&strm);
    return ret == Z_STREAM_END;
}

} // namespace tdx
} // namespace st
