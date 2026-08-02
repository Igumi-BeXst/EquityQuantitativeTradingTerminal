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
