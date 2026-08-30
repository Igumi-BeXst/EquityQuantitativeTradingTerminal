#pragma once

#include "foundation/stock_code.h"
#include "foundation/bar.h"
#include "foundation/types.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace st {

class IDataProvider;
class DataCache;

/// 批量预取不复权日线（并行多连接，针对 TDX 单连接串行瓶颈）。
///
/// - 将 [fetchStart, end] 区间的 raw bars 写入 cache（供回测 warm-up 使用）
/// - 同时将 [filterStart, end] 子集收集到 outRawBars（供成交/盯市真实价使用）
/// - 优先为 TDX 创建多个独立连接并行下载；其他数据源降级为单连接串行
/// - onProgress 可能从 worker 线程调用，调用方需自行 marshal 到 UI 线程
///
/// 注意：本函数会阻塞当前线程；调用方通常应在 ThreadPool::submitIO 的任务中调用。
class ParallelBarFetcher {
public:
    /// 并行拉取不复权 K 线并写入内存缓存。
    /// @param primary      当前主数据源
    /// @param codes        要拉取的股票池
    /// @param period       周期（当前用途为 Daily）
    /// @param fetchStart   数据拉取起点（含 warm-up，早于回测起点）
    /// @param filterStart  rawBars 收集起点（通常等于回测起点）
    /// @param end          拉取/收集终点
    /// @param cache        回测用内存缓存（线程安全）
    /// @param outRawBars   输出：code.fullCode() -> 真实价 bar 序列
    /// @param lanes        并行连接数（TDX 生效；<=1 时串行）
    /// @param onProgress   (done, total) 进度回调；调用方负责 marshal
    static void fetchRawBars(
        IDataProvider* primary,
        const std::vector<StockCode>& codes,
        BarPeriod period,
        DateTime fetchStart,
        DateTime filterStart,
        DateTime end,
        const std::shared_ptr<DataCache>& cache,
        std::map<std::string, std::vector<Bar>>& outRawBars,
        int lanes,
        const std::function<void(int done, int total)>& onProgress);
};

} // namespace st
