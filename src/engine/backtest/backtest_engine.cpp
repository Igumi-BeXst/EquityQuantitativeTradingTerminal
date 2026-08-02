#include "engine/backtest/backtest_engine.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <algorithm>
#include <map>

namespace st {

// ============================================================
// 内部账户 — 管理资金和持仓
// ============================================================
class BacktestEngine::Account {
public:
    explicit Account(Amount initialCapital) : cash_(initialCapital),
        initialCapital_(initialCapital) {}

    Amount cash() const { return cash_; }
    Amount initialCapital() const { return initialCapital_; }

    struct OrderBookEntry {
        Order order;
        Volume remaining;  // 剩余待成交数量
    };
    std::vector<OrderBookEntry> pendingOrders_;

    /// 下单（市价单登记，下一Bar撮合）
    void submitOrder(Order order) {
        order.status = OrderStatus::Pending;
        pendingOrders_.push_back({std::move(order), order.volume});
    }

    /// 持仓查询/更新
    Position& positionOf(const StockCode& code) {
        for (auto& pos : positions_) {
            if (pos.code == code) return pos;
        }
        positions_.push_back(Position{});
        positions_.back().code = code;
        return positions_.back();
    }

    const std::vector<Position>& positions() const { return positions_; }

    /// 成交后更新持仓和资金
    /// 返回 true 表示有效成交
    bool applyTrade(const Trade& trade) {
        if (trade.direction == Direction::Buy) {
            // 买入: 检查资金
            Amount cost = trade.amount + trade.totalFee;
            if (cost > cash_ + 1e-6) {
                return false;  // 资金不足
            }
            cash_ -= cost;

            auto& pos = positionOf(trade.code);
            Amount newCost = pos.costBasis + trade.amount;
            Volume newQty = pos.quantity + trade.volume;
            pos.avgCost = newQty > 0 ? newCost / newQty : 0.0;
            pos.quantity = newQty;
            pos.costBasis = newCost;
            pos.available = pos.quantity;
        } else {
            // 卖出: 检查持仓
            auto* pos = findPosition(trade.code);
            if (!pos || pos->quantity < trade.volume) {
                return false;  // 持仓不足
            }
            pos->quantity -= trade.volume;
            pos->available = pos->quantity;
            if (pos->quantity <= 0) {
                // 清仓，重置成本
                pos->costBasis = 0.0;
                pos->avgCost = 0.0;
            } else {
                // 按比例减少成本
                pos->costBasis *= static_cast<double>(pos->quantity) /
                                  static_cast<double>(pos->quantity + trade.volume);
            }
            cash_ += trade.amount - trade.totalFee;
        }
        return true;
    }

    /// 标记持仓当前价格，更新市值和浮动盈亏
    void markToMarket(const StockCode& code, Price price) {
        auto* pos = findPosition(code);
        if (!pos) return;
        pos->currentPrice = price;
        pos->marketValue = pos->quantity * price;
        pos->profitLoss = pos->marketValue - pos->costBasis;
        pos->profitLossPct = pos->costBasis > 0
            ? pos->profitLoss / pos->costBasis * 100.0 : 0.0;
    }

    /// 组合快照
    Portfolio snapshot(const DateTime& time) const {
        Portfolio pf;
        pf.initialCapital = initialCapital_;
        pf.snapshotTime = time;
        pf.cash = cash_;
        pf.positions = positions_;
        double mv = 0.0, cost = 0.0;
        for (const auto& pos : positions_) {
            mv += pos.marketValue;
            cost += pos.costBasis;
        }
        pf.marketValue = mv;
        pf.totalCost = cost;
        pf.totalAsset = cash_ + mv;
        pf.totalPnl = pf.totalAsset - initialCapital_;
        pf.totalPnlPct = initialCapital_ > 0
            ? pf.totalPnl / initialCapital_ * 100.0 : 0.0;
        return pf;
    }

private:
    Position* findPosition(const StockCode& code) {
        for (auto& pos : positions_) {
            if (pos.code == code) return &pos;
        }
        return nullptr;
    }

