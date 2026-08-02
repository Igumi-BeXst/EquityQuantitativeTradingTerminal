#include <gtest/gtest.h>
#include "engine/paper_trade/paper_trade_engine.h"
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

    void onBar(const StrategyContext& ctx) override {
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
