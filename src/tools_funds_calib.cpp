// 资金数据实连校准工具（P10 第八轮验证用）
// 验证路径：东财 datacenter-web + kamt → 龙虎榜/北向/两融 全链路解析。
#include "data/eastmoney_funds_provider.h"
#include "data/provider_factory.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace st;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    EastMoneyFundsProvider p;

    // 交易日历验证：TDX 上证指数日线（龙虎榜日期下拉的数据源）
    {
        auto provider = makeDataProvider();
        provider->connect();
        std::this_thread::sleep_for(std::chrono::seconds(4));
        if (provider->isConnected()) {
            auto bars = provider->getBars(StockCode("SH000001"), BarPeriod::Daily,
                                          utils::today() - std::chrono::hours(200 * 24),
                                          utils::today());
            std::printf("交易日历: %zu 个交易日，最近 5 个:", bars.size());
            for (size_t i = bars.size() > 5 ? bars.size() - 5 : 0; i < bars.size(); ++i) {
                std::printf(" %s", utils::toDateString(bars[i].time).c_str());
            }
            std::printf("\n");
        } else {
            std::printf("TDX 未连接，无法取交易日历\n");
        }
    }


    const auto lhb = p.fetchDragonTiger(utils::toDateString(utils::today()));
    std::printf("龙虎榜(%s) %zu 条\n", utils::toDateString(utils::today()).c_str(), lhb.size());
    for (size_t i = 0; i < lhb.size() && i < 5; ++i) {
        std::printf("  %s %s 净买%.2f亿 涨跌%.2f%% 换手%.2f%% %s\n",
                    lhb[i].code.c_str(), lhb[i].name.c_str(),
                    lhb[i].netAmt / 1e8, lhb[i].changeRate, lhb[i].turnoverRate,
                    lhb[i].reason.c_str());
    }

    const auto mkt = p.fetchMarginMarket();
    if (!mkt.empty()) {
        std::printf("沪深两融(%s): 融资%.2f亿 两融%.2f亿\n",
                    utils::toDateString(mkt[0].date).c_str(),
                    mkt[0].financeBalance / 1e8, mkt[0].marginBalance / 1e8);
    }

    const auto stk = p.fetchMargin("600519");
    if (!stk.empty()) {
        std::printf("600519 贵州茅台 %zu 条, 最新 %s: 融资%.2f亿 融券%.2f亿 两融%.2f亿\n",
                    stk.size(), utils::toDateString(stk[0].date).c_str(),
                    stk[0].financeBalance / 1e8, stk[0].shortBalance / 1e8,
                    stk[0].marginBalance / 1e8);
    }
    return 0;
}
