#include "engine/strategy/istrategy.h"
#include "engine/strategy/templates/momentum_strategy.h"
#include "engine/strategy/templates/breakout_strategy.h"
#include "engine/strategy/templates/mean_reversion_strategy.h"
#include "engine/strategy/templates/rsi_strategy.h"
#include "foundation/stock_code.h"
#include "foundation/utils/datetime.h"
#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <vector>

namespace st {
namespace {

using st::Direction;

const StockCode kCode(std::string_view("SH600519"));

/// 由收盘序列生成日线（open=前收，high/low 包络）
std::vector<Bar> makeBars(const std::vector<double>& closes) {
    std::vector<Bar> bars;
    bars.reserve(closes.size());
    double prev = closes.front();
    for (size_t i = 0; i < closes.size(); ++i) {
        Bar b;
        b.code = kCode;
        b.time = utils::addTradingDays(utils::parseDate("2026-01-01"),
                                       static_cast<int>(i));
        b.period = BarPeriod::Daily;
        b.open = prev;
        b.close = closes[i];
        b.high = std::max(b.open, b.close) + 0.2;
        b.low = std::min(b.open, b.close) - 0.2;
        b.volume = 100000;
        bars.push_back(b);
        prev = closes[i];
    }
    return bars;
}

std::vector<double> range(double from, double step, int n) {
    std::vector<double> v;
    v.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v.push_back(from + step * i);
    return v;
}

/// 追加一段序列到收盘列表
void appendRange(std::vector<double>& closes, double from, double step, int n) {
    const auto tail = range(from, step, n);
    closes.insert(closes.end(), tail.begin(), tail.end());
}

/// 手动驱动策略：逐根构造前缀历史 + 记录下单
class StrategyHarness {
public:
    explicit StrategyHarness(std::shared_ptr<IStrategy> s) : strategy_(std::move(s)) {
        portfolio_.cash = 100000.0;
        portfolio_.initialCapital = 100000.0;
        IStrategy::TradingApi api;
        api.placeOrder = [this](StockCode, Direction d, Volume v, Amount a) {
            orders_.push_back({d, v > 0 ? static_cast<double>(v) : a});
        };
        api.getPortfolio = [this]() -> const Portfolio& { return portfolio_; };
        api.getCurrentCode = [this]() -> const StockCode& { return code_; };
        strategy_->setTradingApi(api);
        strategy_->initialize();
    }

    /// 模拟持仓（卖出用例）
    void seedPosition(Volume qty, double cost) {
        Position pos;
        pos.code = kCode;
        pos.quantity = qty;
        pos.available = qty;
        pos.avgCost = cost;
        pos.currentPrice = cost;
        pos.marketValue = cost * qty;
        pos.costBasis = cost * qty;
        portfolio_.positions.push_back(pos);
    }

    /// 逐根驱动（每根重建前缀历史）
    void drive(const std::vector<Bar>& bars) {
        for (size_t k = 0; k < bars.size(); ++k) {
            BarSeries prefix(std::vector<Bar>(bars.begin(), bars.begin() + k + 1));
            StrategyContext ctx;
            ctx.currentCode = &code_;
            ctx.currentBar = &prefix.current();
            ctx.history = &prefix;
            ctx.portfolio = &portfolio_;
            ctx.period = BarPeriod::Daily;
            strategy_->onBar(ctx);
        }
    }

    bool hasBuy() const {
        for (const auto& [d, v] : orders_) if (d == Direction::Buy) return true;
        return false;
    }
    bool hasSell() const {
        for (const auto& [d, v] : orders_) if (d == Direction::Sell) return true;
        return false;
    }
    size_t orderCount() const { return orders_.size(); }

private:
    std::shared_ptr<IStrategy> strategy_;
    Portfolio portfolio_;
    StockCode code_ = kCode;
    std::vector<std::pair<Direction, double>> orders_;
};

// ---------- MomentumStrategy ----------

TEST(StrategyTemplateTest, MomentumBuysOnStrongGain) {
    // 横盘 40 根后急涨：最后一根 20 日涨幅 > 5%
    auto closes = range(10.0, 0.0, 40);
    appendRange(closes, 10.2, 0.04, 20);  // 40..59: 10.2 → 10.96
    StrategyHarness h(std::make_shared<MomentumStrategy>());
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasBuy());
}

TEST(StrategyTemplateTest, MomentumNoBuyBelowThreshold) {
    // 温和上涨 2% < 5% 阈值
    auto closes = range(10.0, 0.0, 40);
    appendRange(closes, 10.02, 0.01, 20);  // 末段 ~ +2%
    StrategyHarness h(std::make_shared<MomentumStrategy>());
    h.drive(makeBars(closes));
    EXPECT_FALSE(h.hasBuy());
}

TEST(StrategyTemplateTest, MomentumSellsBelowMa) {
    // 预置持仓；最后收盘跌破 MA10（下跌序列）
    auto closes = range(10.0, 0.0, 40);
    appendRange(closes, 10.0, -0.1, 15);  // 急跌
    StrategyHarness h(std::make_shared<MomentumStrategy>());
    h.seedPosition(1000, 10.0);
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasSell());
}

