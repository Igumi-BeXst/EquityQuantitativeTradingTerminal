#pragma once

#include "foundation/enums.h"
#include "foundation/types.h"
#include <string>
#include <string_view>
#include <cstdint>
namespace st {

/// Represents a stock identifier: market + code
class StockCode {
public:
    StockCode() = default;
    StockCode(Market market, std::string code);
    explicit StockCode(std::string_view fullCode); // e.g. "SH600519" or "600519"

    Market market() const { return market_; }
    const std::string& code() const { return code_; }
    std::string fullCode() const;    // "SH600519"
    std::string displayCode() const; // "600519"

    bool isValid() const;

    bool operator==(const StockCode& o) const { return market_ == o.market_ && code_ == o.code_; }
    bool operator!=(const StockCode& o) const { return !(*this == o); }
    bool operator<(const StockCode& o) const { return fullCode() < o.fullCode(); }

    std::string toPinyinKey() const; // for fuzzy search index (placeholder)

private:
    Market market_ = Market::Unknown;
    std::string code_;
    void parseFullCode(std::string_view fc);
};

} // namespace st

// Hash support
namespace std {
template<> struct hash<st::StockCode> {
    size_t operator()(const st::StockCode& sc) const noexcept;
};
} // namespace std
