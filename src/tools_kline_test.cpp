// K线/分时接口 live 验证工具 — 实测日/周/月/分钟/分时数据
#include "data/tencent_provider.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <iostream>

using namespace st;

static void printBars(const char* label, const std::vector<Bar>& bars) {
    std::cout << label << ": " << bars.size() << " 根";
    if (!bars.empty()) {
        std::cout << " 首=" << utils::toDateString(bars.front().time)
                  << " 末=" << utils::toDateString(bars.back().time)
                  << " 收盘=" << bars.back().close
                  << " 量=" << bars.back().volume;
    }
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    LogManager::instance()->init("logs/kline_test.log");

    TencentProvider provider;
    provider.connect();
    StockCode code(Market::SH, "600519");  // 贵州茅台

    // 日/周/月（空 start = 最近 640 根）
    auto daily = provider.getBars(code, BarPeriod::Daily, DateTime{}, utils::now());
    printBars("日线", daily);

    auto weekly = provider.getBars(code, BarPeriod::Weekly, DateTime{}, utils::now());
    printBars("周线", weekly);

    auto monthly = provider.getBars(code, BarPeriod::Monthly, DateTime{}, utils::now());
    printBars("月线", monthly);

    // 分钟 K 线
    auto m5 = provider.getBars(code, BarPeriod::Minute5, DateTime{}, utils::now());
    printBars("5分钟", m5);

    auto m60 = provider.getBars(code, BarPeriod::Minute60, DateTime{}, utils::now());
    printBars("60分钟", m60);

    // 分时
    auto intra = provider.getIntraday(code);
    if (intra && !intra->empty()) {
        std::cout << "分时: " << intra->points.size() << " 点 日期="
                  << utils::toDateString(intra->date)
                  << " 昨收=" << intra->preClose
                  << " 首点=" << utils::toDateTimeString(intra->points.front().time)
                  << " 价=" << intra->points.front().price
                  << " 量=" << intra->points.front().volume << std::endl;
    } else {
        std::cout << "分时: 无数据" << std::endl;
    }

    // 校验: 周/月应聚合真实（修复前的 bug 会返回日线）。
    // 周线回溯时间应早于日线（640 上限下跨度更广）；月线根数应明显少于周线。
    bool weeklyAgg = !daily.empty() && !weekly.empty() && weekly.front().time < daily.front().time;
    bool monthlyAgg = !monthly.empty() && monthly.size() < weekly.size() / 2;
    bool ok = weeklyAgg && monthlyAgg && !m5.empty() && m60.empty() == false
              && intra && !intra->empty();
    std::cout << "\n=== " << (ok ? "OK: 周/月聚合正确 + 分钟/分时可用" : "FAIL: 周期数据异常") << " ===" << std::endl;
    return ok ? 0 : 1;
}
