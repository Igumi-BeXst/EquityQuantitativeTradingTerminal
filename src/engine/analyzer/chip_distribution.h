#pragma once

#include "foundation/bar.h"
#include "foundation/tick.h"
#include "foundation/types.h"
#include <string>
#include <vector>

namespace st {

/// 筹码分布单点：价位中心 + 该价位筹码量（股）
struct ChipPoint {
    double price = 0.0;
    double shares = 0.0;
};

/// 筹码分布结果
struct ChipDistResult {
    bool success = false;
    std::string error;
    std::vector<ChipPoint> points;  // 按 price 升序（非零筹码桶）
    double avgCost = 0.0;           // 平均成本（筹码加权均价）
    double profitRatio = 0.0;       // 获利盘比例 0..1（price <= 现价的筹码占比）
    double costLow = 0.0;           // 90% 成本区间下沿（筹码加权 P5）
    double costHigh = 0.0;          // 90% 成本区间上沿（筹码加权 P95）
    double costLow70 = 0.0;         // 70% 成本区间下沿（筹码加权 P15）
    double costHigh70 = 0.0;        // 70% 成本区间上沿（筹码加权 P85）
    double concentration = 0.0;     // 集中度 = (P95-P5)/(P95+P5)，越小越集中
    double totalChips = 0.0;        // 总筹码量（股）
    double minPrice = 0.0;
    double maxPrice = 0.0;
};

/// 筹码分布 — 经典「三角分布 + 换手率衰减」模型
///
/// 逐日把当日成交量按三角形分布摊到 [low, high] 价位桶（峰值在 typical 价），
/// 每日先按当日换手率（volume/floatShares）衰减既有筹码再累加新筹码。
/// floatShares<=0 时为纯量模式（无衰减，总量不归一化，供 UI 相对显示）。
class ChipDistribution {
public:
    static ChipDistResult compute(const std::vector<Bar>& bars, double floatShares);
    static constexpr int kDefaultBins = 200;
};

/// 价格×成交量分布单点
struct PriceVolumePoint {
    double price = 0.0;
    Volume volume = 0;
};

/// 当日成交分布结果
struct TransactionDist {
    bool success = false;
    std::vector<PriceVolumePoint> points;  // 按 price 升序（非零桶）
    Volume totalVolume = 0;
};

/// 当日成交分布 — 分时/逐笔聚合为价格直方图
class TransactionDistribution {
public:
    /// 从当日分时数据聚合（points.volume 为累计量，差分得每分成交量）
    static TransactionDist fromIntraday(const IntradayData& data, int bins = 60);
    /// 从逐笔成交聚合
    static TransactionDist fromTicks(const std::vector<Tick>& ticks, int bins = 60);
};

/// 区间统计结果（给定 bar 窗口）
struct RangeStatResult {
    bool success = false;
    std::string error;
    int barCount = 0;
    double startPrice = 0.0;     // 首根开盘价
    double endPrice = 0.0;       // 末根收盘价
    double changePct = 0.0;      // 区间涨跌幅 %（(末close-首open)/首open×100）
    double high = 0.0;           // 区间最高
    double low = 0.0;            // 区间最低
    double amplitudePct = 0.0;   // 振幅 %（(high-low)/首open×100）
    double totalVolume = 0.0;    // 总成交量（股）
    double totalAmount = 0.0;    // 总成交额（元）
    double avgPrice = 0.0;       // 区间均价（额/量）
    double turnoverPct = 0.0;    // 区间换手率 %（需 floatShares>0）
};

/// 区间统计
class RangeStats {
public:
    static RangeStatResult compute(const std::vector<Bar>& bars, double floatShares = 0.0);
};

} // namespace st
