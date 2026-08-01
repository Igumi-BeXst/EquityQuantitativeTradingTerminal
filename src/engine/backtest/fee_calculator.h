#pragma once
#include "foundation/types.h"
#include "foundation/order.h"
namespace st {
struct FeeConfig { double commissionRate = 0.00025; double minCommission = 5.0; double stampTaxRate = 0.001; };
class FeeCalculator { public: Amount calculate(const Trade& trade) const; void setConfig(const FeeConfig&); };
}
