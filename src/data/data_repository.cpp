#include "data/data_repository.h"
namespace st {
bool DataRepository::init(const std::string&) { return true; }
void DataRepository::saveBars(const std::vector<Bar>&) {}
std::vector<Bar> DataRepository::loadBars(const StockCode&, BarPeriod, DateTime, DateTime) { return {}; }
}
