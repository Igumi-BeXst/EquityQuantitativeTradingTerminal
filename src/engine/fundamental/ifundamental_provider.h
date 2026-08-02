#pragma once

#include "engine/fundamental/fundamental_types.h"
#include <vector>
#include <optional>

namespace st {

/// 基本面数据源抽象接口 — 财报和公司资料
///
/// 独立于 IDataProvider（行情），关注点分离。
/// P3 仅定义接口，具体实现（AKShare 财务接口）待 P4 后接入。
class IFundamentalProvider {
public:
    virtual ~IFundamentalProvider() = default;

    /// 获取历史财报（按报告期降序）
    virtual std::vector<FinancialReport> getFinancialReports(const StockCode& code) = 0;

    /// 获取公司基本信息
    virtual std::optional<CompanyProfile> getCompanyProfile(const StockCode& code) = 0;
};

} // namespace st