TEST(StrategyTemplateTest, MomentumInsufficientData) {
    StrategyHarness h(std::make_shared<MomentumStrategy>());
    h.drive(makeBars(range(10.0, 0.1, 15)));  // 15 根 < lookback+1
    EXPECT_EQ(h.orderCount(), 0u);
}

// ---------- BreakoutStrategy（收盘突破） ----------

TEST(StrategyTemplateTest, BreakoutBuysOnCloseHigh) {
    // 横盘 59 根，最后一根收盘突破前 20 根最高收盘
    auto closes = range(10.0, 0.0, 59);  // 恒平
    closes.push_back(10.9);              // 突破
    StrategyHarness h(std::make_shared<BreakoutStrategy>());
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasBuy());
}

TEST(StrategyTemplateTest, BreakoutNoBuyWithoutBreak) {
    auto closes = range(10.0, 0.0, 60);  // 恒平，无突破
    StrategyHarness h(std::make_shared<BreakoutStrategy>());
    h.drive(makeBars(closes));
    EXPECT_FALSE(h.hasBuy());
}

TEST(StrategyTemplateTest, BreakoutSellsOnCloseLow) {
    auto closes = range(10.0, -0.02, 60);  // 持续下跌，最后跌破前 10 根最低收盘
    StrategyHarness h(std::make_shared<BreakoutStrategy>());
    h.seedPosition(1000, 10.0);
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasSell());
}

TEST(StrategyTemplateTest, BreakoutInsufficientData) {
    StrategyHarness h(std::make_shared<BreakoutStrategy>());
    h.drive(makeBars(range(10.0, 0.1, 19)));  // 19 根 < entryPeriod+1
    EXPECT_EQ(h.orderCount(), 0u);
}

// ---------- MeanReversionStrategy ----------

TEST(StrategyTemplateTest, MeanReversionBuysOnOversold) {
    // 横盘 40 根后急跌 5 根（低于 MA20 3% 以上）
    auto closes = range(10.0, 0.0, 40);
    appendRange(closes, 9.9, -0.1, 5);  // 9.9 → 9.5
    StrategyHarness h(std::make_shared<MeanReversionStrategy>());
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasBuy());
}

TEST(StrategyTemplateTest, MeanReversionNoBuyNearMean) {
    auto closes = range(10.0, 0.0, 60);  // 恒平 → 无超跌
    StrategyHarness h(std::make_shared<MeanReversionStrategy>());
    h.drive(makeBars(closes));
    EXPECT_FALSE(h.hasBuy());
}

TEST(StrategyTemplateTest, MeanReversionSellsAboveMean) {
    // 预置持仓；先跌后涨回到均线上方
    auto closes = range(10.0, 0.0, 40);
    appendRange(closes, 9.6, 0.05, 20);  // 9.6 缓涨回均线
    StrategyHarness h(std::make_shared<MeanReversionStrategy>());
    h.seedPosition(1000, 10.0);
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasSell());
}

TEST(StrategyTemplateTest, MeanReversionInsufficientData) {
    StrategyHarness h(std::make_shared<MeanReversionStrategy>());
    h.drive(makeBars(range(10.0, 0.0, 19)));
    EXPECT_EQ(h.orderCount(), 0u);
}

// ---------- RsiStrategy ----------

TEST(StrategyTemplateTest, RsiBuysOnOversold) {
    // 先涨后暴跌 → RSI(14) < 30
    auto closes = range(10.0, 0.1, 40);
    appendRange(closes, 14.0, -0.3, 20);  // 14 → 8.3 暴跌
    StrategyHarness h(std::make_shared<RsiStrategy>());
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasBuy());
}

TEST(StrategyTemplateTest, RsiNoBuyWhenNeutral) {
    auto closes = range(10.0, 0.01, 60);  // 缓涨 RSI 中性
    StrategyHarness h(std::make_shared<RsiStrategy>());
    h.drive(makeBars(closes));
    EXPECT_FALSE(h.hasBuy());
}

TEST(StrategyTemplateTest, RsiSellsOnOverbought) {
    // 预置持仓；持续暴涨 → RSI > 70
    auto closes = range(10.0, 0.2, 40);
    StrategyHarness h(std::make_shared<RsiStrategy>());
    h.seedPosition(1000, 10.0);
    h.drive(makeBars(closes));
    EXPECT_TRUE(h.hasSell());
}

TEST(StrategyTemplateTest, RsiInsufficientData) {
    StrategyHarness h(std::make_shared<RsiStrategy>());
    h.drive(makeBars(range(10.0, 0.1, 10)));
    EXPECT_EQ(h.orderCount(), 0u);
}

}  // namespace
}  // namespace st
