#pragma once
#include "foundation/types.h"
#include "foundation/stock_code.h"
#include <vector>
namespace st {
class BacktestEngine { public:
    struct Result { double totalReturn; double annualReturn; double maxDrawdown; double sharpeRatio; };
    Result run(const std::vector<StockCode>& symbols, DateTime start, DateTime end);
}; }