    Amount cash_;
    Amount initialCapital_;
    std::vector<Position> positions_;
};

// ============================================================
// BacktestEngine
// ============================================================
BacktestEngine::BacktestEngine()
    : matcher_(std::make_unique<OrderMatcher>()) {}

BacktestEngine::~BacktestEngine() = default;

void BacktestEngine::setConfig(const BacktestConfig& config) {
    config_ = config;
    feeCalculator_.setConfig(config.feeConfig);
}

void BacktestEngine::addStrategy(std::shared_ptr<IStrategy> strategy) {
    strategies_.push_back(std::move(strategy));
}

void BacktestEngine::initStrategies() {
    for (auto& s : strategies_) {
        s->initialize();
    }
}

BacktestResult BacktestEngine::run() {
    result_ = BacktestResult{};
    if (config_.symbols.empty() || !cache_) {
        result_.error = "股票池为空或未设置数据源";
        return result_;
    }

    account_ = std::make_unique<Account>(config_.initialCapital);
    initStrategies();

    // 为每个策略注入交易 API
    for (auto& s : strategies_) {
        IStrategy::TradingApi api;
        api.getPortfolio = [this]() -> const Portfolio& {
            static Portfolio cached;
            cached = account_->snapshot(DateTime{});
            return cached;
        };
        api.getCurrentCode = [this]() -> const StockCode& {
            return currentCode_;
        };
        api.placeOrder = [this](StockCode code, Direction dir, Volume vol, Amount amount) {
            Order order;
            order.id = "B" + std::to_string(nextOrderId_++);
            order.code = code;
            order.direction = dir;
            order.type = OrderType::Market;
            order.volume = vol;
            order.strategyId = "strategy";
            order.createTime = DateTime{};
            if (vol <= 0 && amount > 0) {
                // 按金额下单: 用最近价估算股数（撮合时精确计算）
                order.volume = static_cast<Volume>(amount / 10.0);  // 暂存
            }
            account_->submitOrder(std::move(order));
        };
        s->setTradingApi(std::move(api));
    }

    // 加载数据并构建按日期分组的全局时间轴
    struct DailyBar { StockCode code; Bar bar; };
    std::map<DateTime, std::vector<DailyBar>> timeline;

    for (const auto& code : config_.symbols) {
        auto bars = cache_->getBars(code, config_.period);
        // 过滤时间范围
        for (auto& bar : bars) {
            if (bar.time >= config_.startDate && bar.time <= config_.endDate) {
                timeline[bar.time].push_back({code, bar});
            }
        }
    }

    if (timeline.empty()) {
        result_.error = "时间范围内无数据";
        return result_;
    }

    result_.barCount = static_cast<int>(timeline.size());
    int processed = 0;
    const int total = static_cast<int>(timeline.size());

    for (auto& [date, dayBars] : timeline) {
        // 1. 每个股票：通知策略 onBar（先看当前 bar）
        for (auto& db : dayBars) {
            currentCode_ = db.code;
            StrategyContext ctx;
            ctx.currentCode = &currentCode_;
            ctx.currentBar = &db.bar;
            ctx.period = config_.period;
            auto snap = account_->snapshot(date);
            ctx.portfolio = &snap;
            for (auto& s : strategies_) {
                s->onBar(ctx);
            }
        }

        // 2. 撮合昨日订单：以下一Bar开盘价成交
        for (auto& db : dayBars) {
            matchOrders(db.code, db.bar.open);
        }

        // 3. 标记市值 + 记录净值快照
        for (auto& db : dayBars) {
            account_->markToMarket(db.code, db.bar.close);
        }
        result_.equitySnapshots.push_back(account_->snapshot(date));

        // 进度
        processed++;
        if (progressCb_) {
            progressCb_(static_cast<double>(processed) / total * 100.0);
        }
    }

    // 停止策略
    for (auto& s : strategies_) {
        s->onStop();
    }

    // 生成结果
    result_.finalPortfolio = account_->snapshot(DateTime{});

    // 净值曲线
    std::vector<double> equity;
    for (const auto& snap : result_.equitySnapshots) {
        equity.push_back(snap.netValue());
    }
    if (equity.empty()) equity.push_back(1.0);

    PerformanceCalculator::Input perfInput;
    perfInput.equity = equity;
    result_.performance = PerformanceCalculator::calculate(perfInput);

    // 交易明细
    result_.trades = trades_;
    for (const auto& t : trades_) {
        result_.commissionTotal += t.commission;
        result_.totalFees += t.totalFee;
    }

    result_.success = true;
    return result_;
}

void BacktestEngine::matchOrders(const StockCode& code, Price openPrice) {
    auto& orders = account_->pendingOrders_;
    for (auto& entry : orders) {
        auto& order = entry.order;
        if (order.code != code || !order.isActive()) continue;
        if (order.volume <= 0) continue;

        // 计算最大可成交量
        Volume maxVol = order.volume;
        if (order.direction == Direction::Buy) {
            // 资金限制
            Amount available = account_->cash();
            if (available <= 0) {
                order.status = OrderStatus::Rejected;
                continue;
            }
            maxVol = static_cast<Volume>(available / openPrice);
        } else {
            // 持仓限制
            auto& pos = account_->positionOf(code);
            maxVol = pos.quantity;
        }
        if (maxVol <= 0) continue;

        auto match = matcher_->match(order, openPrice, maxVol);
        if (match.filled) {
            auto trade = matcher_->buildTrade(order, match.fillPrice, match.fillVolume);
            // 计算费用
            auto fees = feeCalculator_.calculate(trade);
            trade.commission = fees.commission;
            trade.stampTax = fees.stampTax;
            trade.otherFees = fees.transferFee + fees.handlingFee + fees.regulatoryFee + fees.customFees;
            trade.totalFee = fees.total;

            if (account_->applyTrade(trade)) {
                order.filledVol += trade.volume;
                order.avgFillPrice = trade.price;
                order.status = match.newStatus;
                trades_.push_back(trade);
            }
        }
    }
}

void BacktestEngine::updatePortfolio(const StockCode&) {
    // 预留
}

Portfolio BacktestEngine::snapshot() const {
    return account_->snapshot(DateTime{});
}

} // namespace st
