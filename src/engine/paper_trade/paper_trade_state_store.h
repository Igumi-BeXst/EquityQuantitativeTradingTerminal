#pragma once

#include "engine/paper_trade/paper_trade_engine.h"
#include <string>
#include <vector>

namespace st {

/// 模拟交易完整状态（面板层：策略配置 + 引擎快照）
struct PaperTradeState {
    std::string strategyId;
    int p1 = 0;
    int p2 = 0;
    double capital = 0.0;
    double slippage = 0.0;
    std::vector<std::string> symbols;   // 股票 fullCode
    PaperTradeEngineState engine;
};

/// 模拟交易状态持久化 — JSON（configDir/paper_trade_state.json）
class PaperTradeStateStore {
public:
    /// 保存全量状态；失败返回 false
    bool save(const std::string& path, const PaperTradeState& state) const;

    /// 载入状态；文件缺失/损坏返回 false
    bool load(const std::string& path, PaperTradeState& state) const;
};

} // namespace st
