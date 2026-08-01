#include "data/data_cache.h"
namespace st {
void DataCache::load(const StockCode&, BarPeriod, DateTime, DateTime) {}
const BarSeries* DataCache::get(const StockCode&) const { return nullptr; }
}
