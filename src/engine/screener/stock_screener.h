#pragma once

#include "engine/screener/screener_types.h"
#include "engine/screener/factor.h"
#include "engine/screener/condition_filter.h"
#include "data/data_cache.h"
#include "foundation/bar.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace st {

/// 选股引擎配置
struct ScreenerConfig {
    BarPeriod period = BarPeriod::Daily;
    DateTime endDate;         // 选股截止日期
    int lookbackDays = 250;   // 回看天数
    int topN = 50;            // 输出前 N 名
};

/// 多因子选股引擎（截面标准化打分）
///
/// 流程:
///   1. 输入股票池 + 因子集 + 权重
///   2. 对每只股票加载日线 → 计算各因子原始值
///   3. 对每个因子做全池截面标准化：MAD 去极值 → z-score → 方向符号 →
///      Φ(z) 映射到 0~100（缺失值给中性 50 分）
///   4. 条件筛选（作用于标准化后分数）
///   5. 加权排名 → 输出前 N
///
/// 截面标准化使 Alpha191 / 量价 / 基本面等不同量纲的因子可以直接加权合成，
/// 也避免逐因子手写 0~100 经验映射。
class StockScreener {
public:
    StockScreener();
    ~StockScreener();

    void setConfig(const ScreenerConfig& config);
    void setDataCache(DataCache* cache) { cache_ = cache; }

    /// 添加因子及权重
    void addFactor(std::shared_ptr<IFactor> factor, double weight);

    /// 注入基本面快照（fullCode → QuoteFundamentals；估值/规模因子用）
    /// 未注入或某只缺失时，估值因子降级为缺失（中性 50 分），不阻塞选股
    void setQuoteFundamentals(std::unordered_map<std::string, QuoteFundamentals> quotes) {
        quotes_ = std::move(quotes);
    }

    /// 注入基准指数日线（Alpha075/149/181/182 等基准类因子用；缺失时这些因子降级）
    void setBenchmarkBars(std::vector<Bar> bars) { benchmarkBars_ = std::move(bars); }

    /// 设置筛选条件
    void addCondition(const Condition& condition);

    /// 执行选股
    std::vector<ScreenResult> run(const std::vector<StockCode>& pool);

    /// 进度回调
    void setProgressCallback(std::function<void(double)> cb) { progressCb_ = std::move(cb); }

    const std::vector<std::pair<std::string, double>>& weights() const { return weights_; }

private:
    /// 用已过滤的日线与基本面快照计算逐股时间序列因子的原始值
    ScreenResult evaluate(const StockCode& code,
                          const std::vector<Bar>& bars,
                          const QuoteFundamentals* quote);

    ScreenerConfig config_;
    DataCache* cache_ = nullptr;
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> factors_;
    std::vector<std::pair<std::string, double>> weights_;
    std::unordered_map<std::string, QuoteFundamentals> quotes_;  // fullCode → 快照
    std::vector<Bar> benchmarkBars_;                             // 基准指数日线
    ConditionFilter filter_;
    std::function<void(double)> progressCb_;
};

} // namespace st
