#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include "foundation/types.h"
#include <string>
#include <vector>

namespace st {

/// 不复权日线磁盘缓存（方案 B 轻量实现）。
///
/// 存储位置：<root>/raw/<market><code>.daily.stb
/// 二进制格式：magic + version + period + count + 每根 bar 的裸字段。
/// 目前只用于全市场回测的 warm-up 原始价数据，避免每次回测重复下载同一段历史。
class BarDiskCache {
public:
    explicit BarDiskCache(std::string rootDir);

    /// 从磁盘加载某只股票 [start,end] 的日线；未命中/损坏返回空。
    std::vector<Bar> load(const StockCode& code, BarPeriod period,
                          DateTime start, DateTime end) const;

    /// 从磁盘加载某只股票的全部缓存日线（不做区间过滤，用于覆盖判断）。
    std::vector<Bar> loadAll(const StockCode& code, BarPeriod period) const;

    /// 保存某只股票的一组日线（会覆盖同名缓存文件）。
    void save(const StockCode& code, BarPeriod period,
              const std::vector<Bar>& bars) const;

    /// 缓存文件路径（供诊断/测试）。
    std::string filePath(const StockCode& code, BarPeriod period) const;

private:
    std::string rootDir_;
};

} // namespace st
