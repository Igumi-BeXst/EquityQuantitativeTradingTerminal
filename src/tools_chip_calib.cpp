// 筹码分布实连校准工具（P10 第三轮开发用）
// 用法: chip_calib [代码]  默认 600519
// 验证路径：TDX 拉日K/分时 + 东财流通股本 → 引擎计算，打印筹码/成交/区间统计。
// 实连核对：茅台平均成本应在近期价格区间附近、获利盘随现价变化、换手率衰减收敛。
#include "data/tdx/tdx_provider.h"
#include "data/akshare_provider.h"
#include "engine/analyzer/chip_distribution.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <cstdio>
#include <thread>

using namespace st;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const char* codeStr = (argc > 1) ? argv[1] : "600519";
    const StockCode code(codeStr);

    // 1. 东财流通股本（从工作线程调用，模拟 IO 池路径；失败降级纯量模式）
    double floatShares = 0.0;
    std::thread worker([&] {
        AKShareProvider fp;
        fp.connect();
        auto f = fp.getQuoteFundamentals(code);
        if (f && f->valid && f->floatShares > 0.0) floatShares = f->floatShares;
    });
    worker.join();
    std::printf("%s 流通股本 %.2f亿股 %s\n", code.displayCode().c_str(),
                floatShares / 1e8,
                floatShares > 0.0 ? "(东财)" : "(无 → 纯量模式)");

    // 2. TDX 日K + 分时
    TdxProvider provider;
    provider.setRequestTimeoutMs(8000);
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (!provider.isConnected()) {
        std::printf("TDX 连接失败\n");
        return 1;
    }

    auto bars = provider.getBars(code, BarPeriod::Daily,
                                 utils::parseDate("2015-01-01"), utils::now());
    std::printf("日K bars=%zu 最早=%s 最新=%s\n", bars.size(),
                bars.empty() ? "-" : utils::toDateTimeString(bars.front().time).c_str(),
                bars.empty() ? "-" : utils::toDateTimeString(bars.back().time).c_str());
    if (bars.empty()) return 1;

    // 3. 筹码分布（全部 / 近 250 日）+ 区间统计
    for (int days : {0, 250}) {
        std::vector<Bar> win;
        if (days <= 0 || static_cast<size_t>(days) >= bars.size()) {
            win = bars;
        } else {
            win.assign(bars.end() - days, bars.end());
        }
        auto r = ChipDistribution::compute(win, floatShares);
        std::printf("筹码(%s %d根): %s\n", days == 0 ? "全部" : "近250日",
                    static_cast<int>(win.size()), r.success ? "ok" : r.error.c_str());
        if (r.success) {
            std::printf("  平均成本 %.2f  获利盘 %.1f%%  90%%区间 %.2f~%.2f  集中度 %.1f%%  现价 %.2f\n",
                        r.avgCost, r.profitRatio * 100.0, r.costLow, r.costHigh,
                        r.concentration * 100.0, win.back().close);
        }
        auto rs = RangeStats::compute(win, floatShares);
        if (rs.success) {
            std::printf("  区间统计: 涨跌 %.2f%%  振幅 %.2f%%  换手 %.2f%%  均价 %.2f  额 %.0f\n",
                        rs.changePct, rs.amplitudePct, rs.turnoverPct, rs.avgPrice, rs.totalAmount);
        }
    }

    // 4. 当日成交分布
    auto intraday = provider.getIntraday(code);
    if (intraday) {
        auto txn = TransactionDistribution::fromIntraday(*intraday, 60);
        std::printf("当日成交分布: %s 桶=%zu 总量 %.0f\n",
                    txn.success ? "ok" : "无数据", txn.points.size(),
                    static_cast<double>(txn.totalVolume));
    } else {
        std::printf("当日成交分布: 分时无数据\n");
    }
    return 0;
}
