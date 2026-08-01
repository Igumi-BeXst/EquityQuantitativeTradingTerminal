#pragma once
#include "foundation/bar.h"
#include <vector>
namespace st {
class DataCache { public:
    void load(const StockCode& code, BarPeriod period, DateTime start, DateTime end);
    const BarSeries* get(const StockCode& code) const;
};
}
