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

/// 多因子选股引擎
///
/// 流程:
///   1. 输入股票池 + 因子集 + 权重
///   2. 对每只股票加载日线 → 计算各因子
///   3. 条件筛选
///   4. 加权排名 → 输出前 N
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

    /// 设置筛选条件
    void addCondition(const Condition& condition);

    /// 执行选股
    std::vector<ScreenResult> run(const std::vector<StockCode>& pool);

    /// 进度回调
    void setProgressCallback(std::function<void(double)> cb) { progressCb_ = std::move(cb); }

    const std::vector<std::pair<std::string, double>>& weights() const { return weights_; }

private:
    ScreenResult evaluate(const StockCode& code);

    ScreenerConfig config_;
    DataCache* cache_ = nullptr;
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> factors_;
    std::vector<std::pair<std::string, double>> weights_;
    std::unordered_map<std::string, QuoteFundamentals> quotes_;  // fullCode → 快照
    ConditionFilter filter_;
    std::function<void(double)> progressCb_;
};

} // namespace st
