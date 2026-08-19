#pragma once

#include "foundation/types.h"
#include "foundation/enums.h"
#include "foundation/stock_code.h"
#include <vector>
#include <string>

namespace st {

/// Position in a single stock
struct Position {
    StockCode  code;
    Volume     quantity   = 0;       // 持仓数量（股）
    Price      avgCost    = 0.0;     // 平均成本价
    Price      currentPrice = 0.0;   // 当前市价
    Amount     marketValue = 0.0;    // 市值
    Amount     costBasis   = 0.0;    // 成本总额
    Amount     profitLoss  = 0.0;    // 浮动盈亏
    Percentage profitLossPct = 0.0;  // 盈亏比例(%)
    Volume     available   = 0;      // 可用数量（可卖）
    Volume     todayBuy    = 0;      // 当日买入数量（T+1 不可卖）
    int        holdDays    = 0;      // 持仓天数

    [[nodiscard]] bool hasPosition() const { return quantity > 0; }
};

/// Portfolio snapshot
struct Portfolio {
    Amount     totalAsset  = 0.0;   // 总资产
    Amount     cash        = 0.0;   // 可用资金
    Amount     frozenCash  = 0.0;   // 冻结资金
    Amount     marketValue = 0.0;   // 持仓市值
    Amount     totalCost   = 0.0;   // 总成本
    Amount     totalPnl    = 0.0;   // 总盈亏
    Percentage totalPnlPct = 0.0;   // 总盈亏比例
    Amount     initialCapital = 0.0; // 初始资金（回测/模拟用）
    DateTime   snapshotTime;

    std::vector<Position> positions;

    [[nodiscard]] double netValue() const {
        if (initialCapital <= 0) return 1.0;
        return totalAsset / initialCapital;
    }

    [[nodiscard]] Amount available() const { return cash; }

    /// Find position by code
    const Position* find(const StockCode& code) const {
        for (auto& p : positions) {
            if (p.code == code) return &p;
        }
        return nullptr;
    }
};

} // namespace st
