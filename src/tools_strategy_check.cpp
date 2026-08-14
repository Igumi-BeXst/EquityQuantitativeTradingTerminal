// 策略模板真实数据验证工具（P10 第二十轮）
// 连 TDX 拉真实日线 → 6 个策略模板各跑一次回测 → 打印绩效与交易数
#include "data/idata_provider.h"
#include "data/provider_factory.h"
#include "data/data_cache.h"
#include "engine/backtest/backtest_engine.h"
#include "engine/optimizer/grid_search.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <vector>

using namespace st;

namespace {
void runOne(const char* title, const std::string& id,
            const std::vector<std::pair<std::string, int>>& params,
            const BacktestConfig& cfg, DataCache& cache) {
    std::cout << "\n--- " << title << " ---" << std::endl;
    auto strategy = GridSearchOptimizer::makeStrategy(id, params);
    if (!strategy) {
        std::cout << "!! makeStrategy 未注册 id=" << id << std::endl;
        return;
    }
    BacktestEngine engine;
    engine.setConfig(cfg);
    engine.setDataCache(&cache);
    engine.addStrategy(strategy);
    auto result = engine.run();
    if (!result.success) {
        std::cout << "回测失败: " << result.error << std::endl;
        return;
    }
    std::cout << "总收益: " << result.performance.totalReturn << "%"
              << "  最大回撤: " << result.performance.maxDrawdown << "%"
              << "  夏普: " << result.performance.sharpeRatio
              << "  交易次数: " << result.trades.size()
              << "  期末资产: " << result.finalPortfolio.totalAsset << std::endl;
}
}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    LogManager::instance()->init("logs/strategy_check.log");

    std::cout << "=== 策略模板真实数据验证（6 策略 × 茅台日线）===" << std::endl;

    auto provider = makeDataProvider();
    if (!provider || !provider->connect()) {
        std::cout << "!! 数据源连接失败" << std::endl;
        return 1;
    }
    std::cout << "数据源: " << provider->providerName() << std::endl;

    StockCode code(Market::SH, "600519");
    auto start = utils::parseDate("2023-01-01");
    auto end = utils::now();
    auto bars = provider->getBars(code, BarPeriod::Daily, start, end);
    std::cout << "拉取 " << code.fullCode() << " 日线: " << bars.size() << " 根"
              << (bars.empty() ? "" : "（首 " + utils::toDateString(bars.front().time) +
                                    " 末 " + utils::toDateString(bars.back().time) + "）")
              << std::endl;
    if (bars.empty()) {
        std::cout << "!! 数据拉取失败" << std::endl;
        return 1;
    }

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    BacktestConfig cfg;
    cfg.symbols = {code};
    cfg.startDate = bars.front().time;
    cfg.endDate = bars.back().time;
    cfg.initialCapital = 100000.0;
    cfg.period = BarPeriod::Daily;
    cfg.feeConfig = FeeConfig::defaultAShare();

    runOne("双均线 MACross(5,20)", "MACross", {{"fastPeriod", 5}, {"slowPeriod", 20}},
           cfg, cache);
    runOne("海龟 Turtle(20,10)", "Turtle", {{"entryPeriod", 20}, {"exitPeriod", 10}},
           cfg, cache);
    runOne("动量 Momentum(20,10)", "Momentum", {{"lookbackPeriod", 20}, {"exitPeriod", 10}},
           cfg, cache);
    runOne("收盘突破 Breakout(20,10)", "Breakout",
           {{"entryPeriod", 20}, {"exitPeriod", 10}}, cfg, cache);
    runOne("均值回归 MeanReversion(20,30)", "MeanReversion",
           {{"maPeriod", 20}, {"deviationPct", 30}}, cfg, cache);
    runOne("RSI Rsi(30,70)", "Rsi", {{"buyLevel", 30}, {"sellLevel", 70}}, cfg, cache);

    std::cout << "\n=== 验证完成 ===" << std::endl;
    return 0;
}
