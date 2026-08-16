#include <gtest/gtest.h>
#include "engine/paper_trade/paper_trade_engine.h"
#include "engine/strategy/templates/ma_cross_strategy.h"
#include "foundation/utils/datetime.h"

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

    // 连续喂上升报价 → 金叉 → 触发买入
    const auto t = utils::parseDate("2024-01-10");
    for (int i = 0; i < 5; ++i) {
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
