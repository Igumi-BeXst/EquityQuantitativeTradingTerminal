#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include "foundation/types.h"
#include <string>
#include <vector>

namespace st {

/// 旧版不复权 K 线磁盘缓存；新缓存只用它从 `.stb` 导入数据到 SQLite。
///
/// 存储位置：<root>/raw/<market><code>.<period>.stb
/// 二进制格式：magic + version + period + count + 每根 bar 的裸字段。
/// 文件保留在原位置，不再写入新的 `.stb` 数据。
class BarDiskCache {
public:
    explicit BarDiskCache(std::string rootDir);

    /// 从磁盘加载某只股票指定周期 [start,end] 的数据；未命中/损坏返回空。
    std::vector<Bar> load(const StockCode& code, BarPeriod period,
                          DateTime start, DateTime end) const;

    /// 从磁盘加载某只股票指定周期的全部缓存数据（不做区间过滤，用于覆盖判断）。
    std::vector<Bar> loadAll(const StockCode& code, BarPeriod period) const;

    /// 保存某只股票的一组指定周期数据（会覆盖同名缓存文件）。
    void save(const StockCode& code, BarPeriod period,
              const std::vector<Bar>& bars) const;

    /// 缓存文件路径（供诊断/测试）。
    std::string filePath(const StockCode& code, BarPeriod period) const;

private:
    std::string rootDir_;
};

} // namespace st
