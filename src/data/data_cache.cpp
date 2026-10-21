#include "data/data_cache.h"
#include <algorithm>

namespace st {

void DataCache::cacheBars(const StockCode& code, BarPeriod period, std::vector<Bar> bars) {
    auto series = std::make_shared<const BarSeries>(std::move(bars));
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[Key{code, period}] = std::move(series);
}

std::shared_ptr<const BarSeries> DataCache::get(const StockCode& code, BarPeriod period) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(Key{code, period});
    if (it == cache_.end()) return {};
    return it->second;
}

std::vector<Bar> DataCache::getBars(const StockCode& code, BarPeriod period) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(Key{code, period});
    if (it == cache_.end()) return {};
    const auto& bars = it->second;
    if (!bars) return {};
    return {bars->begin(), bars->end()};
}

bool DataCache::has(const StockCode& code, BarPeriod period) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(Key{code, period}) != cache_.end();
}

void DataCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

size_t DataCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

} // namespace st
