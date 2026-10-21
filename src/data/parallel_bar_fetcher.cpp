#include "data/parallel_bar_fetcher.h"
#include "data/cached_data_provider.h"
#include "data/data_cache.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_provider.h"
#include "core/app_paths.h"
#include "core/log_manager.h"
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
    // MultiProvider 的 providerName() 以 "multi(tdx→" 开头，TDX 直连为 "tdx"。
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
        // 非 TDX 或未启用并行：主数据源的缓存装饰器负责 SQLite 读写。
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

    // TDX 多连接并行：每个 lane 有独立 provider 和线程内 SQLite 连接。
    const int n = std::min<int>(std::max(1, lanes), total);
    std::atomic<int> done{0};
    std::mutex rawMutex;
    std::vector<std::thread> threads;
    threads.reserve(n);

    auto fetchOne = [&](const StockCode& code, IDataProvider* provider) {
        auto bars = provider ? provider->getRawBars(code, period, fetchStart, end)
                             : std::vector<Bar>{};
        if (bars.empty() && primary) {
            bars = primary->getRawBars(code, period, fetchStart, end);
        }
        {
            std::lock_guard<std::mutex> lk(rawMutex);
            commitBars(code, bars);
        }
    };

    for (int lane = 0; lane < n; ++lane) {
        threads.emplace_back([&, lane] {
            // 数据目录不可用时传空路径，让缓存层自行降级（不写盘但仍可拉数据）
            const std::string dataDir = AppPaths::dataDir();
            const std::string dbPath =
                dataDir.empty() ? std::string() : dataDir + "/market_data.sqlite";
            auto provider = std::make_unique<CachedDataProvider>(
                std::make_unique<TdxProvider>(), "tdx", dbPath);
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
