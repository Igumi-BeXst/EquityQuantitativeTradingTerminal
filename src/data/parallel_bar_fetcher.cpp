#include "data/parallel_bar_fetcher.h"
#include "data/bar_disk_cache.h"
#include "data/data_cache.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_provider.h"
#include "core/app_paths.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace st {

namespace {

bool supportsTdxParallel(IDataProvider* primary) {
    if (!primary) return false;
    // MultiProvider 的 providerName() 形如 "multi(tdx→tencent)"，TDX 直连为 "tdx"。
    return primary->providerName().find("tdx") != std::string::npos;
}

void filterRawBars(const std::vector<Bar>& bars,
                   DateTime filterStart,
                   DateTime end,
                   std::vector<Bar>& out) {
    out.clear();
    out.reserve(bars.size());
    for (const auto& b : bars) {
        if (b.time >= filterStart && b.time <= end) out.push_back(b);
    }
}

std::vector<Bar> sliceBars(const std::vector<Bar>& bars,
                           DateTime start,
                           DateTime end) {
    std::vector<Bar> out;
    out.reserve(bars.size());
    for (const auto& b : bars) {
        if (b.time >= start && b.time <= end) out.push_back(b);
    }
    return out;
}

/// 判断磁盘缓存是否覆盖本次请求范围。
/// 允许最多 3 个自然日的边界误差（应对周末，避免长期使用陈旧缓存）。
bool diskCacheCovers(const std::vector<Bar>& all,
                     DateTime fetchStart,
                     DateTime end) {
    if (all.empty()) return false;
    const auto now = utils::now();
    const auto targetEnd = std::min(end, now);
    const auto tolerance = std::chrono::hours(24 * 3);
    const bool coversStart = all.front().time <= fetchStart + tolerance;
    const bool coversEnd = (targetEnd - all.back().time) <= tolerance;
    return coversStart && coversEnd;
}

} // namespace

void ParallelBarFetcher::fetchRawBars(
    IDataProvider* primary,
    const std::vector<StockCode>& codes,
    BarPeriod period,
    DateTime fetchStart,
    DateTime filterStart,
    DateTime end,
    const std::shared_ptr<DataCache>& cache,
    std::map<std::string, std::vector<Bar>>& outRawBars,
    int lanes,
    const std::function<void(int done, int total)>& onProgress)
{
    outRawBars.clear();
    if (codes.empty()) return;

    const int total = static_cast<int>(codes.size());
    const int step = std::max(1, total / 50);
    const bool useDisk = supportsTdxParallel(primary);
    BarDiskCache disk(useDisk ? (AppPaths::dataDir() + "/bar_cache") : std::string{});
    std::mutex diskMutex;

    auto report = [&](int done) {
        if (!onProgress) return;
        if (done == total || done % step == 0) {
            onProgress(done, total);
        }
    };

    // 把已拉取/缓存命中的 bars 写入内存缓存 + 收集真实价
    auto commitBars = [&](const StockCode& code, const std::vector<Bar>& bars) {
        if (bars.empty()) return;
        std::vector<Bar> filtered;
        filterRawBars(bars, filterStart, end, filtered);
        cache->cacheBars(code, period, bars);
        if (!filtered.empty()) outRawBars[code.fullCode()] = std::move(filtered);
    };

    if (!supportsTdxParallel(primary) || lanes <= 1) {
        // 非 TDX 或未启用并行：直接用主数据源串行拉取（不写磁盘缓存，避免跨数据源混用）
        int done = 0;
        for (const auto& code : codes) {
            auto bars = primary ? primary->getRawBars(code, period, fetchStart, end)
                                : std::vector<Bar>{};
            commitBars(code, bars);
            ++done;
            report(done);
        }
        return;
    }

    // TDX 多连接并行 + 磁盘缓存：每个 lane 创建独立 TdxProvider
    const int n = std::min<int>(std::max(1, lanes), total);
    std::atomic<int> done{0};
    std::mutex rawMutex;
    std::vector<std::thread> threads;
    threads.reserve(n);

    auto fetchOne = [&](const StockCode& code, TdxProvider* provider) {
        // 1. 先尝试磁盘缓存
        if (useDisk) {
            std::vector<Bar> all;
            {
                std::lock_guard<std::mutex> lk(diskMutex);
                all = disk.loadAll(code, period);
            }
            if (diskCacheCovers(all, fetchStart, end)) {
                std::vector<Bar> needed = sliceBars(all, fetchStart, end);
                if (!needed.empty()) {
                    std::lock_guard<std::mutex> lk(rawMutex);
                    commitBars(code, needed);
                }
                return;
            }
        }

        // 2. 网络拉取
        auto bars = provider->getRawBars(code, period, fetchStart, end);
        if (bars.empty() && primary) {
            bars = primary->getRawBars(code, period, fetchStart, end);
        }
        if (!bars.empty() && useDisk) {
            std::lock_guard<std::mutex> lk(diskMutex);
            disk.save(code, period, bars);
        }
        {
            std::lock_guard<std::mutex> lk(rawMutex);
            commitBars(code, bars);
        }
    };

    for (int lane = 0; lane < n; ++lane) {
        threads.emplace_back([&, lane] {
            auto provider = std::make_unique<TdxProvider>();
            for (int i = lane; i < total; i += n) {
                const auto& code = codes[i];
                fetchOne(code, provider.get());
                const int d = ++done;
                report(d);
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
}

} // namespace st
