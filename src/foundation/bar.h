#pragma once

#include "foundation/types.h"
#include "foundation/enums.h"
#include "foundation/stock_code.h"
#include <vector>

namespace st {

/// Single OHLCV bar
struct Bar {
    StockCode  code;
    DateTime   time;
    BarPeriod  period = BarPeriod::Daily;

    Price  open   = 0.0;
    Price  high   = 0.0;
    Price  low    = 0.0;
    Price  close  = 0.0;
    Volume volume = 0;
    Amount amount = 0.0;      // 成交额 (元)

    // Optional fields
    Ratio turnoverRate = 0.0; // 换手率 (0.0 ~ 1.0)

    [[nodiscard]] bool isValid() const {
        return open > 0 && high > 0 && low > 0 && close > 0 && volume >= 0;
    }

    [[nodiscard]] Price typical() const { return (high + low + close) / 3.0; }
    [[nodiscard]] Price median()  const { return (high + low) / 2.0; }
};

/// Time-ordered Bar sequence for technical analysis
class BarSeries {
public:
    BarSeries() = default;
    explicit BarSeries(std::vector<Bar> bars);

    [[nodiscard]] size_t size() const { return bars_.size(); }
    [[nodiscard]] bool empty() const { return bars_.empty(); }

    /// Access the last bar (most recent)
    [[nodiscard]] const Bar& current() const;
    [[nodiscard]] int currentIndex() const { return static_cast<int>(size()) - 1; }

    /// Access by index (0 = oldest)
    [[nodiscard]] const Bar& at(size_t index) const;
    [[nodiscard]] const Bar& operator[](size_t index) const { return at(index); }

    /// Look back N bars from current
    [[nodiscard]] const Bar& lookback(int n) const; // n=1 = previous bar

    /// Get close price series for indicator calculation
    [[nodiscard]] std::vector<Price> closes() const;
    [[nodiscard]] std::vector<Price> highs() const;
    [[nodiscard]] std::vector<Price> lows() const;
    [[nodiscard]] std::vector<Volume> volumes() const;

    // Iteration
    auto begin() { return bars_.begin(); }
    auto end()   { return bars_.end(); }
    [[nodiscard]] auto begin() const { return bars_.begin(); }
    [[nodiscard]] auto end()   const { return bars_.end(); }

private:
    std::vector<Bar> bars_;
};

} // namespace st
