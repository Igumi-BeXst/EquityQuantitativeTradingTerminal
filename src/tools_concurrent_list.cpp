// 连接未就绪时混合并发复现（getStockList + getBars，模拟 QuantWindow 打开）
#include "data/provider_factory.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include "core/thread_pool.h"
#include "foundation/enums.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>

using namespace st;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    auto provider = makeDataProvider();
    provider->connect();   // 异步：不等待

    std::atomic<int> done{0};
    IDataProvider* p = provider.get();
    const auto start = utils::parseDate("2023-01-01");
    const auto end = utils::now();
    StockCode moutai(Market::SH, "600519");
    // 混合负载：3 任务拉列表 + 7 任务拉 K 线（模拟 QuantWindow 各面板）
    for (int t = 0; t < 3; ++t) {
        ThreadPool::submitIO([p, &done] {
            auto sh = p->getStockList(Market::SH);
            auto sz = p->getStockList(Market::SZ);
            std::printf("  list: SH %zu SZ %zu\n", sh.size(), sz.size());
            done.fetch_add(1);
        });
    }
    for (int t = 0; t < 7; ++t) {
        ThreadPool::submitIO([p, &done, moutai, start, end] {
            auto bars = p->getBars(moutai, BarPeriod::Daily, start, end);
            std::printf("  bars: %zu\n", bars.size());
            done.fetch_add(1);
        });
    }
    while (done.load() < 10) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("all done\n");
    ThreadPool::ioPool()->waitForDone();
    std::printf("drained\n");
    return 0;
}
