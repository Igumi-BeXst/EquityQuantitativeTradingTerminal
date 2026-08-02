#include "foundation/bar.h"
#include <algorithm>
#include <stdexcept>

namespace st {

BarSeries::BarSeries(std::vector<Bar> bars) : bars_(std::move(bars)) {}

const Bar& BarSeries::current() const {
    return at(size() - 1);
}

const Bar& BarSeries::at(size_t index) const {
    if (index >= bars_.size()) {
        throw std::out_of_range("BarSeries::at: index out of range");
    }
    return bars_[index];
}

const Bar& BarSeries::lookback(int n) const {
    // n=1 → 前一根, n=2 → 前两根, ...
    if (n <= 0 || static_cast<size_t>(n) >= bars_.size()) {
        throw std::out_of_range("BarSeries::lookback: lookback out of range");
    }
    return bars_[bars_.size() - static_cast<size_t>(n) - 1];
}

std::vector<Price> BarSeries::closes() const {
    std::vector<Price> result;
    result.reserve(bars_.size());
    for (const auto& bar : bars_) {
        result.push_back(bar.close);
    }
    return result;
}

std::vector<Price> BarSeries::highs() const {
    std::vector<Price> result;
    result.reserve(bars_.size());
    for (const auto& bar : bars_) {
        result.push_back(bar.high);
    }
    return result;
}

std::vector<Price> BarSeries::lows() const {
    std::vector<Price> result;
    result.reserve(bars_.size());
    for (const auto& bar : bars_) {
        result.push_back(bar.low);
    }
    return result;
}

std::vector<Volume> BarSeries::volumes() const {
    std::vector<Volume> result;
    result.reserve(bars_.size());
    for (const auto& bar : bars_) {
        result.push_back(bar.volume);
    }
    return result;
}

} // namespace st
