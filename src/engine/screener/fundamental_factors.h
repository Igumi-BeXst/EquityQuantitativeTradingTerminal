#pragma once

#include "engine/screener/factor.h"

namespace st::factors {

/// 基本面因子（估值 / 盈利 / 成长 / 财务质量）
///
/// 数据来自 ctx.quote（东财行情快照扩展字段：PE/PB/ROE/营收/净利/毛利率/负债率/市值）。
/// 命名 fund_*；缺失（valid=false 或字段为 0）→ calculate 返回 nullopt。
#define ST_DECLARE_FUND(ClassName, FactorId, Display, Desc, Higher) \
    class ClassName : public IFactor {                              \
    public:                                                         \
        std::string name() const override { return FactorId; }      \
        std::string displayName() const override { return Display; }\
        std::string description() const override { return Desc; }   \
        FactorCategory category() const override {                  \
            return FactorCategory::Fundamental;                     \
        }                                                           \
        bool higherIsBetter() const override { return Higher; }     \
        std::optional<double> calculate(const FactorContext& ctx) const override; \
    };

ST_DECLARE_FUND(FundPe, "fund_pe", "市盈率(动态)",
                "市盈率(动态)，优先取动态 PE，缺失回退 TTM/静态；越低越好", false)
ST_DECLARE_FUND(FundPb, "fund_pb", "市净率",
                "市净率 PB；越低越好", false)
ST_DECLARE_FUND(FundRoe, "fund_roe", "ROE(加权)",
                "净资产收益率(加权) %；盈利能力，越高越好", true)
ST_DECLARE_FUND(FundRevenueYoy, "fund_revenue_yoy", "营收同比",
                "营业总收入同比增速 %；成长性，越高越好", true)
ST_DECLARE_FUND(FundProfitYoy, "fund_profit_yoy", "净利同比",
                "净利润同比增速 %；盈利成长，越高越好", true)
ST_DECLARE_FUND(FundGrossMargin, "fund_gross_margin", "毛利率",
                "毛利率 %；产品定价与成本控制能力，越高越好", true)
ST_DECLARE_FUND(FundDebtRatio, "fund_debt_ratio", "资产负债率",
                "资产负债率 %；财务杠杆与风险，越低越好", false)
ST_DECLARE_FUND(FundMarketCap, "fund_market_cap", "总市值",
                "总市值（元）；小市值偏好，越低越好", false)

} // namespace st::factors
