#pragma once

#include "foundation/stock_code.h"
#include "foundation/stock_info.h"
#include "foundation/bar.h"
#include "foundation/types.h"
#include "data/adjustment_data_provider.h"
#include <QSqlDatabase>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <memory>

namespace st {

/// SQLite 数据仓储 — 持久化 K线、股票信息、同步状态
/// 所有数据通过 Qt6::Sql 访问 SQLite
/// 历史行情缓存由 CachedDataProvider 访问；每个线程使用自己的 SQL 连接。
class DataRepository {
public:
    DataRepository();
    ~DataRepository();

    /// 打开数据库并创建表结构
    bool init(const std::string& dbPath);

    /// 返回当前线程专属的连接；不同线程永不共享 QSqlDatabase。
    static DataRepository& forCurrentThread(const std::string& dbPath);

    struct CoverageRange {
        DateTime start{};
        DateTime end{};
    };

    struct BarSyncState {
        std::optional<DateTime> lastCurrentSuccessAt;
        std::optional<DateTime> lastCurrentAttemptAt;
        bool lastCurrentAttemptSucceeded = false;
    };

    struct AdjustmentSnapshot {
        std::vector<AdjustmentEvent> events;
        std::string version;
        DateTime fetchedAt{};
    };

    /// 新的来源/复权口径隔离缓存 API。旧表 API 保留以兼容现有调用。
    std::vector<Bar> loadCachedBars(const std::string& sourceId,
                                    const StockCode& code, BarPeriod period,
                                    BarPriceMode mode, DateTime start, DateTime end);
    bool saveCachedBars(const std::string& sourceId, BarPriceMode mode,
                        const std::vector<Bar>& bars);
    std::vector<CoverageRange> loadBarCoverage(const std::string& sourceId,
                                               const StockCode& code,
                                               BarPeriod period, BarPriceMode mode);
    bool addBarCoverage(const std::string& sourceId, const StockCode& code,
                        BarPeriod period, BarPriceMode mode,
                        DateTime start, DateTime end, DateTime syncedAt);
    BarSyncState loadBarSyncState(const std::string& sourceId, const StockCode& code,
                                  BarPeriod period, BarPriceMode mode);
    void recordBarSyncAttempt(const std::string& sourceId, const StockCode& code,
                              BarPeriod period, BarPriceMode mode,
                              DateTime attemptedAt, bool success,
                              bool currentWindow);
    std::optional<AdjustmentSnapshot> loadAdjustmentSnapshot(
        const std::string& sourceId, const StockCode& code);
    bool saveAdjustmentSnapshot(const std::string& sourceId, const StockCode& code,
                                const std::vector<AdjustmentEvent>& events,
                                const std::string& version, DateTime fetchedAt);

    bool saveReferenceInfos(const std::string& sourceId, const std::string& kind,
                            const std::vector<StockInfo>& infos);
    std::vector<StockInfo> loadReferenceInfos(const std::string& sourceId,
                                              const std::string& kind,
                                              Market market = Market::Unknown);
    std::optional<StockInfo> loadReferenceInfo(const std::string& sourceId,
                                               const std::string& kind,
                                               const StockCode& code);
    std::optional<DateTime> getReferenceSyncTime(const std::string& sourceId,
                                                 const std::string& kind,
                                                 Market market = Market::Unknown);
    void updateReferenceSyncTime(const std::string& sourceId, const std::string& kind,
                                 Market market, DateTime syncedAt);

    /// 股票信息
    void saveStockInfos(const std::vector<StockInfo>& infos);
    std::vector<StockInfo> loadStockInfos();
    std::optional<StockInfo> loadStockInfo(const StockCode& code);

    /// K线数据
    void saveBars(const std::vector<Bar>& bars);
    std::vector<Bar> loadBars(const StockCode& code, BarPeriod period,
                              DateTime start, DateTime end);

    /// 数据同步状态
    void updateSyncLog(const StockCode& code, DateTime time, bool success);
    std::optional<DateTime> getLastSyncTime(const StockCode& code);

    /// 关闭连接
    void close();

    [[nodiscard]] bool isOpen() const;

private:
    bool createSchema();
    bool executeSql(const QString& sql);
    static QString marketToString(Market m);
    static Market stringToMarket(const QString& s);
    static QString periodToString(BarPeriod p);
    static BarPeriod stringToPeriod(const QString& s);

    QSqlDatabase db_;
    std::string dbPath_;
    bool open_ = false;
};

} // namespace st
