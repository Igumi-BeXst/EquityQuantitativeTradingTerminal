#include <gtest/gtest.h>
#include "engine/paper_trade/paper_trade_engine.h"
#include "engine/paper_trade/paper_trade_state_store.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "foundation/utils/datetime.h"
#include <filesystem>

using namespace st;

namespace {

// 策略: 首次收到行情即买入100股
class BuyOnceStrategy : public IStrategy {
public:
    std::string name() const override { return "BuyOnce"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext&) override {
        if (!bought_) {
            buy(100);
            bought_ = true;
        }
    }
private:
    bool bought_ = false;
};

// 策略: 第1次买入，第2次尝试卖出，第3次再尝试卖出（用于验证 T+1）
class BuyThenSellStrategy : public IStrategy {
public:
    std::string name() const override { return "BuyThenSell"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext&) override {
        ++calls_;
        if (calls_ == 1) {
            buy(100);
        } else if (calls_ == 2 || calls_ == 3) {
            sell(100);
        }
    }
private:
    int calls_ = 0;
};

// 策略: 记录每次 onBar 看到的当前 bar（用于验证日线聚合）
class RecordBarStrategy : public IStrategy {
public:
    std::string name() const override { return "RecordBar"; }
    void initialize() override {}
    void onStart() override {}
    void onStop() override {}

    void onBar(const StrategyContext& ctx) override {
        if (!ctx.currentBar) return;
        dates.push_back(utils::toDateString(ctx.currentBar->time));
        closes.push_back(ctx.currentBar->close);
        highs.push_back(ctx.currentBar->high);
        lows.push_back(ctx.currentBar->low);
    }

    std::vector<std::string> dates;
    std::vector<double> closes;
    std::vector<double> highs;
    std::vector<double> lows;
};

} // namespace

TEST(PaperTradeEngineTest, BuySharesUpdatesPortfolio) {
    PaperTradeEngine engine;
    PaperTradeConfig config;
    config.initialCapital = 100000.0;
    config.slippage = 0.0;
    engine.setConfig(config);

    auto strategy = std::make_shared<BuyOnceStrategy>();
    engine.addStrategy(strategy);
    engine.start();

    StockCode code(Market::SH, "600519");
    auto now = utils::now();
    engine.onQuote(code, 100.0, now);

    // 买入100股 @ 100 = 10000 + 总费用
    // 佣金5(最低) + 过户0.2 + 经手0.487 + 证管0.2 = 5.887
    const auto& pf = engine.portfolio();
    EXPECT_NEAR(pf.cash, 100000.0 - 10000.0 - 5.887, 0.01);
    EXPECT_GT(pf.marketValue, 0.0);
    ASSERT_FALSE(pf.positions.empty());
    EXPECT_EQ(pf.positions[0].quantity, 100);

    engine.stop();
}

TEST(PaperTradeEngineTest, T1BlocksSameDaySellAndAllowsNextDay) {
    PaperTradeEngine engine;
    PaperTradeConfig config;
    config.initialCapital = 100000.0;
    config.slippage = 0.0;
    engine.setConfig(config);

    auto strategy = std::make_shared<BuyThenSellStrategy>();
    engine.addStrategy(strategy);
    engine.start();

    StockCode code(Market::SH, "600519");
    const auto day1 = utils::parseDateTime("2026-08-03 09:30:00");
    const auto day1b = utils::parseDateTime("2026-08-03 14:00:00");
    const auto day2 = utils::parseDateTime("2026-08-04 09:30:00");

    // 第1根报价：买入 100 股
    engine.onQuote(code, 100.0, day1);
    ASSERT_EQ(engine.trades().size(), 1u);
    EXPECT_EQ(engine.trades()[0].direction, Direction::Buy);
    ASSERT_FALSE(engine.portfolio().positions.empty());
    EXPECT_EQ(engine.portfolio().positions[0].available, 0);  // 当日不可卖
    EXPECT_EQ(engine.portfolio().positions[0].todayBuy, 100);

    // 同一交易日再报价：策略尝试卖出，应被 T+1 拦截
    engine.onQuote(code, 101.0, day1b);
    EXPECT_EQ(engine.trades().size(), 1u);

    // 下一交易日：解冻后可卖出
    engine.onQuote(code, 102.0, day2);
    ASSERT_EQ(engine.trades().size(), 2u);
    EXPECT_EQ(engine.trades()[1].direction, Direction::Sell);

    engine.stop();
}

