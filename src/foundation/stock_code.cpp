#include "foundation/stock_code.h"
#include <algorithm>
#include <cctype>

namespace st {

StockCode::StockCode(Market market, std::string code)
    : market_(market), code_(std::move(code)) {}

StockCode::StockCode(std::string_view fullCode) {
    parseFullCode(fullCode);
}

void StockCode::parseFullCode(std::string_view fc) {
    if (fc.empty()) return;
    // Try to detect market prefix
    if (fc.size() >= 2) {
        std::string prefix(fc.substr(0, 2));
        std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (prefix == "SH") {
            market_ = Market::SH;
            code_ = std::string(fc.substr(2));
            return;
        } else if (prefix == "SZ") {
            market_ = Market::SZ;
            code_ = std::string(fc.substr(2));
            return;
        } else if (prefix == "BJ") {
            market_ = Market::BJ;
            code_ = std::string(fc.substr(2));
            return;
        } else if (prefix == "HK") {
            market_ = Market::HK;
            code_ = std::string(fc.substr(2));
            return;
        }
    }
    // No prefix: try to identify from code pattern
    std::string code(fc);
    if (code.size() == 6 && std::all_of(code.begin(), code.end(),
                                         [](unsigned char c) { return std::isdigit(c); })) {
        // 6-digit A-share code
        if (code[0] == '6' || code[0] == '5') {
            market_ = Market::SH;
        } else if (code[0] == '0' || code[0] == '2' || code[0] == '3') {
            market_ = Market::SZ;
        } else if (code[0] == '4' || code[0] == '8') {
            market_ = Market::BJ;
        }
        code_ = std::move(code);
    }
}

std::string StockCode::fullCode() const {
    std::string prefix;
    switch (market_) {
        case Market::SH: prefix = "SH"; break;
        case Market::SZ: prefix = "SZ"; break;
        case Market::BJ: prefix = "BJ"; break;
        case Market::HK: prefix = "HK"; break;
        case Market::US: prefix = "US"; break;
        default: prefix = "??"; break;
    }
    return prefix + code_;
}

std::string StockCode::displayCode() const {
    return code_;
}

bool StockCode::isValid() const {
    return market_ != Market::Unknown && !code_.empty();
}

std::string StockCode::toPinyinKey() const {
    // Placeholder — will be populated from stock list data
    return "";
}

} // namespace st

namespace std {
size_t hash<st::StockCode>::operator()(const st::StockCode& sc) const noexcept {
    return std::hash<std::string>()(sc.fullCode());
}
} // namespace std
