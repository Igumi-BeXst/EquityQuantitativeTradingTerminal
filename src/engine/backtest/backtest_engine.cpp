#include "engine/backtest/backtest_engine.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <algorithm>
#include <map>
#include <exception>

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

    /// 新交易日 T+1 解冻：上一交易日买入 → available
    void settleT1(const DateTime& time) {
        const std::string date = utils::toDateString(time);
        if (currentDate_.empty()) {
            currentDate_ = date;
            return;
        }
        if (currentDate_ == date) return;
        for (auto& pos : positions_) {
            if (pos.todayBuy > 0) {
                pos.available += pos.todayBuy;
                pos.todayBuy = 0;
            }
        }
        currentDate_ = date;
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
            pos.todayBuy += trade.volume;
            pos.available = pos.quantity - pos.todayBuy;  // T+1：当日买入不可卖
        } else {
            // 卖出: 检查可用持仓（T+1）
            auto* pos = findPosition(trade.code);
            if (!pos || pos->available < trade.volume) {
                return false;  // 可用持仓不足
            }
            pos->quantity -= trade.volume;
            pos->available -= trade.volume;
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

    /// 净值（不拷贝持仓，网格搜索高频累积用）
    double netValue() const {
        double mv = 0.0;
        for (const auto& pos : positions_) {
            mv += pos.marketValue;
        }
        if (initialCapital_ <= 0) return 1.0;
        return (cash_ + mv) / initialCapital_;
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
    std::string currentDate_;  // 当前交易日（YYYY-MM-DD），用于 T+1 解冻
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
    equityCurve_.clear();
    equityDates_.clear();
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
            cachedPortfolio_ = account_->snapshot(DateTime{});
            return cachedPortfolio_;
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
            order.targetAmount = (vol <= 0 && amount > 0) ? amount : 0.0;
            order.strategyId = "strategy";
            order.createTime = currentTime_;
            order.updateTime = currentTime_;
            account_->submitOrder(std::move(order));
        };
        s->setTradingApi(std::move(api));
    }

    // 加载数据并构建按日期分组的全局时间轴
    // bar 存指针（指向 DataCache 的 shared_ptr<BarSeries>，缓存贯穿回测，安全）：
    // 全市场 5213 只 × 900 天避免每次组合拷贝 ~300MB
    struct DailyBar { StockCode code; const Bar* bar; };
    std::map<DateTime, std::vector<DailyBar>> timeline;

    for (const auto& code : config_.symbols) {
        const BarSeries* series = cache_ ? cache_->get(code, config_.period) : nullptr;
        if (!series) continue;
        // 过滤时间范围
        for (const auto& bar : *series) {
            if (bar.time >= config_.startDate && bar.time <= config_.endDate) {
                timeline[bar.time].push_back({code, &bar});
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

    // 每只股票到当前为止的累计历史（避免前视偏差）
    // BarSeries 内部 shared_ptr + append，O(1) 追加，避免每根 bar 整段拷贝
    std::map<StockCode, BarSeries> seriesByCode;

    // 聚宽兼容：用回测起始日之前的 K 线预填历史，保证策略指标从第一天起就有足够的 warm-up 数据；
    // 交易/净值仍只从 startDate 开始（timeline 已过滤），预填数据不会产生起始日前的成交。
    for (const auto& code : config_.symbols) {
        const BarSeries* series = cache_ ? cache_->get(code, config_.period) : nullptr;
        if (!series) continue;
        auto& dest = seriesByCode[code];
        for (const auto& bar : *series) {
            if (bar.time >= config_.startDate) break;  // 序列按时间升序
            dest.append(bar);
        }
    }

    // 基准指数（沪深300）按日期对齐：用于 Alpha/Beta 计算
    std::map<DateTime, double> benchByDate;
    for (const auto& b : config_.benchmarkBars) {
        if (b.close > 0) benchByDate[b.time] = b.close;
    }
    std::vector<double> benchmarkEquity;
    benchmarkEquity.reserve(timeline.size());
    double lastBenchClose = 0.0;
    bool hasBenchClose = false;

    // 不复权真实价（用于成交/市值，聚宽 use_real_price 兼容）
    std::map<std::string, std::map<DateTime, Bar>> rawByCodeDate;
    for (const auto& [code, bars] : config_.rawBars) {
        for (const auto& b : bars) {
            rawByCodeDate[code][b.time] = b;
        }
    }

    for (auto& [date, dayBars] : timeline) {
        // 0. T+1：新交易日先解冻上一交易日买入的持仓
        account_->settleT1(date);

        // 1. 策略基于“截至昨日”的 K 线产生信号（聚宽兼容：不含今日 bar）
        for (auto& db : dayBars) {
            currentCode_ = db.code;
            currentTime_ = db.bar->time;

            auto& series = seriesByCode[db.code];  // 此刻只含昨日及以前
            StrategyContext ctx;
            ctx.currentCode = &currentCode_;
            ctx.currentBar = series.empty() ? nullptr : &series.current();
            ctx.history = &series;
            ctx.period = config_.period;
            auto snap = account_->snapshot(date);
            ctx.portfolio = &snap;
            for (auto& s : strategies_) {
                try {
                    s->onBar(ctx);
                } catch (const std::exception& e) {
                    LogManager::instance()->log(LogLevel::Error,
                        "策略 onBar 异常: {}", e.what());
                    result_.error = std::string("策略异常: ") + e.what();
                    return result_;
                }
            }
        }

        // 2. 撮合今日开盘价：成交的是昨日收盘产生的信号
        for (auto& db : dayBars) {
            Price execOpen = db.bar->open;
            auto rawIt = rawByCodeDate.find(db.code.fullCode());
            if (rawIt != rawByCodeDate.end()) {
                auto rawDay = rawIt->second.find(date);
                if (rawDay != rawIt->second.end()) execOpen = rawDay->second.open;
            }
            matchOrders(db.code, execOpen);
        }

        // 3. 今日收盘后，把今日 bar 加入历史（供下一交易日使用）
        for (auto& db : dayBars) {
            seriesByCode[db.code].append(*db.bar);
        }

        // 4. 标记市值 + 记录净值快照（市值用真实价）
        for (auto& db : dayBars) {
            Price markClose = db.bar->close;
            auto rawIt = rawByCodeDate.find(db.code.fullCode());
            if (rawIt != rawByCodeDate.end()) {
                auto rawDay = rawIt->second.find(date);
                if (rawDay != rawIt->second.end()) {
                    // 数据源偶发把下一日收盘错标到当日：若 raw close 超出当日 low~high，
                    // 说明该 bar 数据异常，回退到信号缓存里的收盘价（K线显示口径）。
                    const Price rc = rawDay->second.close;
                    const Price rl = rawDay->second.low;
                    const Price rh = rawDay->second.high;
                    if (rc >= rl - 1e-6 && rc <= rh + 1e-6) {
                        markClose = rc;
                    }
                    // 否则保留 db.bar->close
                }
            }
            account_->markToMarket(db.code, markClose);
        }
        if (config_.keepEquitySnapshots) {
            result_.equitySnapshots.push_back(account_->snapshot(date));
        }
        // 净值曲线始终累积（网格搜索不存快照时仍可算绩效/曲线）
        equityCurve_.push_back(account_->netValue());
        equityDates_.push_back(date);

        // 基准净值对齐（缺失时向前借用最近一个基准收盘）
        {
            auto it = benchByDate.find(date);
            if (it != benchByDate.end()) {
                lastBenchClose = it->second;
                hasBenchClose = true;
            } else if (!hasBenchClose) {
                auto lb = benchByDate.lower_bound(date);
                if (lb != benchByDate.end()) {
                    lastBenchClose = lb->second;
                    hasBenchClose = true;
                }
            }
            benchmarkEquity.push_back(hasBenchClose ? lastBenchClose : 0.0);
        }

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
    result_.benchmarkEquity = benchmarkEquity;
    result_.equityDates = equityDates_;

    // 净值曲线
    std::vector<double> equity;
    if (!config_.keepEquitySnapshots) {
        equity = std::move(equityCurve_);   // 快照未存：直接用累积的净值曲线
    } else {
        for (const auto& snap : result_.equitySnapshots) {
            equity.push_back(snap.netValue());
        }
    }
    if (equity.empty()) equity.push_back(1.0);

    PerformanceCalculator::Input perfInput;
    perfInput.equity = equity;
    if (benchmarkEquity.size() == equity.size() &&
        std::all_of(benchmarkEquity.begin(), benchmarkEquity.end(),
                    [](double v) { return v > 0.0; })) {
        perfInput.benchmarkEquity = benchmarkEquity;
    }
    result_.performance = PerformanceCalculator::calculate(perfInput);

    // 交易明细按时间升序（同一时间按代码排序，保证展示顺序稳定）
    std::stable_sort(trades_.begin(), trades_.end(),
                     [](const Trade& a, const Trade& b) {
                         if (a.time != b.time) return a.time < b.time;
                         return a.code.fullCode() < b.code.fullCode();
                     });

    // 交易明细
    result_.trades = trades_;
    for (const auto& t : trades_) {
        result_.commissionTotal += t.commission;
        result_.totalFees += t.totalFee;
    }

    // 交易统计（winRate/profitFactor/trades/totalPnl）
    auto ts = PerformanceCalculator::computeTradeStats(result_.trades);
    result_.performance.totalTrades = ts.totalTrades;
    result_.performance.winningTrades = ts.winningTrades;
    result_.performance.winRate = ts.winRate;
    result_.performance.profitFactor = ts.profitFactor;
    result_.performance.totalPnl = result_.finalPortfolio.totalPnl;

    result_.success = true;
    return result_;
}

void BacktestEngine::matchOrders(const StockCode& code, Price openPrice) {
    auto& orders = account_->pendingOrders_;
    for (auto& entry : orders) {
        auto& order = entry.order;
        if (order.code != code || !order.isActive()) continue;
        if (order.volume <= 0 && order.targetAmount <= 0) continue;

        // 滑点：对齐聚宽默认 1 跳（买+0.01，卖-0.01）
        const Price slippage = config_.slippagePerShare;
        const Price fillPrice = order.direction == Direction::Buy
            ? openPrice + slippage : openPrice - slippage;

        // 计算最大可成交量
        Volume maxVol = order.volume;
        if (order.direction == Direction::Buy) {
            // 按金额下单：在成交价（今日开盘）处换算股数，并按 A 股 100 股一手向下取整
            if (order.targetAmount > 0) {
                Volume amountVol = static_cast<Volume>(order.targetAmount / openPrice);
                amountVol = (amountVol / 100) * 100;
                order.volume = amountVol;   // 让 unfilled()/部分成交逻辑正常
            }
            maxVol = order.volume;

            // 资金限制（按含滑点成交价）
            Amount available = account_->cash();
            if (available <= 0) {
                order.status = OrderStatus::Rejected;
                continue;
            }
            maxVol = std::min(maxVol, static_cast<Volume>(available / fillPrice));
            maxVol = (maxVol / 100) * 100;  // 买入必须是 100 股整数倍
        } else {
            // T+1：仅可用持仓可卖
            auto& pos = account_->positionOf(code);
            maxVol = pos.available;
        }
        if (maxVol <= 0) continue;

        auto match = matcher_->match(order, fillPrice, maxVol);
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