TEST(PaperTradeEngineTest, DailyQuotesAggregateIntoSingleBar) {
    PaperTradeEngine engine;
    PaperTradeConfig config;
    config.initialCapital = 100000.0;
    config.slippage = 0.0;
    engine.setConfig(config);

    auto strategy = std::make_shared<RecordBarStrategy>();
    engine.addStrategy(strategy);
    engine.start();

    StockCode code(Market::SH, "600519");
    const auto d1a = utils::parseDateTime("2026-08-03 09:30:00");
    const auto d1b = utils::parseDateTime("2026-08-03 10:00:00");
    const auto d1c = utils::parseDateTime("2026-08-03 14:00:00");
    const auto d2 = utils::parseDateTime("2026-08-04 09:30:00");

    engine.onQuote(code, 100.0, d1a);
    engine.onQuote(code, 110.0, d1b);
    engine.onQuote(code, 90.0, d1c);
    engine.onQuote(code, 105.0, d2);

    // 共 4 次 onBar；前 3 次是同一根日线，最后一次是新日线
    ASSERT_EQ(strategy->dates.size(), 4u);
    EXPECT_EQ(strategy->dates[0], strategy->dates[1]);
    EXPECT_EQ(strategy->dates[1], strategy->dates[2]);
    EXPECT_NE(strategy->dates[2], strategy->dates[3]);

    // 同一根日线聚合 OHLC：open=100, high=110, low=90, close=90
    EXPECT_DOUBLE_EQ(strategy->closes[0], 100.0);
    EXPECT_DOUBLE_EQ(strategy->closes[1], 110.0);
    EXPECT_DOUBLE_EQ(strategy->closes[2], 90.0);
    EXPECT_DOUBLE_EQ(strategy->highs[2], 110.0);
    EXPECT_DOUBLE_EQ(strategy->lows[2], 90.0);

    engine.stop();
}

TEST(PaperTradeEngineTest, SlippageAffectsFillPrice) {
    PaperTradeEngine engine;
    PaperTradeConfig config;
    config.initialCapital = 100000.0;
    config.slippage = 0.001;  // 0.1%
    engine.setConfig(config);

    auto strategy = std::make_shared<BuyOnceStrategy>();
    engine.addStrategy(strategy);
    engine.start();

    StockCode code(Market::SH, "600519");
    engine.onQuote(code, 100.0, utils::now());

    // 买入滑点: 100 * 1.001 = 100.1
    const auto& trades = engine.trades();
    ASSERT_FALSE(trades.empty());
    EXPECT_NEAR(trades[0].price, 100.1, 0.01);

    engine.stop();
}

TEST(PaperTradeEngineTest, NotRunningDoesNothing) {
    PaperTradeEngine engine;
    PaperTradeConfig config;
    config.initialCapital = 100000.0;
    engine.setConfig(config);

    auto strategy = std::make_shared<BuyOnceStrategy>();
    engine.addStrategy(strategy);
    // 未 start()

    StockCode code(Market::SH, "600519");
    engine.onQuote(code, 100.0, utils::now());

    // 不运行 → 无交易、无持仓
    EXPECT_TRUE(engine.trades().empty());
    EXPECT_TRUE(engine.portfolio().positions.empty());
}

TEST(PaperTradeEngineTest, TrendStrategyTradesWithSeededHistory) {
    // 历史播种后，趋势策略（双均线）能在模拟交易中触发买入
    StockCode code(Market::SH, "600519");
    PaperTradeEngine engine;
    PaperTradeConfig config;
    config.initialCapital = 100000.0;
    config.slippage = 0.0;
    engine.setConfig(config);

    auto strategy = std::make_shared<MACrossStrategy>();
    strategy->fastPeriod_ = 3;
    strategy->slowPeriod_ = 5;
    engine.addStrategy(strategy);

    // 播种 8 根横盘日线（slow=5 立即可算均线）
    std::vector<Bar> seed;
    const auto base = utils::parseDate("2024-01-02");
    for (int i = 0; i < 8; ++i) {
        Bar b;
        b.code = code;
        b.period = BarPeriod::Daily;
        b.time = utils::addTradingDays(base, i);
        b.open = b.high = b.low = b.close = 100.0;
        b.volume = 10000;
        seed.push_back(b);
    }
    engine.seedHistory(code, seed);
    engine.start();

    // 按日线聚合：每个交易日喂一根报价，连续上升 → 金叉 → 触发买入
    for (int i = 0; i < 5; ++i) {
        const auto t = utils::addTradingDays(base, 8 + i);
        engine.onQuote(code, 100.0 + static_cast<double>(i) * 2.0, t);
    }

    const auto& trades = engine.trades();
    ASSERT_FALSE(trades.empty());
    EXPECT_EQ(trades.front().direction, Direction::Buy);
    engine.stop();
}

