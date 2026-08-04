#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "data/tdx/tdx_protocol.h"
#include <cstdint>
#include <string>
#include <vector>

namespace st {
namespace tdx {

/// TdxTime → DateTime
DateTime tdxTimeToDateTime(const TdxTime& t);

/// K线记录（元/股）
struct TdxKlineRec {
    DateTime time;
    double open = 0.0, high = 0.0, low = 0.0, close = 0.0;
    double volume = 0.0;  // 股
    double amount = 0.0;  // 元
};

/// 实时行情记录（元/股）
struct TdxQuoteRec {
    StockCode code;
    double price = 0.0;      // 最新价
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double preClose = 0.0;
    double volume = 0.0;     // 股
    double amount = 0.0;     // 元
};

/// 分时点（元/股，volume 为累计）
struct TdxMinuteRec {
    int minute = 0;          // 从 09:00 起算的分钟数
    double price = 0.0;
    double volume = 0.0;     // 累计量（股）
};

/// 除权除息记录（category=1 时字段有效）
struct TdxGbbqRec {
    uint32_t date = 0;       // YYYYMMDD
    int category = 0;        // 1=除权除息
    double fenHong = 0.0;    // 每10股分红（元）
    double peiGuJia = 0.0;   // 配股价
    double songZhuanGu = 0.0;// 每10股送转
    double peiGu = 0.0;      // 每10股配股
};

/// 股票列表记录
struct TdxStockRec {
    StockCode code;
    std::string name;        // UTF-8
};

/// 全部接收【已解压】payload
std::vector<TdxKlineRec> decodeKline(const std::vector<uint8_t>& payload,
                                     uint8_t klineCategory);
std::vector<TdxQuoteRec> decodeQuote(const std::vector<uint8_t>& payload);
std::vector<TdxMinuteRec> decodeMinute(const std::vector<uint8_t>& payload);
std::vector<TdxGbbqRec> decodeGbbq(const std::vector<uint8_t>& payload);
uint32_t decodeCount(const std::vector<uint8_t>& payload);
/// 股票列表解码。记录内不含市场字段（由请求隐含），market 由调用方传入。
std::vector<TdxStockRec> decodeCodeList(const std::vector<uint8_t>& payload, Market market);

/// 市场字节 → Market 枚举（0=SZ 1=SH 2=BJ；其他 Unknown）
Market marketFromTdx(uint8_t m);

} // namespace tdx
} // namespace st
