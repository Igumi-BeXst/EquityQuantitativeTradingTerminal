#pragma once

#include "foundation/types.h"
#include "foundation/enums.h"
#include "foundation/stock_code.h"
#include <array>

namespace st {

/// Single tick trade record
struct Tick {
    StockCode  code;
    DateTime   time;
    Price      price  = 0.0;
    Volume     volume = 0;
    Amount     amount = 0.0;
    Direction  direction = Direction::Buy; // 主动买卖方向

    [[nodiscard]] bool isValid() const {
        return price > 0 && volume > 0;
    }
};

/// Level-2 market depth (5-level ask/bid)
struct MarketDepth {
    StockCode code;
    DateTime  time;

    struct Level {
        Price  price  = 0.0;
        Volume volume = 0;
    };

    std::array<Level, 5> bids;  // 买盘 (highest first)
    std::array<Level, 5> asks;  // 卖盘 (lowest first)

    [[nodiscard]] Price bestBid() const { return bids[0].price; }
    [[nodiscard]] Price bestAsk() const { return asks[0].price; }
    [[nodiscard]] Price spread() const { return bestAsk() - bestBid(); }
};

/// Real-time quote snapshot
struct Quote {
    StockCode  code;
    DateTime   time;
    Price      lastPrice     = 0.0;   // 最新价
    Price      open          = 0.0;
    Price      high          = 0.0;
    Price      low           = 0.0;
    Price      preClose      = 0.0;   // 昨收
    Volume     volume        = 0;     // 成交量
    Amount     amount        = 0.0;   // 成交额
    Percentage change        = 0.0;   // 涨跌幅(%)
    Percentage turnover      = 0.0;   // 换手率(%)
    Volume     bidVol1       = 0;     // 买一量
    Price      bidPrice1     = 0.0;   // 买一价
    Volume     askVol1       = 0;     // 卖一量
    Price      askPrice1     = 0.0;   // 卖一价
};

} // namespace st
