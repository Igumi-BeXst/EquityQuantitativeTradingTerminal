#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "data/tdx/tdx_protocol.h"
#include <array>
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

    /// 五档盘口（元 / 股），bids[0]=买一，asks[0]=卖一
    struct DepthLevel {
        double price = 0.0;
        double volume = 0.0;
    };
    std::array<DepthLevel, 5> bids;
    std::array<DepthLevel, 5> asks;

    double sVol = 0.0;  // 内盘（主动卖，股）
    double bVol = 0.0;  // 外盘（主动买，股）
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

/// 逐笔成交记录（0x0FC5）
struct TdxTickRec {
    int hour = 0, minute = 0;
    double price = 0.0;      // 元（差分累积）
    double volume = 0.0;     // 股
    int num = 0;             // 笔数
    int buyorsell = 0;       // 0=买 1=卖 2=中性
};

/// 全部接收【已解压】payload
/// isIndex: 指数 K线记录比个股多 4 字节（上涨/下跌家数），需跳过
std::vector<TdxKlineRec> decodeKline(const std::vector<uint8_t>& payload,
                                     uint8_t klineCategory, bool isIndex = false);
std::vector<TdxQuoteRec> decodeQuote(const std::vector<uint8_t>& payload);
std::vector<TdxMinuteRec> decodeMinute(const std::vector<uint8_t>& payload);
std::vector<TdxGbbqRec> decodeGbbq(const std::vector<uint8_t>& payload);
uint32_t decodeCount(const std::vector<uint8_t>& payload);
std::vector<TdxTickRec> decodeTransaction(const std::vector<uint8_t>& payload);
/// 股票列表解码。记录内不含市场字段（由请求隐含），market 由调用方传入。
std::vector<TdxStockRec> decodeCodeList(const std::vector<uint8_t>& payload, Market market);

/// 市场字节 → Market 枚举（0=SZ 1=SH 2=BJ；其他 Unknown）
Market marketFromTdx(uint8_t m);

/// 判断是否为指数代码（指数 K线记录多 4 字节涨跌家数）
/// SH 指数 = 000xxx（上证指数/沪深300/科创50…）；SZ 指数 = 399xxx
bool isIndexCode(const StockCode& code);

/// 判断是否为通达信板块指数（880xxx 行业 / 885xxx 概念）——记录格式同指数（多 4 字节）
bool isSectorIndexCode(const StockCode& code);

/// 是否为可交易 A 股（排除回购/债券/基金/指数等非交易品种）
/// SH 600/601/603/605/688；SZ 000/001/002/003/300/301
bool isTradableAShare(const StockCode& code);

} // namespace tdx
} // namespace st
