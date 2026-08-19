#include <gtest/gtest.h>
#include "engine/backtest/backtest_engine.h"
#include "engine/strategy/templates/momentum_strategy.h"
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

using namespace st;

namespace {

// 简单策略: 第一个Bar全仓买入，持有到结束
class BuyAndHoldStrategy : public IStrategy {
public:
    std::string name() const override { return "BuyAndHold"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        if (!bought_) {
            auto available = ctx.portfolio->cash;
            buyByAmount(available * 0.9);  // 90% 资金买入
            bought_ = true;
        }
    }
private:
    bool bought_ = false;
};

// 策略: 第1个Bar买入，第10个Bar清仓（产生一次平仓，验证交易统计填充）
class BuyThenSellStrategy : public IStrategy {
public:
    std::string name() const override { return "BuyThenSell"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        if (!bought_) {
            buyByAmount(ctx.portfolio->cash * 0.9);
            bought_ = true;
        } else if (!sold_ && ctx.history->size() >= 10) {
            sellAll();
            sold_ = true;
        }
    }
private:
    bool bought_ = false;
    bool sold_ = false;
};

// 策略: 第1根 Bar 买入，第2根 Bar 尝试卖出（用于验证分钟级 T+1）
class BuyThenSellNextBarStrategy : public IStrategy {
public:
    std::string name() const override { return "BuyThenSellNextBar"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext&) override {
        ++calls_;
        if (calls_ == 1) {
            buy(100);
        } else if (calls_ == 2) {
            sell(100);
        }
    }
private:
    int calls_ = 0;
};

std::vector<Bar> makeRisingSeries(const StockCode& code, int n, double startPrice) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        bar.open = startPrice + i;
        bar.high = startPrice + i + 0.5;
        bar.low = startPrice + i - 0.5;
        bar.close = startPrice + i;
        bar.volume = 10000;
        bars.push_back(bar);
    }
    return bars;
}

std::vector<Bar> makeSeriesFromCloses(const StockCode& code, const std::vector<double>& closes) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (size_t i = 0; i < closes.size(); ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, static_cast<int>(i));
        bar.open = closes[i];
        bar.high = closes[i] * 1.01;
        bar.low = closes[i] * 0.99;
        bar.close = closes[i];
        bar.volume = 10000;
        bars.push_back(bar);
    }
    return bars;
}

} // namespace

TEST(BacktestEngineTest, BuyAndHoldRisingMarket) {
    StockCode code(Market::SH, "600519");
    auto bars = makeRisingSeries(code, 20, 100.0);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars.front().time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Daily;

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);
    engine.addStrategy(std::make_shared<BuyAndHoldStrategy>());

    auto result = engine.run();

    ASSERT_TRUE(result.success) << result.error;
    // 上涨市场买入持有 → 正收益
    EXPECT_GT(result.performance.totalReturn, 0.0);
    EXPECT_FALSE(result.trades.empty());
    // 有交易发生
    EXPECT_GT(result.trades.size(), 0u);
    // 期末总资产应大于初始（上涨）
    EXPECT_GT(result.finalPortfolio.totalAsset, config.initialCapital);
}

// 策略: 记录第一根 bar 时 history 的长度（验证回测起始日前的 warm-up 数据已预填）
class RecordFirstHistorySizeStrategy : public IStrategy {
public:
    std::string name() const override { return "RecordFirstHistorySize"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}
    void onBar(const StrategyContext& ctx) override {
        if (!recorded_) {
            firstSize_ = ctx.history ? ctx.history->size() : 0;
            recorded_ = true;
        }
    }
    size_t firstSize() const { return firstSize_; }
private:
    bool recorded_ = false;
    size_t firstSize_ = 0;
};

TEST(BacktestEngineTest, PreStartHistoryIsAvailableToStrategy) {
    StockCode code(Market::SH, "600519");
    std::vector<double> closes;
    for (int i = 0; i < 40; ++i) closes.push_back(100.0 + i);
    auto bars = makeSeriesFromCloses(code, closes);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars[30].time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Daily;

    auto strategy = std::make_shared<RecordFirstHistorySizeStrategy>();
    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);
    engine.addStrategy(strategy);

    auto result = engine.run();
    ASSERT_TRUE(result.success) << result.error;
    // 第一根 bar 时应能看到 startDate 之前的 30 根 warm-up 历史
    EXPECT_EQ(strategy->firstSize(), 30u);
}

TEST(BacktestEngineTest, NoDataReturnsError) {
    StockCode code(Market::SH, "600519");
    DataCache cache;  // 空缓存

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = utils::parseDate("2024-01-01");
    config.endDate = utils::parseDate("2024-12-31");

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);

    auto result = engine.run();
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

