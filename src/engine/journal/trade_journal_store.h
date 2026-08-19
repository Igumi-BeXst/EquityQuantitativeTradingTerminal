#pragma once

#include "engine/journal/trade_journal.h"
#include <string>

namespace st {

/// 交易日志持久化 — JSON（configDir/trade_journal.json）
/// 条目数组 + 费率配置。文件缺失/损坏 → 空日志 + 默认费率。
class TradeJournalStore {
public:
    /// 默认保留的最大条目数，超出部分自动归档到 path.archive
    static constexpr size_t kDefaultMaxEntries = 50000;

    /// 载入 → 填充 engine（含指纹集重建）；失败返回 false 但 engine 为空
    bool load(const std::string& path, TradeJournalEngine& engine) const;

    /// 保存 engine 全量 + 费率；失败返回 false。
    /// 超过 maxEntries 时，最旧的多余条目自动归档到 <path>.archive（按 id 去重）。
    bool save(const std::string& path, const TradeJournalEngine& engine,
              size_t maxEntries = kDefaultMaxEntries) const;

    /// 费率独立读写（可单独存 journal_config 或用同一文件）
    static FeeConfig loadFeeConfig(const std::string& path);
    static bool saveFeeConfig(const std::string& path, const FeeConfig& cfg);
};

} // namespace st
