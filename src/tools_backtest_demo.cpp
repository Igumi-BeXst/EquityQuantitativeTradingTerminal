#include "data/tencent_provider.h"
#include "data/data_cache.h"
#include "engine/backtest/backtest_engine.h"
#include "engine/strategy/istrategy.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "engine/strategy/templates/turtle_strategy.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <iostream>
#include <memory>

using namespace st;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    LogManager::instance()->init("logs/backtest_demo.log");

    std::cout << "=== P4 端到端回测验证 ===" << std::endl;

    // 1. 腾讯数据源拉取真实数据
    TencentProvider provider;
    provider.connect();
    StockCode code(Market::SH, "600519");  // 贵州茅台
    auto start = utils::parseDate("2023-01-01");
    auto end = utils::parseDate("2024-12-31");
    auto bars = provider.getBars(code, BarPeriod::Daily, start, end);
    std::cout << "拉取 " << code.fullCode() << " 日线: " << bars.size() << " 根" << std::endl;
    if (bars.empty()) {
        std::cout << "!! 数据拉取失败，请检查网络" << std::endl;
        return 1;
    }

    // 2. 存入 DataCache
    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);
    std::cout << "已缓存数据" << std::endl;

    // 3. 回测配置
    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars.front().time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Daily;
    config.feeConfig = FeeConfig::defaultAShare();

    // 诊断: 打印缓存数据范围
    auto cachedBars = cache.getBars(code, BarPeriod::Daily);
    std::cout << "缓存 bars=" << cachedBars.size()
              << " 首=" << utils::toDateString(cachedBars.front().time)
              << " 末=" << utils::toDateString(cachedBars.back().time) << std::endl;

    // 4a. 双均线策略回测
    std::cout << "\n--- 双均线策略 (MA5/MA20) ---" << std::endl;
    {
        BacktestEngine engine;
        engine.setConfig(config);
        engine.setDataCache(&cache);
        auto ma = std::make_shared<MACrossStrategy>();
        engine.addStrategy(ma);
        auto result = engine.run();
        if (result.success) {
            std::cout << "总收益: " << result.performance.totalReturn << "%" << std::endl;
            std::cout << "最大回撤: " << result.performance.maxDrawdown << "%" << std::endl;
            std::cout << "夏普比率: " << result.performance.sharpeRatio << std::endl;
            std::cout << "交易次数: " << result.trades.size() << std::endl;
            std::cout << "期末资产: " << result.finalPortfolio.totalAsset << std::endl;
        } else {
            std::cout << "回测失败: " << result.error << std::endl;
        }
    }

    // 4b. 海龟策略回测
    std::cout << "\n--- 海龟策略 (唐奇安通道) ---" << std::endl;
    {
        BacktestEngine engine;
        engine.setConfig(config);
        engine.setDataCache(&cache);
        engine.addStrategy(std::make_shared<TurtleStrategy>());
        auto result = engine.run();
        if (result.success) {
            std::cout << "总收益: " << result.performance.totalReturn << "%" << std::endl;
            std::cout << "最大回撤: " << result.performance.maxDrawdown << "%" << std::endl;
            std::cout << "夏普比率: " << result.performance.sharpeRatio << std::endl;
            std::cout << "交易次数: " << result.trades.size() << std::endl;
            std::cout << "期末资产: " << result.finalPortfolio.totalAsset << std::endl;
            std::cout << "\n交易明细 (前5笔):" << std::endl;
            for (int i = 0; i < std::min<int>(5, static_cast<int>(result.trades.size())); ++i) {
                auto& t = result.trades[i];
                std::cout << "  " << (t.direction == Direction::Buy ? "买入" : "卖出")
                          << " " << utils::toDateString(t.time)
                          << " @ " << t.price
                          << " x " << t.volume
                          << " 费用=" << t.totalFee << std::endl;
            }
        } else {
            std::cout << "回测失败: " << result.error << std::endl;
        }
    }

    return 0;
}
