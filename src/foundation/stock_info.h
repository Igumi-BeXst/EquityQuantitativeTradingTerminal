#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include <string>

namespace st {

/// 股票基本信息
struct StockInfo {
    StockCode code;
    std::string name;           // 股票名称
    std::string pinyin;         // 拼音全拼（搜索用）
    std::string pinyinInitials; // 拼音首字母（搜索用）
    std::string industry;       // 所属行业
    std::string conceptTags;    // 概念标签（分号分隔）
    std::string exchange;       // 交易所名称
    std::string board;          // 板块（主板/创业板/科创板...）
    DateTime listDate;          // 上市日期
    bool valid = false;

    [[nodiscard]] bool isValid() const { return valid && code.isValid(); }
};

} // namespace st