// 策略: 每 3 根 bar 清仓一次 → 高频触发 getPortfolio（旧代码共享 static Portfolio，并发时数据竞争）
class VolatileSellStrategy : public IStrategy {
public:
    std::string name() const override { return "VolatileSell"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}
    void onBar(const StrategyContext& ctx) override {
        if (!bought_) {
            buy(100);
            bought_ = true;
        }
        if (ctx.history && ctx.history->size() % 3 == 0) {
            sellAll();  // portfolio() → getPortfolio
        }
    }
private:
    bool bought_ = false;
};

TEST(BacktestEngineTest, ConcurrentEnginesPortfolioSafe) {
    // 并发跑多个引擎，策略高频调 portfolio()/sellAll()
    // 回归: 旧代码 getPortfolio 用函数级 static Portfolio（跨线程共享 → 数据竞争/堆损坏）；
    //       现改为引擎实例成员（每引擎独立 → 安全）。
    StockCode code(Market::SH, "600519");
    std::vector<double> closes;
    for (int i = 0; i < 150; ++i) closes.push_back(100.0 + 30.0 * std::sin(i / 4.0));

    DataCache cache;  // 共享（线程安全）
    cache.cacheBars(code, BarPeriod::Daily, makeSeriesFromCloses(code, closes));
    const auto start = utils::parseDate("2024-01-02");
    const auto end = utils::addTradingDays(start, static_cast<int>(closes.size()) - 1);

    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            BacktestConfig config;
            config.symbols = {code};
            config.startDate = start;
            config.endDate = end;
            config.initialCapital = 100000.0;
            config.period = BarPeriod::Daily;
            BacktestEngine engine;
            engine.setConfig(config);
            engine.setDataCache(&cache);
            engine.addStrategy(std::make_shared<VolatileSellStrategy>());
            auto result = engine.run();
            if (result.success && !result.performance.equityCurve.empty()) ++ok;
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(ok.load(), 4);
}

TEST(BacktestEngineTest, TradeStatsFilled) {
    // 买入→卖出 一次平仓：交易统计应被填充
    StockCode code(Market::SH, "600519");
    auto bars = makeRisingSeries(code, 20, 100.0);

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars.front().time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Daily;

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);
    engine.addStrategy(std::make_shared<BuyThenSellStrategy>());

    auto result = engine.run();
    ASSERT_TRUE(result.success) << result.error;

    // 一次平仓 → 胜率 100%，盈亏为正（上涨行情）
    EXPECT_EQ(result.performance.totalTrades, 1);
    EXPECT_EQ(result.performance.winningTrades, 1);
    EXPECT_NEAR(result.performance.winRate, 100.0, 0.01);
    EXPECT_GT(result.performance.totalPnl, 0.0);
    EXPECT_GT(result.performance.profitFactor, 1.0);
}

TEST(BacktestEngineTest, T1BlocksSameDaySellInMinuteBars) {
    StockCode code(Market::SH, "600519");

    std::vector<Bar> bars;
    const auto base = utils::parseDateTime("2026-08-03 09:30:00");
    for (int i = 0; i < 5; ++i) {
        Bar b;
        b.code = code;
        b.period = BarPeriod::Minute1;
        b.time = base + std::chrono::minutes(i);
        b.open = b.high = b.low = b.close = 100.0 + i;
        b.volume = 1000;
        bars.push_back(b);
    }

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Minute1, bars);

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars.front().time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Minute1;

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);
    engine.addStrategy(std::make_shared<BuyThenSellNextBarStrategy>());

    auto result = engine.run();
    ASSERT_TRUE(result.success) << result.error;

    // 第1根买入，第2根尝试卖出：同一天内卖出应被 T+1 拦截
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].direction, Direction::Buy);
    ASSERT_FALSE(result.finalPortfolio.positions.empty());
    EXPECT_EQ(result.finalPortfolio.positions[0].quantity, 100);
}

TEST(BacktestEngineTest, MomentumStrategyProducesTradesWithSyntheticData) {
    StockCode code(Market::SH, "600519");

    std::vector<Bar> bars;
    const auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < 80; ++i) {
        Bar b;
        b.code = code;
        b.period = BarPeriod::Daily;
        b.time = utils::addTradingDays(base, i);
        double close = 10.0 + i * 0.3;  // 持续上涨，3日动量远超 5%
        b.open = close - 0.1;
        b.high = close + 0.2;
        b.low = close - 0.2;
        b.close = close;
        b.volume = 10000;
        bars.push_back(b);
    }

    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    auto strategy = std::make_shared<MomentumStrategy>();
    strategy->lookbackPeriod_ = 3;
    strategy->exitPeriod_ = 2;
    strategy->thresholdPct_ = 50;  // 5%

    BacktestConfig config;
    config.symbols = {code};
    config.startDate = bars.front().time;
    config.endDate = bars.back().time;
    config.initialCapital = 100000.0;
    config.period = BarPeriod::Daily;

    BacktestEngine engine;
    engine.setConfig(config);
    engine.setDataCache(&cache);
    engine.addStrategy(strategy);

    auto result = engine.run();
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.trades.empty());
}
