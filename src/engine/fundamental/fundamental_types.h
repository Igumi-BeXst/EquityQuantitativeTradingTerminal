#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include <string>
#include <vector>
#include <optional>

namespace st {

/// 财务报告核心指标（单期）
struct FinancialReport {
    StockCode code;
    std::string period;       // 报告期，如 "2024Q1" / "2023FY"
    DateTime reportDate;      // 披露日期

    // 利润表
    Amount revenue = 0.0;         // 营业收入
    Amount netProfit = 0.0;       // 净利润
    Amount operatingCashFlow = 0.0; // 经营现金流

    // 盈利能力
    double roe = 0.0;             // 净资产收益率 (%)
    double roa = 0.0;             // 总资产收益率 (%)
    double grossMargin = 0.0;     // 毛利率 (%)
    double netMargin = 0.0;       // 净利率 (%)

    // 每股指标
    double eps = 0.0;             // 每股收益
    double bvps = 0.0;            // 每股净资产

    // 估值
    double pe = 0.0;              // 市盈率
    double pb = 0.0;              // 市净率
    double ps = 0.0;              // 市销率
};

/// 公司基本信息 (F10)
struct CompanyProfile {
    StockCode code;
    std::string name;
    std::string industry;         // 行业
    std::string businessScope;    // 主营业务
    std::string region;           // 地区
    std::string website;
    DateTime establishedDate;     // 成立日期
    DateTime listDate;            // 上市日期
    double totalShares = 0.0;     // 总股本 (亿)
    double floatShares = 0.0;     // 流通股本 (亿)
};

} // namespace st
