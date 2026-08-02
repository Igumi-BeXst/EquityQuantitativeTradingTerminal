#include "engine/backtest/performance.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <deque>
#include <map>

namespace st {

PerformanceCalculator::TradeStats
PerformanceCalculator::computeTradeStats(const std::vector<Trade>& trades) {
    TradeStats ts;
    struct Lot { Volume vol; double unitCost; };
    std::map<std::string, std::deque<Lot>> lots;  // code -> 买入开仓队列

    double grossProfit = 0.0, grossLoss = 0.0;
    double realized = 0.0;

    for (const auto& t : trades) {
        if (t.direction == Direction::Buy && t.volume > 0) {
            const double unitCost =
                (t.amount + t.totalFee) / static_cast<double>(t.volume);
            lots[t.code.fullCode()].push_back({t.volume, unitCost});
        } else if (t.direction == Direction::Sell && t.volume > 0) {
            const double sellNet =
                (t.amount - t.totalFee) / static_cast<double>(t.volume);
            auto& q = lots[t.code.fullCode()];
            double pnl = 0.0;
            Volume remaining = t.volume;
            while (remaining > 0 && !q.empty()) {
                const Volume take = std::min(remaining, q.front().vol);
                pnl += static_cast<double>(take) * (sellNet - q.front().unitCost);
                q.front().vol -= take;
                remaining -= take;
                if (q.front().vol <= 0) q.pop_front();
            }
            realized += pnl;
            ++ts.totalTrades;
            if (pnl > 1e-9) {
                ++ts.winningTrades;
                grossProfit += pnl;
            } else if (pnl < -1e-9) {
                grossLoss += -pnl;
            }
        }
    }

    ts.winRate = ts.totalTrades > 0
        ? ts.winningTrades * 100.0 / static_cast<double>(ts.totalTrades) : 0.0;
    if (grossLoss > 1e-12) {
        ts.profitFactor = grossProfit / grossLoss;
    } else {
        ts.profitFactor = grossProfit > 1e-12 ? 999.0 : 0.0;
    }
    ts.totalPnl = realized;
    return ts;
}

double PerformanceCalculator::mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double PerformanceCalculator::stdDev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double m = mean(values);
    double sum = 0.0;
    for (double v : values) {
        sum += (v - m) * (v - m);
    }
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

std::vector<double> PerformanceCalculator::computeDailyReturns(const std::vector<double>& equity) {
    std::vector<double> returns;
    if (equity.size() < 2) return returns;
    returns.reserve(equity.size() - 1);
    for (size_t i = 1; i < equity.size(); ++i) {
        if (equity[i - 1] <= 0) {
            returns.push_back(0.0);
        } else {
            returns.push_back(equity[i] / equity[i - 1] - 1.0);
        }
    }
    return returns;
}

double PerformanceCalculator::maxDrawdown(const std::vector<double>& equity) {
    if (equity.empty()) return 0.0;
    double peak = equity[0];
    double maxDD = 0.0;
    for (double v : equity) {
        if (v > peak) peak = v;
        double dd = (peak - v) / peak;
        if (dd > maxDD) maxDD = dd;
    }
    return maxDD * 100.0;  // 百分比
}

double PerformanceCalculator::annualizedReturn(const std::vector<double>& equity, int daysPerYear) {
    if (equity.size() < 2) return 0.0;
    double start = equity.front();
    double end = equity.back();
    if (start <= 0) return 0.0;
    double totalReturn = end / start - 1.0;
    double years = static_cast<double>(equity.size() - 1) / static_cast<double>(daysPerYear);
    if (years <= 0) return 0.0;
    // 年化 = (1+总收益率)^(1/年数) - 1
    if (1.0 + totalReturn <= 0) return -100.0;
    return (std::pow(1.0 + totalReturn, 1.0 / years) - 1.0) * 100.0;
}

double PerformanceCalculator::annualizedVolatility(const std::vector<double>& dailyReturns,
                                                   int daysPerYear) {
    double dailyVol = stdDev(dailyReturns);
    return dailyVol * std::sqrt(static_cast<double>(daysPerYear)) * 100.0;
}

double PerformanceCalculator::sharpe(const std::vector<double>& dailyReturns,
                                     double riskFree, int daysPerYear) {
    if (dailyReturns.empty()) return 0.0;
    double m = mean(dailyReturns);
    double sd = stdDev(dailyReturns);
    if (sd <= 0) return 0.0;
    double dailyRiskFree = riskFree / static_cast<double>(daysPerYear);
    return (m - dailyRiskFree) / sd * std::sqrt(static_cast<double>(daysPerYear));
}

double PerformanceCalculator::sortino(const std::vector<double>& dailyReturns,
                                      double riskFree, int daysPerYear) {
    if (dailyReturns.empty()) return 0.0;
    double m = mean(dailyReturns);
    double dailyRiskFree = riskFree / static_cast<double>(daysPerYear);
    double downsideSum = 0.0;
    int count = 0;
    for (double r : dailyReturns) {
        double downside = r - dailyRiskFree;
        if (downside < 0) {
            downsideSum += downside * downside;
            count++;
        }
    }
    if (count == 0) return 0.0;
    double downsideDev = std::sqrt(downsideSum / static_cast<double>(count));
    if (downsideDev <= 0) return 0.0;
    return (m - dailyRiskFree) / downsideDev * std::sqrt(static_cast<double>(daysPerYear));
}

Performance PerformanceCalculator::calculate(const std::vector<double>& equity) {
    Input input;
    input.equity = equity;
    return calculate(input);
}

Performance PerformanceCalculator::calculate(const Input& input) {
    Performance p;
    if (input.equity.empty()) return p;

    p.equityCurve = input.equity;
    p.dailyReturns = computeDailyReturns(input.equity);

    // 核心指标
    p.totalReturn = (input.equity.back() / input.equity.front() - 1.0) * 100.0;
    p.annualReturn = annualizedReturn(input.equity, input.tradingDaysPerYear);
    p.maxDrawdown = maxDrawdown(input.equity);
    p.volatility = annualizedVolatility(p.dailyReturns, input.tradingDaysPerYear);
    p.sharpeRatio = sharpe(p.dailyReturns, input.riskFreeRate, input.tradingDaysPerYear);
    p.sortinoRatio = sortino(p.dailyReturns, input.riskFreeRate, input.tradingDaysPerYear);
    if (p.maxDrawdown > 0) {
        p.calmarRatio = p.annualReturn / p.maxDrawdown;
    }

    // Alpha/Beta (相对基准)
    if (!input.benchmarkEquity.empty() &&
        input.benchmarkEquity.size() == input.equity.size()) {
        auto benchReturns = computeDailyReturns(input.benchmarkEquity);
        if (benchReturns.size() == p.dailyReturns.size() && !benchReturns.empty()) {
            double cov = 0.0;
            double mS = mean(p.dailyReturns);
            double mB = mean(benchReturns);
            for (size_t i = 0; i < p.dailyReturns.size(); ++i) {
                cov += (p.dailyReturns[i] - mS) * (benchReturns[i] - mB);
            }
            cov /= static_cast<double>(p.dailyReturns.size());
            double varB = 0.0;
            for (double b : benchReturns) {
                varB += (b - mB) * (b - mB);
            }
            varB /= static_cast<double>(benchReturns.size());
            if (varB > 0) {
                p.beta = cov / varB;
            }
            // Alpha = 组合年化 - (无风险 + beta*(基准年化 - 无风险))
            double benchAnnual = annualizedReturn(input.benchmarkEquity, input.tradingDaysPerYear);
            p.alpha = p.annualReturn - (input.riskFreeRate * 100.0 +
                      p.beta * (benchAnnual - input.riskFreeRate * 100.0));
        }
    }

    return p;
}

} // namespace st
