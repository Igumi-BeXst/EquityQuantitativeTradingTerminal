#include "data/tencent_provider.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    st::LogManager::instance()->init("logs/fetch_test.log");
    st::TencentProvider provider;
    provider.connect();

    // 拉取贵州茅台日线
    st::StockCode code(st::Market::SH, "600519");
    auto start = st::utils::parseDate("2024-01-01");
    auto end = st::utils::parseDate("2024-06-30");
    auto bars = provider.getBars(code, st::BarPeriod::Daily, start, end);

    std::cout << "Fetched " << bars.size() << " bars for " << code.fullCode() << std::endl;
    for (int i = 0; i < std::min<int>(5, static_cast<int>(bars.size())); ++i) {
        auto& b = bars[i];
        std::cout << st::utils::toDateString(b.time)
                  << " O=" << b.open << " H=" << b.high
                  << " L=" << b.low << " C=" << b.close
                  << " V=" << b.volume << std::endl;
    }

    // 获取股票信息
    auto info = provider.getStockInfo(code);
    if (info) {
        std::cout << "Stock name: " << info->name << std::endl;
    }

    // 获取股票列表
    auto stocks = provider.getStockList(st::Market::SH);
    std::cout << "SH stocks: " << stocks.size() << std::endl;
    for (int i = 0; i < std::min<int>(3, static_cast<int>(stocks.size())); ++i) {
        std::cout << "  " << stocks[i].code.fullCode() << " " << stocks[i].name << std::endl;
    }
    return 0;
}
