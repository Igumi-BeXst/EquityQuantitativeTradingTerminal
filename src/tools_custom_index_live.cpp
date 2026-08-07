// 自定义指数实连校准工具（P10 第七轮开发用）
// 用法: custom_index_live [代码1] [代码2] [代码3] ...  默认 600519 000001
// 走 GUI 同款 MultiProvider 路径（tdx 主源 → tencent 备源），验证指数日线/分时/昨收/实时外推。
#include "data/provider_factory.h"
#include "engine/analyzer/custom_index.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <cstdio>
#include <thread>

using namespace st;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    CustomIndex idx;
    idx.name = "校准组合";
    idx.baseValue = 1000.0;
    idx.baseDate = utils::today();  // 与编辑器一致：基准日 = 创建当天
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            StockCode c(argv[i]);
            idx.constituents.push_back({c, std::string(argv[i]), 0.0});
        }
    } else {
        idx.constituents = {
            {StockCode(Market::SH, "600519"), "贵州茅台", 0.0},
            {StockCode(Market::SZ, "000001"), "平安银行", 0.0},
        };
    }
    normalizeWeights(idx.constituents);

    auto provider = makeDataProvider();
    provider->connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (!provider->isConnected()) {
        std::printf("数据源连接失败\n");
        return 1;
    }
    std::printf("数据源: %s\n", provider->providerName().c_str());

    IDataProvider* p = provider.get();
    const auto fetchDaily = [p](const StockCode& c, BarPeriod) {
        return p->getBars(c, BarPeriod::Daily, DateTime{}, utils::now());
    };
    const auto daily = computeIndexBars(idx, fetchDaily, BarPeriod::Daily);
    std::printf("指数日线 bars=%zu 最早=%s 最新=%s\n", daily.size(),
                daily.empty() ? "-" : utils::toDateString(daily.front().time).c_str(),
                daily.empty() ? "-" : utils::toDateString(daily.back().time).c_str());
    if (daily.empty()) return 1;
    std::printf("  首收盘=%.2f  末收盘=%.2f（基准日=%.2f）\n",
                daily.front().close, daily.back().close, 1000.0);
    for (size_t i = daily.size() > 8 ? daily.size() - 8 : 0; i < daily.size(); ++i) {
        std::printf("  %s %.1f", utils::toDateString(daily[i].time).c_str(), daily[i].close);
    }
    std::printf("\n");

    const double prevClose = lastCompletedClose(daily, utils::now());
    std::printf("指数昨收 = %.2f\n", prevClose);

    // 每个成分股分时诊断（GUI 分时图走同一 fetch 路径）
    for (const auto& c : idx.constituents) {
        auto d = p->getIntraday(c.code);
        std::printf("  成分 %s 分时: %s (preClose=%.2f, points=%zu)\n",
                    c.code.displayCode().c_str(),
                    d ? "有" : "无",
                    d ? d->preClose : 0.0,
                    d ? d->points.size() : 0u);
    }

    const auto fetchIntraday = [p](const StockCode& c) {
        auto d = p->getIntraday(c);
        return d ? *d : IntradayData{};
    };
    const auto intraday = computeIndexIntraday(idx, prevClose, fetchIntraday);
    std::printf("指数分时 points=%zu  首点=%.2f  末点=%.2f\n", intraday.points.size(),
                intraday.points.empty() ? -1 : intraday.points.front().price,
                intraday.points.empty() ? -1 : intraday.points.back().price);

    std::vector<StockCode> codes;
    for (const auto& c : idx.constituents) codes.push_back(c.code);
    const auto quotes = provider->batchQuote(codes);
    const double live = computeIndexLive(prevClose, idx, quotes);
    std::printf("实时外推 = %.2f  (昨收=%.2f, 涨跌幅=%.2f%%)\n",
                live, prevClose, (live / prevClose - 1.0) * 100.0);
    for (const auto& q : quotes) {
        std::printf("  %s 现价=%.2f 涨跌=%.2f%%\n", q.code.displayCode().c_str(),
                    q.lastPrice, q.change);
    }
    return 0;
}
