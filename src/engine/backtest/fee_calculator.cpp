#include "engine/backtest/fee_calculator.h"
#include <algorithm>

namespace st {

FeeConfig FeeConfig::defaultAShare() {
    return FeeConfig{};
}

FeeConfig FeeConfig::lowCommissionAShare() {
    FeeConfig cfg;
    cfg.commissionRate = 0.00015;  // 万1.5
    return cfg;
}

FeeConfig FeeConfig::etf() {
    FeeConfig cfg;
    cfg.commissionRate = 0.0001;
    cfg.minCommission = 0.0;
    cfg.stampTaxRate = 0.0;
    cfg.transferFeeRate = 0.0;
    cfg.handlingFeeRate = 0.0;
    cfg.regulatoryFeeRate = 0.0;
    return cfg;
}

FeeConfig FeeConfig::hkStock() {
    FeeConfig cfg;
    cfg.commissionRate = 0.0003;
    cfg.minCommission = 100.0;
    cfg.stampTaxRate = 0.0013;
    cfg.transferFeeRate = 0.00002;
    cfg.handlingFeeRate = 0.00005;
    cfg.regulatoryFeeRate = 0.000027;
    return cfg;
}

std::vector<std::pair<std::string, Amount>> FeeBreakdown::items() const {
    std::vector<std::pair<std::string, Amount>> result;
    if (commission > 0)    result.emplace_back("佣金", commission);
    if (stampTax > 0)      result.emplace_back("印花税", stampTax);
    if (transferFee > 0)   result.emplace_back("过户费", transferFee);
    if (handlingFee > 0)   result.emplace_back("经手费", handlingFee);
    if (regulatoryFee > 0) result.emplace_back("证管费", regulatoryFee);
    if (customFees > 0)    result.emplace_back("其他费用", customFees);
    return result;
}

FeeBreakdown FeeCalculator::calculate(const Trade& trade) const {
    FeeBreakdown fees;
    if (trade.volume <= 0 || trade.price <= 0) return fees;

    Amount notional = trade.price * trade.volume;  // 成交金额
    bool isBuy = trade.direction == Direction::Buy;

    // 佣金（双向）
    fees.commission = notional * config_.commissionRate;
    if (config_.minCommission > 0) {
        fees.commission = std::max(fees.commission, config_.minCommission);
    }

    // 印花税（仅卖出）
    if (!isBuy && config_.stampTaxRate > 0) {
        fees.stampTax = notional * config_.stampTaxRate;
    }

    // 过户费（双向）
    if (config_.transferFeeRate > 0) {
        fees.transferFee = notional * config_.transferFeeRate;
    }

    // 经手费（双向）
    if (config_.handlingFeeRate > 0) {
        fees.handlingFee = notional * config_.handlingFeeRate;
    }

    // 证管费（双向）
    if (config_.regulatoryFeeRate > 0) {
        fees.regulatoryFee = notional * config_.regulatoryFeeRate;
    }

    // 自定义费用
    for (const auto& rule : config_.customRules) {
        if (isBuy && !rule.applyOnBuy) continue;
        if (!isBuy && !rule.applyOnSell) continue;
        Amount fee = notional * rule.rate;
        if (rule.minAmount > 0) {
            fee = std::max(fee, rule.minAmount);
        }
        fees.customFees += fee;
    }

    fees.total = fees.commission + fees.stampTax + fees.transferFee +
                 fees.handlingFee + fees.regulatoryFee + fees.customFees;
    return fees;
}

} // namespace st
