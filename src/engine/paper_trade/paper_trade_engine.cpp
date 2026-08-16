#include "engine/paper_trade/paper_trade_engine.h"
#include "core/log_manager.h"
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

    currentCode_ = code;
    lastPrices_[code.fullCode()] = price;

    // 用当前报价构造 bar，累积历史供趋势策略计算均线
    Bar bar;
    bar.code = code;
    bar.time = time;
    bar.open = bar.high = bar.low = bar.close = price;

    auto& series = history_[code.fullCode()];
    series.append(bar);

    // 驱动该股票的策略 onBar，策略可能下单
    StrategyContext ctx;
    ctx.currentCode = &code;
    ctx.currentBar = &bar;
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
        pos->costBasis += trade.amount;
        pos->avgCost = pos->quantity > 0 ? pos->costBasis / pos->quantity : 0.0;
        pos->available = pos->quantity;
    } else {
        auto* pos = findPosition(code);
        if (!pos || pos->quantity < vol) return;  // 持仓不足
        pos->quantity -= vol;
        pos->available = pos->quantity;
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
