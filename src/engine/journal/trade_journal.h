#pragma once

#include "foundation/enums.h"
#include "foundation/order.h"
#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "engine/backtest/fee_calculator.h"
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace st {

/// 交易日志条目 — 模拟自动落库(AutoTrade) / 手动实盘录入(ManualNote)
struct JournalEntry {
    std::string  id;             // 唯一 ID（内部生成 "J" + 序号）
    StockCode    code;
    std::string  name;
    JournalType  type = JournalType::ManualNote;
    Direction    direction = Direction::Buy;
    Price        price = 0.0;
    Volume       volume = 0;
    Amount       fees = 0.0;     // 总费用（含佣金+印花税+过户费等）
    std::string  strategy;       // 关联策略名（可空）
    std::string  note;           // 注解
    DateTime     time;
};

/// 单组交易统计（总体 / 模拟 / 实盘 / 单个策略 通用）
struct GroupStats {
    int    count = 0;
    int    wins = 0;
    int    losses = 0;
    double winRate = 0.0;      // 0..1
    double totalPnl = 0.0;     // 已实现 + 浮动（简化：按买卖配对计算）
    double profitFactor = 0.0; // 盈利总额/亏损总额
    double maxDrawdown = 0.0;  // 累计盈亏曲线最大回撤（金额）
    std::vector<double> cumPnl; // 累计盈亏序列（按时间升序）
};

/// 一笔已实现盈亏（按 code 买卖 FIFO 配对）
struct RealizedPnl {
    StockCode code;
    std::string name;
    double pnl = 0.0;           // 已实现盈亏（卖出时结算）
    double pnlPct = 0.0;        // 相对买入成本的百分比
    int    roundTrips = 0;      // 完成的买卖回合数
};

struct MonthlyPnl {
    std::string ym;             // "2026-08"
    double pnl = 0.0;
};

/// 逐笔配对（模拟 ↔ 实盘）
struct PairRow {
    StockCode code;
    Direction direction = Direction::Buy;
    double    priceDiff = 0.0;   // 实盘价 − 模拟价
    double    diffPct = 0.0;     // 价差/模拟价 × 100
    Price     simPrice = 0.0;
    Price     manualPrice = 0.0;
    Volume    matchedVol = 0;
    DateTime  simTime;
    DateTime  manualTime;
};

/// 全量统计
struct JournalStats {
    GroupStats overall;
    GroupStats sim;             // 模拟（AutoTrade）
    GroupStats manual;          // 实盘（ManualNote）
    std::vector<PairRow> pairs;      // 精确配对结果（§Task 3）
    std::vector<RealizedPnl> realized; // 持仓/已实现盈亏
    std::vector<MonthlyPnl> monthly;  // 月度收益
    std::vector<std::pair<std::string, GroupStats>> byStrategy; // 按策略
};

/// 纯函数统计 — 不依赖 UI/线程，可单测
JournalStats computeStats(const std::vector<JournalEntry>& entries);

/// 交易日志引擎 — 增删改查 + 持久化入口 + 模拟成交自动落库
class TradeJournalEngine {
public:
    TradeJournalEngine() = default;

    /// CRUD（手动录入路径；id 由引擎生成）
    std::string addEntry(const JournalEntry& e);      // 返回生成 id
    bool updateEntry(const std::string& id, const JournalEntry& e);
    bool removeEntry(const std::string& id);
    void clear();
    const std::vector<JournalEntry>& entries() const { return entries_; }

    /// 费率（手动录入自动算费用用）— 线程安全（Task 9 运行时改费率）
    void setFees(const FeeConfig& cfg) { std::lock_guard<std::mutex> lock(mutex_); fees_ = cfg; }
    FeeConfig fees() const { std::lock_guard<std::mutex> lock(mutex_); return fees_; }

    /// 模拟成交自动落库 — 供 PaperTradeEngine.onTrade 回调（线程安全）
    /// strategy 为当前模拟策略名；内部指纹去重，重复则跳过返回空 id
    std::string appendAuto(const Trade& t, const std::string& strategy);

    /// 指纹 = FNV-1a(代码|时间|方向|数量) — 进程内去重
    std::string entryFingerprint(const JournalEntry& e) const;

    /// 载入恢复 — 保留原 id + 重建指纹集 + 更新 nextId_（供 Store::load）
    void restoreEntries(const std::vector<JournalEntry>& entries);

private:
    std::vector<JournalEntry> entries_;
    std::set<std::string> fingerprints_;   // 已入库指纹（防重）
    FeeConfig fees_ = standardFees();
    int nextId_ = 1;
    mutable std::mutex mutex_;

    /// 日志标准 A 股费率 — 佣金万2.5/最低5 + 印花税卖出0.0005(2023减半后) + 过户费双向0.00002
    /// 不用 FeeConfig::defaultAShare()（其印花税仍为 0.001 旧默认，见设计文档）
    static FeeConfig standardFees() {
        FeeConfig cfg;                       // 继承默认：佣金0.00025/最低5/过户0.00002
        cfg.stampTaxRate = 0.0005;           // 修正印花税为 2023 减半后标准
        return cfg;
    }
};

} // namespace st