TEST(PaperTradeEngineTest, OnTradeCallbackFires) {
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    cfg.slippage = 0.0;
    e.setConfig(cfg);

    int fired = 0;
    Trade captured;
    e.setOnTrade([&](const Trade& t) { ++fired; captured = t; });

    auto s = std::make_shared<BuyOnceStrategy>();
    e.addStrategy(s);
    e.start();
    e.onQuote(StockCode("SH600519"), 10.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_GE(fired, 1);
    EXPECT_EQ(captured.code.fullCode(), "SH600519");
    EXPECT_EQ(captured.direction, Direction::Buy);
    e.stop();
}

TEST(PaperTradeEngineTest, NoCallbackIsSafe) {
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    cfg.slippage = 0.0;
    e.setConfig(cfg);

    auto s = std::make_shared<BuyOnceStrategy>();
    e.addStrategy(s);
    e.start();
    e.onQuote(StockCode("SH600519"), 10.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_EQ(e.trades().size(), 1u);
    e.stop();
}

// ---- 多股票 ----

TEST(PaperTradeEngineTest, MultiStockIndependentStrategies) {
    // 两只股票各自绑定 BuyOnceStrategy：每只独立买入，互不串扰
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    cfg.slippage = 0.0;
    e.setConfig(cfg);

    StockCode a(Market::SH, "600519");
    StockCode b(Market::SZ, "000001");
    e.addStrategy(a, std::make_shared<BuyOnceStrategy>());
    e.addStrategy(b, std::make_shared<BuyOnceStrategy>());
    e.start();

    e.onQuote(a, 100.0, utils::parseDateTime("2026-08-01 09:30:00"));
    e.onQuote(b, 50.0, utils::parseDateTime("2026-08-01 09:30:00"));

    const auto& pf = e.portfolio();
    ASSERT_EQ(pf.positions.size(), 2u);
    // 两只股票都买入 100 股
    EXPECT_EQ(pf.positions[0].quantity, 100);
    EXPECT_EQ(pf.positions[1].quantity, 100);
    EXPECT_EQ(e.trades().size(), 2u);
    e.stop();
}

TEST(PaperTradeEngineTest, MultiStockOrderRoutedByCode) {
    // 策略只对 A 下单；B 的报价不应成交 A 的挂单
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    cfg.slippage = 0.0;
    e.setConfig(cfg);

    StockCode a(Market::SH, "600519");
    StockCode b(Market::SZ, "000001");
    e.addStrategy(a, std::make_shared<BuyOnceStrategy>());
    e.addStrategy(b, std::make_shared<BuyOnceStrategy>());
    e.start();

    // 只喂 B：A 的挂单不应被 B 报价成交
    e.onQuote(b, 50.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_EQ(e.trades().size(), 1u);
    EXPECT_EQ(e.trades()[0].code.fullCode(), b.fullCode());

    // 再喂 A：A 的挂单成交
    e.onQuote(a, 100.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_EQ(e.trades().size(), 2u);
    EXPECT_EQ(e.trades()[1].code.fullCode(), a.fullCode());
    e.stop();
}

TEST(PaperTradeEngineTest, MultiStockLastPricePerCode) {
    // buyByAmount 按各自股票最近报价换算股数
    PaperTradeEngine e;
    PaperTradeConfig cfg;
    cfg.initialCapital = 1000000;
    cfg.slippage = 0.0;
    e.setConfig(cfg);

    StockCode a(Market::SH, "600519");
    StockCode b(Market::SZ, "000001");
    e.addStrategy(a, std::make_shared<BuyOnceStrategy>());
    e.addStrategy(b, std::make_shared<BuyOnceStrategy>());
    e.start();

    // 先喂 A 高价，再喂 B 低价：各自的 buyByAmount 用各自价格
    e.onQuote(a, 200.0, utils::parseDateTime("2026-08-01 09:30:00"));
    e.onQuote(b, 10.0, utils::parseDateTime("2026-08-01 09:30:00"));
    EXPECT_EQ(e.trades().size(), 2u);
    e.stop();
}

TEST(PaperTradeEngineTest, CaptureRestorePreservesPortfolioAndTrades) {
    PaperTradeEngine e1;
    PaperTradeConfig cfg;
    cfg.initialCapital = 100000;
    cfg.slippage = 0.0;
    e1.setConfig(cfg);

    StockCode code(Market::SH, "600519");
    e1.addStrategy(code, std::make_shared<BuyOnceStrategy>());
    e1.start();
    e1.onQuote(code, 100.0, utils::parseDateTime("2026-08-03 09:30:00"));

    auto state = e1.capture();
    e1.stop();

    PaperTradeEngine e2;
    e2.setConfig(cfg);
    e2.restore(state);

    EXPECT_EQ(e2.portfolio().cash, e1.portfolio().cash);
    ASSERT_EQ(e2.portfolio().positions.size(), 1u);
    EXPECT_EQ(e2.portfolio().positions[0].quantity, 100);
    EXPECT_EQ(e2.portfolio().positions[0].todayBuy, 100);
    EXPECT_EQ(e2.portfolio().positions[0].available, 0);  // T+1 状态也保留
    ASSERT_EQ(e2.trades().size(), 1u);
    EXPECT_EQ(e2.trades()[0].direction, Direction::Buy);
    EXPECT_EQ(e2.trades()[0].code.fullCode(), "SH600519");
}

TEST(PaperTradeEngineTest, StateStoreRoundTrip) {
    PaperTradeState state;
    state.strategyId = "MACross";
    state.p1 = 5;
    state.p2 = 20;
    state.capital = 100000;
    state.slippage = 0.001;
    state.symbols = {"SH600519", "SZ000001"};

    state.engine.initialCapital = 100000;
    state.engine.cash = 90000;
    state.engine.currentTradeDate = "2026-08-03";

    Position pos;
    pos.code = StockCode("SH600519");
    pos.quantity = 100;
    pos.available = 0;
    pos.todayBuy = 100;
    pos.avgCost = 100.0;
    pos.costBasis = 10000.0;
    pos.currentPrice = 101.0;
    pos.marketValue = 10100.0;
    state.engine.positions.push_back(pos);

    Trade t;
    t.id = "T1";
    t.orderId = "P1";
    t.code = StockCode("SH600519");
    t.direction = Direction::Buy;
    t.price = 100.0;
    t.volume = 100;
    t.amount = 10000.0;
    t.time = utils::parseDateTime("2026-08-03 09:30:00");
    t.strategyId = "MACross";
    state.engine.trades.push_back(t);

    Bar b;
    b.code = StockCode("SH600519");
    b.time = utils::parseDateTime("2026-08-02 00:00:00");
    b.period = BarPeriod::Daily;
    b.open = b.high = b.low = b.close = 100.0;
    b.volume = 10000;
    state.engine.history["SH600519"].push_back(b);

    const auto path = (std::filesystem::temp_directory_path() / "paper_trade_state_test.json").string();
    PaperTradeStateStore store;
    ASSERT_TRUE(store.save(path, state));

    PaperTradeState loaded;
    ASSERT_TRUE(store.load(path, loaded));
    EXPECT_EQ(loaded.strategyId, state.strategyId);
    EXPECT_EQ(loaded.p1, state.p1);
    EXPECT_EQ(loaded.p2, state.p2);
    EXPECT_EQ(loaded.capital, state.capital);
    EXPECT_EQ(loaded.slippage, state.slippage);
    EXPECT_EQ(loaded.symbols.size(), state.symbols.size());

    ASSERT_EQ(loaded.engine.positions.size(), 1u);
    EXPECT_EQ(loaded.engine.positions[0].quantity, 100);
    EXPECT_EQ(loaded.engine.positions[0].todayBuy, 100);
    EXPECT_EQ(loaded.engine.positions[0].available, 0);

    ASSERT_EQ(loaded.engine.trades.size(), 1u);
    EXPECT_EQ(loaded.engine.trades[0].id, "T1");
    EXPECT_EQ(loaded.engine.trades[0].direction, Direction::Buy);

    ASSERT_EQ(loaded.engine.history.count("SH600519"), 1u);
    EXPECT_EQ(loaded.engine.history.at("SH600519").size(), 1u);
    EXPECT_EQ(loaded.engine.history.at("SH600519")[0].close, 100.0);

    std::filesystem::remove(path);
}
