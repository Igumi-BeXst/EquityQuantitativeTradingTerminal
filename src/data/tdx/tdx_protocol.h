#pragma once

#include "foundation/enums.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace st {
namespace tdx {

/// 通达信命令 ID
enum class Cmd : uint16_t {
    Connect       = 0x000D,  // 建立连接
    Heart         = 0x0004,  // 心跳
    Login2        = 0x0FDB,  // 二次登录
    Count         = 0x044E,  // 股票数量
    Code          = 0x0450,  // 股票列表
    Quote         = 0x053E,  // 实时行情
    Minute        = 0x051D,  // 当日分时
    HistoryMinute = 0x0FB4,  // 历史分时
    MinuteTrade   = 0x0FC5,  // 分时成交
    Kline         = 0x052D,  // K线
    Gbbq          = 0x000F,  // 除权除息
    Finance       = 0x0A04,  // 财务数据（股本/市值/市盈率等）
};

// 帧头常量
constexpr uint8_t  kRequestPrefix  = 0x0C;
constexpr uint32_t kResponsePrefix = 0xB1CB7400;
constexpr uint8_t  kControl01      = 0x01;
constexpr int      kRequestHeader  = 12;
constexpr int      kResponseHeader = 16;

/// 构造请求帧字节（12 字节头 + data）
std::vector<uint8_t> encodeRequest(Cmd type, const std::vector<uint8_t>& data,
                                   uint32_t msgId);

/// 响应帧解析 → (ok, type, 已解压 payload)
struct ResponseFrame {
    bool ok = false;
    uint8_t control = 0;
    uint16_t type = 0;
    std::vector<uint8_t> payload;  // 已解压的数据域
};
ResponseFrame decodeResponse(const std::vector<uint8_t>& frame);

// ---- 请求构造（纯函数）----
std::vector<uint8_t> buildConnectReq();   // Data={0x01}
std::vector<uint8_t> buildHeartReq();
std::vector<uint8_t> buildCountReq(uint8_t market);
std::vector<uint8_t> buildCodeReq(uint8_t market, uint16_t start);
std::vector<uint8_t> buildQuoteReq(const std::vector<std::pair<uint8_t, std::string>>& mc);
std::vector<uint8_t> buildMinuteReq(uint8_t market, const std::string& code);
std::vector<uint8_t> buildKlineReq(uint8_t market, const std::string& code,
                                   uint8_t category, uint16_t start, uint16_t count);
std::vector<uint8_t> buildGbbqReq(uint8_t market, const std::string& code);
std::vector<uint8_t> buildTransactionReq(uint8_t market, const std::string& code,
                                         uint16_t start, uint16_t count);
/// 财务数据请求（0x0A04）：market(1) + code6
std::vector<uint8_t> buildFinanceReq(uint8_t market, const std::string& code);

/// 市场编码：SH=1, SZ=0, BJ=2；其他 -1
int tdxMarket(Market m);

// ---- K线类别 ----
constexpr uint8_t Kline5Min  = 0;   // 5分钟
constexpr uint8_t Kline15Min = 1;   // 15分钟
constexpr uint8_t Kline30Min = 2;   // 30分钟
constexpr uint8_t Kline60Min = 3;   // 60分钟
constexpr uint8_t KlineDay2  = 4;   // 日（÷100）
constexpr uint8_t KlineWeek  = 5;   // 周
constexpr uint8_t KlineMonth = 6;   // 月
constexpr uint8_t KlineMin1  = 7;   // 1分钟
constexpr uint8_t KlineMin12 = 8;   // 1分钟2
constexpr uint8_t KlineDay   = 9;   // 日
constexpr uint8_t KlineQuarter = 10; // 季
constexpr uint8_t KlineYear  = 11;  // 年

/// BarPeriod → K线类别（不支持的返回 -1）
int klineCategory(BarPeriod period);

// ---- 小端读取 ----
inline uint16_t rdU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t rdU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// ---- TDX 变长整数解码 ----
// getData：第一字节低6位+符号位，后续每字节低7位
int32_t decodeVarInt(const uint8_t*& p, const uint8_t* end);

/// 从 buf[pos:] 读一个变长整数，前进 pos，返回值
int64_t readVar(const std::vector<uint8_t>& buf, size_t& pos);

/// 量解码（TDX 专有浮点，getVolume2 等价实现）
double decodeVolume(uint32_t val);

/// 时间解码：分钟线 vs 日线/周线
struct TdxTime { int year = 0, month = 0, day = 0, hour = 0, minute = 0; };
TdxTime decodeTime(const uint8_t bs4[4], uint8_t klineType);

/// zlib 解压（TDX 响应数据域压缩）
bool zlibInflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

} // namespace tdx
} // namespace st
