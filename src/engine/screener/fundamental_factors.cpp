#include "engine/screener/fundamental_factors.h"

#include <optional>

namespace st::factors {

namespace {

/// 取可用的基本面快照：未拉取（nullptr）或快照无效 → 降级为因子缺失
const QuoteFundamentals* snapshot(const FactorContext& ctx) {
    if (ctx.quote == nullptr || !ctx.quote->valid) return nullptr;
    return ctx.quote;
}

}  // namespace

std::optional<double> FundPe::calculate(const FactorContext& ctx) const {
    // 优先动态 PE，缺失回退 TTM / 静态 PE；亏损股（PE<=0）视为缺失
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr) return std::nullopt;
    if (quote->peDynamic > 0.0) return quote->peDynamic;
    if (quote->peTtm > 0.0) return quote->peTtm;
    if (quote->peStatic > 0.0) return quote->peStatic;
    return std::nullopt;
}

std::optional<double> FundPb::calculate(const FactorContext& ctx) const {
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || !(quote->pb > 0.0)) return std::nullopt;
    return quote->pb;
}

std::optional<double> FundRoe::calculate(const FactorContext& ctx) const {
    // ROE 可为负（亏损），仅以 0 作为缺失哨兵
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || quote->roe == 0.0) return std::nullopt;
    return quote->roe;
}

std::optional<double> FundRevenueYoy::calculate(const FactorContext& ctx) const {
    // 营收同比可为负（下滑），仅以 0 作为缺失哨兵
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || quote->revenueYoY == 0.0) return std::nullopt;
    return quote->revenueYoY;
}

std::optional<double> FundProfitYoy::calculate(const FactorContext& ctx) const {
    // 净利同比可为负（下滑甚至转亏），仅以 0 作为缺失哨兵
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || quote->netProfitYoY == 0.0) return std::nullopt;
    return quote->netProfitYoY;
}

std::optional<double> FundGrossMargin::calculate(const FactorContext& ctx) const {
    // 毛利率必须为正（负毛利率视为数据缺失，不做行业中性处理）
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || !(quote->grossMargin > 0.0)) return std::nullopt;
    return quote->grossMargin;
}

std::optional<double> FundDebtRatio::calculate(const FactorContext& ctx) const {
    // 资产负债率 0 视为缺失（真实公司很难为 0，数据源无值时为 0）
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || quote->debtRatio == 0.0) return std::nullopt;
    return quote->debtRatio;
}

std::optional<double> FundMarketCap::calculate(const FactorContext& ctx) const {
    const QuoteFundamentals* quote = snapshot(ctx);
    if (quote == nullptr || !(quote->marketCap > 0.0)) return std::nullopt;
    return quote->marketCap;
}

}  // namespace st::factors
