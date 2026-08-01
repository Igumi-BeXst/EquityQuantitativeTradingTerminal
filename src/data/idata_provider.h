#pragma once
#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include "foundation/tick.h"
#include <vector>
#include <string>
namespace st {
class IDataProvider {
public:
    virtual ~IDataProvider() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual std::vector<Bar> getBars(const StockCode&, BarPeriod, DateTime, DateTime) = 0;
    virtual std::vector<StockCode> getStockList(Market) = 0;
};
}
