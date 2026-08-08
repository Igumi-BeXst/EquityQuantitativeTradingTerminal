#pragma once

#include "engine/journal/trade_journal.h"
#include <string>

namespace st {

/// 交易日志持久化 — JSON（configDir/trade_journal.json）
/// 条目数组 + 费率配置。文件缺失/损坏 → 空日志 + 默认费率。
class TradeJournalStore {
public:
    /// 载入 → 填充 engine（含指纹集重建）；失败返回 false 但 engine 为空
    bool load(const std::string& path, TradeJournalEngine& engine) const;

    /// 保存 engine 全量 + 费率；失败返回 false
    bool save(const std::string& path, const TradeJournalEngine& engine) const;

    /// 费率独立读写（可单独存 journal_config 或用同一文件）
    static FeeConfig loadFeeConfig(const std::string& path);
    static bool saveFeeConfig(const std::string& path, const FeeConfig& cfg);
};

} // namespace st
