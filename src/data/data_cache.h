#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include <unordered_map>
#include <mutex>
#include <memory>

namespace st {

/// 内存缓存 — 回测时把目标股票的时间段数据整体加载到内存
/// 提供高速随机访问，回测循环不触碰磁盘
class DataCache {
public:
    DataCache() = default;

    /// 缓存一序列 bars (按 code+period 分组)
    void cacheBars(const StockCode& code, BarPeriod period, std::vector<Bar> bars);

    /// 获取缓存的 bar 序列（返回 nullptr 表示未缓存）
    const BarSeries* get(const StockCode& code, BarPeriod period) const;

    /// 获取缓存的 bar 原始向量
    std::vector<Bar> getBars(const StockCode& code, BarPeriod period) const;

    /// 是否已缓存
    bool has(const StockCode& code, BarPeriod period) const;

    /// 清空缓存
    void clear();

    /// 缓存条目数量
    size_t size() const;

private:
    struct Key {
        StockCode code;
        BarPeriod period;
        bool operator==(const Key& o) const { return code == o.code && period == o.period; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<std::string>()(k.code.fullCode()) ^
                   (static_cast<size_t>(k.period) << 16);
        }
    };

    mutable std::mutex mutex_;
    std::unordered_map<Key, std::shared_ptr<BarSeries>, KeyHash> cache_;
};

} // namespace st
