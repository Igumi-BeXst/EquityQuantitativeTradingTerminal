// 盘口/内外盘实连诊断工具（P10 开发期）
// 用法: depth_probe [代码6位]  默认 600519
#include "data/tdx/tdx_provider.h"
#include <QCoreApplication>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace st;

namespace {

Market inferMarket(const char* c) {
    if (c[0] == '6') return Market::SH;
    if (c[0] == '0' || c[0] == '3') return Market::SZ;
    return Market::BJ;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const char* codeStr = (argc > 1) ? argv[1] : "600519";
    TdxProvider provider;
    provider.setRequestTimeoutMs(8000);
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (!provider.isConnected()) {
        std::printf("连接失败\n");
        return 1;
    }

    const StockCode code(inferMarket(codeStr), codeStr);
    std::printf("== %s %s ==\n", code.displayCode().c_str(), codeStr);

    auto q = provider.batchQuote({code});
    if (q.empty()) {
        std::printf("无报价\n");
        provider.disconnect();
        return 1;
    }
    const double total = static_cast<double>(q[0].volume) / 100.0;
    const double o = static_cast<double>(q[0].outerVol) / 100.0;
    const double in = static_cast<double>(q[0].innerVol) / 100.0;
    std::printf("报价 最新价 %.2f 量%.0f手 额%.0f\n", q[0].lastPrice, total, q[0].amount);
    std::printf("s_vol/b_vol → 外盘 %.2f万手  内盘 %.2f万手  合计 %.2f万手 (量=%.2f万手)\n",
                o / 10000.0, in / 10000.0, (o + in) / 10000.0, total / 10000.0);
    std::printf("外/内差占总量 %.2f%%\n",
                std::abs(o - in) * 100.0 / total);

    auto md = provider.getMarketDepth(code);
    if (md) {
        std::printf("盘口 bestBid %.2f  bestAsk %.2f\n", md->bestBid(), md->bestAsk());
        for (int k = 0; k < 5; ++k) {
            std::printf("  卖%d %.2f %lld   买%d %.2f %lld\n", 5 - k,
                        md->asks[4 - k].price,
                        static_cast<long long>(md->asks[4 - k].volume),
                        k + 1, md->bids[k].price,
                        static_cast<long long>(md->bids[k].volume));
        }
    } else {
        std::printf("无盘口数据\n");
    }

    // 逐笔成交明细：按方向求和，判定真实内/外盘
    auto ticks = provider.getDayTransactions(code);
    double buyShares = 0.0, sellShares = 0.0;
    for (const auto& t : ticks) {
        if (t.direction == Direction::Buy) buyShares += static_cast<double>(t.volume);
        else sellShares += static_cast<double>(t.volume);
    }
    std::printf("\n全天逐笔 %zu 条: 主动买 %.2f万手  主动卖 %.2f万手  合计 %.2f万手\n",
                ticks.size(), buyShares / 100.0 / 10000.0, sellShares / 100.0 / 10000.0,
                (buyShares + sellShares) / 100.0 / 10000.0);
    std::printf("对照A(外盘≈主动买): 外盘-主动买 %+.0f手\n",
                (o * 100.0 - buyShares) / 100.0);
    std::printf("对照B(外盘≈主动卖): 外盘-主动卖 %+.0f手\n",
                (o * 100.0 - sellShares) / 100.0);
    const char* verdict = (std::abs(o * 100.0 - buyShares) < std::abs(o * 100.0 - sellShares))
        ? "外盘=主动买 ✓" : "外盘=主动卖 ✗";
    std::printf("判定: %s\n", verdict);

    provider.disconnect();
    return 0;
}
