// 盘口五档实连诊断工具（P10 开发期）
#include "data/tdx/tdx_provider.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <cstdio>
#include <thread>

using namespace st;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TdxProvider provider;
    provider.setRequestTimeoutMs(8000);
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (!provider.isConnected()) {
        std::printf("连接失败\n");
        return 1;
    }

    const StockCode code(Market::SH, "600519");  // 贵州茅台
    auto q = provider.batchQuote({code});
    if (!q.empty()) {
        std::printf("报价 最新价 %.2f 量%.0f手 额%.0f\n", q[0].lastPrice,
                    static_cast<double>(q[0].volume) / 100.0, q[0].amount);
        std::printf("外盘 %.0f手  内盘 %.0f手  合计 %.0f手\n",
                    static_cast<double>(q[0].outerVol) / 100.0,
                    static_cast<double>(q[0].innerVol) / 100.0,
                    static_cast<double>(q[0].outerVol + q[0].innerVol) / 100.0);
    }

    auto md = provider.getMarketDepth(code);
    if (!md) {
        std::printf("无盘口数据\n");
        provider.disconnect();
        return 1;
    }
    std::printf("bestBid %.2f  bestAsk %.2f  spread %.2f\n",
                md->bestBid(), md->bestAsk(), md->spread());
    for (int k = 0; k < 5; ++k) {
        std::printf("卖%d  %.2f  %lld\n", 5 - k, md->asks[4 - k].price,
                    static_cast<long long>(md->asks[4 - k].volume));
    }
    for (int k = 0; k < 5; ++k) {
        std::printf("买%d  %.2f  %lld\n", k + 1, md->bids[k].price,
                    static_cast<long long>(md->bids[k].volume));
    }

    auto ticks = provider.getTransactions(code, 10);
    std::printf("\n成交明细（最近 %zu 条，最新在前）:\n", ticks.size());
    for (const auto& t : ticks) {
        std::printf("%s  %.2f  %.0f手  %s\n",
                    utils::toDateTimeString(t.time).substr(11, 5).c_str(),
                    t.price, static_cast<double>(t.volume) / 100.0,
                    t.direction == Direction::Sell ? "卖" : "买");
    }
    provider.disconnect();
    return 0;
}
