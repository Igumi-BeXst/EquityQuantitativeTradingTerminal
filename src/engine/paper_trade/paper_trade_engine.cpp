#include "engine/paper_trade/paper_trade_engine.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <algorithm>

namespace st {

PaperTradeEngine::PaperTradeEngine()
    : matcher_(std::make_unique<OrderMatcher>()) {}

PaperTradeEngine::~PaperTradeEngine() = default;

void PaperTradeEngine::setConfig(const PaperTradeConfig& config) {
    config_ = config;
    feeCalculator_.setConfig(config.feeConfig);
}

void PaperTradeEngine::addStrategy(std::shared_ptr<IStrategy> strategy) {
    strategies_.push_back(std::move(strategy));
    if (!portfolio_.initialCapital) {
        portfolio_.initialCapital = config_.initialCapital;
        portfolio_.cash = config_.initialCapital;
        portfolio_.totalAsset = config_.initialCapital;
    }
}

void PaperTradeEngine::addStrategy(const StockCode& code,
                                   std::shared_ptr<IStrategy> strategy) {
    boundStrategies_[code.fullCode()].push_back(std::move(strategy));
    if (!portfolio_.initialCapital) {
        portfolio_.initialCapital = config_.initialCapital;
        portfolio_.cash = config_.initialCapital;
        portfolio_.totalAsset = config_.initialCapital;
    }
}

void PaperTradeEngine::seedHistory(const StockCode& code, std::vector<Bar> bars) {
    history_[code.fullCode()] = BarSeries(std::move(bars));
}

void PaperTradeEngine::start() {
    if (running_) return;
    running_ = true;
    LogManager::instance()->log(LogLevel::Info, "PaperTradeEngine started, capital={}",
                                config_.initialCapital);

    auto initStrategy = [this](std::shared_ptr<IStrategy>& s) {
        s->initialize();
        IStrategy::TradingApi api;
        api.getPortfolio = [this]() -> const Portfolio& { return portfolio_; };
        api.getCurrentCode = [this]() -> const StockCode& {
            return currentCode_;
        };
        api.placeOrder = [this](StockCode code, Direction dir, Volume vol, Amount amount) {
            // buyByAmount 传 vol=0 + amount → 按该股票最近报价换算股数
            if (vol <= 0 && amount > 0) {
                auto it = lastPrices_.find(code.fullCode());
                if (it != lastPrices_.end() && it->second > 0) {
                    vol = static_cast<Volume>(amount / it->second);
                }
            }
            submitOrder(code, dir, vol);
        };
        s->setTradingApi(std::move(api));
        s->onStart();
    };

    for (auto& s : strategies_) initStrategy(s);
    for (auto& [code, list] : boundStrategies_) {
        (void)code;
        for (auto& s : list) initStrategy(s);
    }
}

void PaperTradeEngine::stop() {
    if (!running_) return;
    running_ = false;
    for (auto& s : strategies_) {
        s->onStop();
    }
    for (auto& [code, list] : boundStrategies_) {
        (void)code;
        for (auto& s : list) s->onStop();
    }
    LogManager::instance()->log(LogLevel::Info, "PaperTradeEngine stopped");
}

PaperTradeEngineState PaperTradeEngine::capture() const {
    PaperTradeEngineState state;
    state.initialCapital = portfolio_.initialCapital;
    state.cash = portfolio_.cash;
    state.currentTradeDate = currentTradeDate_;
    state.positions = portfolio_.positions;
    state.trades = trades_;
    for (const auto& [code, series] : history_) {
        std::vector<Bar> bars;
        bars.reserve(series.size());
        for (const auto& b : series) bars.push_back(b);
        state.history[code] = std::move(bars);
    }
    return state;
}

void PaperTradeEngine::restore(const PaperTradeEngineState& state) {
    portfolio_.initialCapital = state.initialCapital;
    portfolio_.cash = state.cash;
    portfolio_.positions = state.positions;

    double mv = 0.0, cost = 0.0;
    for (const auto& pos : portfolio_.positions) {
        mv += pos.marketValue;
        cost += pos.costBasis;
    }
    portfolio_.marketValue = mv;
    portfolio_.totalCost = cost;
    portfolio_.totalAsset = portfolio_.cash + mv;
    portfolio_.totalPnl = portfolio_.totalAsset - portfolio_.initialCapital;
    portfolio_.totalPnlPct = portfolio_.initialCapital > 0
        ? portfolio_.totalPnl / portfolio_.initialCapital * 100.0 : 0.0;

    trades_ = state.trades;
    currentTradeDate_ = state.currentTradeDate;
    nextOrderId_ = static_cast<int>(trades_.size()) + 1;

    history_.clear();
    for (const auto& [code, bars] : state.history) {
        history_[code] = BarSeries(bars);
    }
    pendingOrders_.clear();
    lastPrices_.clear();
}

void PaperTradeEngine::settleT1(const DateTime& time) {
    const std::string date = utils::toDateString(time);
    if (currentTradeDate_.empty()) {
        currentTradeDate_ = date;
        return;
    }
    if (currentTradeDate_ == date) return;

    // 进入新交易日：将上一交易日的当日买入转为可卖
    for (auto& pos : portfolio_.positions) {
        if (pos.todayBuy > 0) {
            pos.available += pos.todayBuy;
            pos.todayBuy = 0;
        }
    }
    currentTradeDate_ = date;
}

void PaperTradeEngine::submitOrder(StockCode code, Direction dir, Volume vol) {
    if (vol <= 0) return;

    Order order;
    order.id = "P" + std::to_string(nextOrderId_++);
    order.code = code;
    order.direction = dir;
    order.type = OrderType::Market;
    order.volume = vol;
    order.createTime = DateTime{};

    // 模拟交易：订单立即可执行，等待该股票行情价格
    pendingOrders_[code.fullCode()] = order;
}

std::vector<std::shared_ptr<IStrategy>>
PaperTradeEngine::strategiesFor(const StockCode& code) const {
    auto it = boundStrategies_.find(code.fullCode());
    if (it != boundStrategies_.end()) return it->second;
    return strategies_;   // 未绑定 → 全部策略（单股票兼容）
}

void PaperTradeEngine::onQuote(const StockCode& code, Price price, DateTime time) {
    if (!running_ || price <= 0) return;

    // T+1：进入新交易日时先解冻上一交易日买入的持仓
    settleT1(time);

    currentCode_ = code;
    lastPrices_[code.fullCode()] = price;

    // 实时报价按日线聚合：同一交易日只保留/更新一根日线，避免 3 秒 tick 被当成多根 K 线
    auto& series = history_[code.fullCode()];
    const std::string date = utils::toDateString(time);
    if (series.empty() || utils::toDateString(series.current().time) != date) {
        Bar bar;
        bar.code = code;
        bar.time = time;
        bar.open = bar.high = bar.low = bar.close = price;
        series.append(bar);
    } else {
        Bar& b = series.back();
        b.high = std::max(b.high, price);
        b.low = std::min(b.low, price);
        b.close = price;
    }

    // 驱动该股票的策略 onBar，策略可能下单
    StrategyContext ctx;
    ctx.currentCode = &code;
    ctx.currentBar = &series.current();
    ctx.history = &series;
    ctx.portfolio = &portfolio_;
    for (auto& s : strategiesFor(code)) {
        s->onBar(ctx);
    }

    // 立即以当前价执行该股票挂起的订单（模拟交易即时成交）
    auto it = pendingOrders_.find(code.fullCode());
    if (it != pendingOrders_.end()) {
        const auto& order = it->second;
        executeTrade(code, order.direction, order.volume, price, time);
        pendingOrders_.erase(it);
    }
}

void PaperTradeEngine::executeTrade(StockCode code, Direction dir, Volume vol,
                                    Price price, DateTime time) {
    // 滑点
    double slip = config_.slippage;
    Price execPrice = (dir == Direction::Buy)
        ? price * (1.0 + slip)
        : price * (1.0 - slip);

    Trade trade;
    trade.id = "T" + std::to_string(nextOrderId_++);
    trade.code = code;
    trade.direction = dir;
    trade.price = execPrice;
    trade.volume = vol;
    trade.amount = execPrice * vol;
    trade.time = time;

    // 费用
    auto fees = feeCalculator_.calculate(trade);
    trade.commission = fees.commission;
    trade.stampTax = fees.stampTax;
    trade.otherFees = fees.transferFee + fees.handlingFee + fees.regulatoryFee + fees.customFees;
    trade.totalFee = fees.total;

    // 更新组合
    if (dir == Direction::Buy) {
        if (trade.amount + trade.totalFee > portfolio_.cash) return;  // 资金不足
        portfolio_.cash -= (trade.amount + trade.totalFee);
        auto* pos = findPosition(code);
        if (!pos) {
            portfolio_.positions.push_back({});
            pos = &portfolio_.positions.back();
            pos->code = code;
        }
        pos->quantity += vol;
        pos->todayBuy += vol;
        pos->costBasis += trade.amount;
        pos->avgCost = pos->quantity > 0 ? pos->costBasis / pos->quantity : 0.0;
        pos->available = pos->quantity - pos->todayBuy;  // T+1：当日买入不可卖
    } else {
        auto* pos = findPosition(code);
        if (!pos || pos->available < vol) return;  // T+1：仅可用持仓可卖
        pos->quantity -= vol;
        pos->available -= vol;
        if (pos->quantity <= 0) {
            pos->costBasis = 0.0;
            pos->avgCost = 0.0;
        } else {
            pos->costBasis *= static_cast<double>(pos->quantity) /
                              static_cast<double>(pos->quantity + vol);
        }
        portfolio_.cash += (trade.amount - trade.totalFee);
    }

    // 更新市值
    for (auto& pos : portfolio_.positions) {
        if (pos.code == code) {
            pos.currentPrice = price;
            pos.marketValue = pos.quantity * price;
            pos.profitLoss = pos.marketValue - pos.costBasis;
            pos.profitLossPct = pos.costBasis > 0
                ? pos.profitLoss / pos.costBasis * 100.0 : 0.0;
        }
    }

    // 组合汇总
    double mv = 0.0, cost = 0.0;
    for (const auto& pos : portfolio_.positions) {
        mv += pos.marketValue;
        cost += pos.costBasis;
    }
    portfolio_.marketValue = mv;
    portfolio_.totalCost = cost;
    portfolio_.totalAsset = portfolio_.cash + mv;
    portfolio_.totalPnl = portfolio_.totalAsset - portfolio_.initialCapital;
    portfolio_.totalPnlPct = portfolio_.initialCapital > 0
        ? portfolio_.totalPnl / portfolio_.initialCapital * 100.0 : 0.0;
    portfolio_.snapshotTime = time;

    trades_.push_back(trade);
    if (onTrade_) onTrade_(trade);
    LogManager::instance()->log(LogLevel::Info, "PaperTrade: {} {} x{} @ {}",
                                dir == Direction::Buy ? "BUY" : "SELL",
                                code.fullCode(), vol, execPrice);
}

Position* PaperTradeEngine::findPosition(const StockCode& code) {
    for (auto& pos : portfolio_.positions) {
        if (pos.code == code) return &pos;
    }
    return nullptr;
}

} // namespace st
