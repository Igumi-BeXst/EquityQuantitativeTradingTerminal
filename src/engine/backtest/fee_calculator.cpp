#include "engine/backtest/fee_calculator.h"
namespace st { Amount FeeCalculator::calculate(const Trade&) const { return 0; } void FeeCalculator::setConfig(const FeeConfig&) {} }
