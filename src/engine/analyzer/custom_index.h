#pragma once

#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "foundation/tick.h"
#include "foundation/types.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace st {

/// 自定义指数成分股（权重归一化到 1；全 0 = 等权）
struct IndexConstituent {
    StockCode code;
    std::string name;
    double weight = 0.0;
};

/// 自定义指数定义（用户自建股票篮子）
struct CustomIndex {
    std::string id;
    std::string name;
    double baseValue = 1000.0;   // 基点
    std::optional<DateTime> baseDate;  // 空 = 自动取成分股首个共同数据日
    std::vector<IndexConstituent> constituents;
};

/// 归一化权重：全部 ≤0 → 等权（1/N）；否则正权重等比缩放到 Σ=1（负权重清零）
void normalizeWeights(std::vector<IndexConstituent>& constituents);

using BarsFetcher     = std::function<std::vector<Bar>(const StockCode&, BarPeriod)>;
using IntradayFetcher = std::function<IntradayData(const StockCode&)>;

/// 历史指数 K 线（价格加权 + 基点重定基）
///
/// 从成分股 DAILY 前复权序列按日期对齐计算日线指数，再聚合到请求周期（周/月）：
///   指数(t) = 基点 × Σ[wᵢ·Pᵢ(t)] / Σ[wᵢ·Pᵢ(T₀)]
/// 缺失日（停牌）按上一已知收盘 carry-forward；历史不足基准日的成分股被剔除。
std::vector<Bar> computeIndexBars(const CustomIndex& idx, const BarsFetcher& fetch,
                                  BarPeriod period);

/// 分时指数（从指数昨收做加权涨跌幅外推，分钟对齐 carry-forward）：
///   分时(t) = 指数昨收 × (1 + Σ wᵢ·(Pᵢ(t)/昨收ᵢ − 1))
IntradayData computeIndexIntraday(const CustomIndex& idx, double indexPrevClose,
                                  const IntradayFetcher& fetch);

/// 实时点位 = 指数昨收 × (1 + Σ wᵢ·涨跌幅ᵢ/100)；缺报价成分股涨跌幅按 0
double computeIndexLive(double indexPrevClose, const CustomIndex& idx,
                        const std::vector<Quote>& quotes);

/// 最后一个已完成交易日收盘（now 当日不参与；无昨日则回退最后收盘）
double lastCompletedClose(const std::vector<Bar>& dailyIndexBars, const DateTime& now);

} // namespace st
