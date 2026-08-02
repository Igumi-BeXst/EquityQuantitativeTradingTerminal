#pragma once

#include "foundation/types.h"
#include "foundation/order.h"
#include "foundation/enums.h"
#include <string>
#include <vector>

namespace st {

/// 自定义费用规则 — 扩展费用项
struct CustomFeeRule {
    std::string name;
    double rate = 0.0;          // 费率（按成交金额）
    bool applyOnBuy = true;     // 买入是否计费
    bool applyOnSell = true;    // 卖出是否计费
    double minAmount = 0.0;     // 最低收费
};

/// A股费用配置 — 全参数可配置
struct FeeConfig {
    // 佣金
    double commissionRate   = 0.00025;   // 万2.5
    double minCommission    = 5.0;       // 最低 5 元

    // 印花税（仅卖出）
    double stampTaxRate     = 0.001;     // 千分之一

    // 过户费（双向）
    double transferFeeRate  = 0.00002;   // 十万分之二

    // 经手费 / 证管费
    double handlingFeeRate  = 0.0000487; // 经手费
    double regulatoryFeeRate= 0.00002;   // 证管费

    // 自定义项
    std::vector<CustomFeeRule> customRules;

    /// 内置模板
    static FeeConfig defaultAShare();
    static FeeConfig lowCommissionAShare();   // 万1.5
    static FeeConfig etf();                   // 免印花税
    static FeeConfig hkStock();               // 港股
};

/// 费用明细 — 单笔成交的费用拆分
struct FeeBreakdown {
    Amount commission      = 0.0;  // 佣金
    Amount stampTax        = 0.0;  // 印花税
    Amount transferFee     = 0.0;  // 过户费
    Amount handlingFee     = 0.0;  // 经手费
    Amount regulatoryFee   = 0.0;  // 证管费
    Amount customFees      = 0.0;  // 自定义费用合计
    Amount total           = 0.0;  // 总费用

    [[nodiscard]] std::vector<std::pair<std::string, Amount>> items() const;
};

/// 费用计算器 — 计算单笔交易的手续费
class FeeCalculator {
public:
    FeeCalculator() = default;
    explicit FeeCalculator(const FeeConfig& config) : config_(config) {}

    void setConfig(const FeeConfig& config) { config_ = config; }
    const FeeConfig& config() const { return config_; }

    /// 计算一笔成交的费用明细
    FeeBreakdown calculate(const Trade& trade) const;

    /// 快捷计算总费用
    Amount calculateTotal(const Trade& trade) const {
        return calculate(trade).total;
    }

private:
    FeeConfig config_;
};

} // namespace st
