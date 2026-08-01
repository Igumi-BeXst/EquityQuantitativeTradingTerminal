#pragma once
#include <vector>
namespace st { struct Performance { double totalReturn; double annualReturn; double maxDrawdown; double sharpe; double calmar; double winRate; };
class PerformanceCalculator { public: static Performance calculate(const std::vector<double>& equityCurve); }; }
