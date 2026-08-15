// 全市场股票列表拉取耗时探针（P10 第三十轮验证用）
// 用法: stock_list_probe   → 连接 TDX，拉 SH+SZ 全量列表，打印数量与耗时
#include "data/tdx/tdx_provider.h"
#include "core/log_manager.h"
#include "foundation/enums.h"
#include <chrono>
#include <cstdio>

using namespace st;

int main() {
    LogManager::instance()->init("logs/stock_list_probe.log");
    TdxProvider tdx;
    if (!tdx.connect()) {
        std::printf("TDX 连接失败\n");
        return 1;
    }
    // connect 是异步的，等待就绪
    for (int i = 0; i < 50 && !tdx.isConnected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!tdx.isConnected()) {
        std::printf("TDX 未就绪\n");
        return 1;
    }
    std::printf("TDX 已连接\n");

    auto t0 = std::chrono::steady_clock::now();
    auto sh = tdx.getStockList(Market::SH);
    auto t1 = std::chrono::steady_clock::now();
    auto sz = tdx.getStockList(Market::SZ);
    auto t2 = std::chrono::steady_clock::now();

    auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };

    int tradableSh = 0, tradableSz = 0;
    for (const auto& s : sh) if (tdx::isTradableAShare(s.code)) ++tradableSh;
    for (const auto& s : sz) if (tdx::isTradableAShare(s.code)) ++tradableSz;

    std::printf("SH 原始 %zu 只（可交易 %d）耗时 %lld ms\n",
                sh.size(), tradableSh, ms(t0, t1));
    std::printf("SZ 原始 %zu 只（可交易 %d）耗时 %lld ms\n",
                sz.size(), tradableSz, ms(t1, t2));
    std::printf("合计可交易 A 股: %d 只\n", tradableSh + tradableSz);
    return 0;
}
