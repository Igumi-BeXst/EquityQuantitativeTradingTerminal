// 东财基本面实连校准工具（P10 第二轮开发用）
// 用法: fundamental_calib [代码]  默认 600519
// 验证路径：与 widget 一致——从非主线程（IO 池）调用 thread_local QNAM
#include "data/akshare_provider.h"
#include <QCoreApplication>
#include <cstdio>
#include <optional>
#include <thread>

using namespace st;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const char* codeStr = (argc > 1) ? argv[1] : "600519";
    const StockCode code(codeStr);

    // 从工作线程调用（模拟 ThreadPool::submitIO 的 IO 线程路径）
    std::optional<QuoteFundamentals> f;
    std::thread worker([&] {
        AKShareProvider provider;
        provider.connect();
        f = provider.getQuoteFundamentals(code);
    });
    worker.join();

    if (!f) {
        std::printf("%s 无基本面数据（东财拉取失败或未上市）\n", code.displayCode().c_str());
        return 1;
    }
    std::printf("%s 基本面:\n", code.displayCode().c_str());
    std::printf("  换手率 %.2f%%  换手率(实) %.2f%%\n", f->turnoverRate, f->turnoverRateReal);
    std::printf("  市盈(静) %.2f  市盈(TTM) %.2f\n", f->peStatic, f->peTtm);
    std::printf("  总市值 %.2f亿  流通值 %.2f亿\n", f->marketCap / 1e8, f->floatCap / 1e8);
    std::printf("  总股本 %.2f亿股  流通股 %.2f亿股\n", f->totalShares / 1e8, f->floatShares / 1e8);
    return 0;
}
