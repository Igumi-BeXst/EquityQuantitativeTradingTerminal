#pragma once
#include "foundation/bar.h"
#include <vector>
namespace st {
class DataRepository { public:
    bool init(const std::string& dbPath);
    void saveBars(const std::vector<Bar>& bars);
    std::vector<Bar> loadBars(const StockCode& code, BarPeriod period, DateTime start, DateTime end);
};
}
