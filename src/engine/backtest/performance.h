#pragma once

#include "foundation/types.h"
#include "foundation/order.h"
#include <vector>
#include <string>

namespace st {

/// 绩效指标汇总
struct Performance {
    // 核心指标
    double totalReturn   = 0.0;  // 总收益率 (%)
    double annualReturn  = 0.0;  // 年化收益率 (%)
    double maxDrawdown   = 0.0;  // 最大回撤 (%)
    double sharpeRatio   = 0.0;  // 夏普比率
    double winRate       = 0.0;  // 胜率 (%)
    double profitFactor  = 0.0;  // 盈亏比

    // 进阶指标
    double calmarRatio   = 0.0;  // 卡玛比率 (年化/最大回撤)
    double volatility    = 0.0;  // 年化波动率 (%)
    double sortinoRatio  = 0.0;  // 索提诺比率
    double alpha         = 0.0;  // Alpha
    double beta          = 0.0;  // Beta
    double informationRatio = 0.0; // 信息比率

    // 交易统计
    int    totalTrades   = 0;    // 总交易次数
    int    winningTrades = 0;    // 盈利交易次数
    double totalPnl      = 0.0;  // 总盈亏 (绝对金额)

    // 明细
    std::vector<double> dailyReturns;    // 日收益率序列
    std::vector<double> equityCurve;     // 净值曲线
};

/// 绩效计算器 — 基于净值曲线计算各指标
class PerformanceCalculator {
public:
    /// 参数: 净值曲线 (初始=1.0)，年化天数，基准收益率序列(可选)
    struct Input {
        std::vector<double> equity;             // 净值序列
        double riskFreeRate = 0.02;             // 无风险利率 (年化 2%)
        std::vector<double> benchmarkEquity;    // 基准净值(可选，算Alpha/Beta)
        int tradingDaysPerYear = 252;           // 年化天数
    };

    /// 从净值曲线计算指标
    static Performance calculate(const Input& input);

    /// 便捷重载: 只给净值曲线
    static Performance calculate(const std::vector<double>& equity);

    /// 交易统计（从成交记录派生）
    struct TradeStats {
        int    totalTrades   = 0;   // 平仓（卖出）笔数
        int    winningTrades = 0;   // 盈利平仓笔数
        double winRate       = 0.0; // 胜率 (%) = winning/total*100
        double profitFactor  = 0.0; // 盈亏比 = 总盈利/总亏损
        double totalPnl      = 0.0; // 已实现盈亏（FIFO 逐股配对）
    };

    /// FIFO 逐股配对计算交易统计（buy 净成本=amount+totalFee，sell 净回款=amount-totalFee）
    static TradeStats computeTradeStats(const std::vector<Trade>& trades);

private:
    static std::vector<double> computeDailyReturns(const std::vector<double>& equity);
    static double maxDrawdown(const std::vector<double>& equity);
    static double annualizedReturn(const std::vector<double>& equity, int daysPerYear);
    static double annualizedVolatility(const std::vector<double>& dailyReturns, int daysPerYear);
    static double sharpe(const std::vector<double>& dailyReturns, double riskFree, int daysPerYear);
    static double sortino(const std::vector<double>& dailyReturns, double riskFree, int daysPerYear);
    static double stdDev(const std::vector<double>& values);
    static double mean(const std::vector<double>& values);
};

} // namespace st
