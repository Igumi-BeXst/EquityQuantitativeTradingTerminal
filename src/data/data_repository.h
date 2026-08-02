#pragma once

#include "foundation/stock_code.h"
#include "foundation/stock_info.h"
#include "foundation/bar.h"
#include "foundation/types.h"
#include <QSqlDatabase>
#include <string>
#include <vector>
#include <memory>

namespace st {

/// SQLite 数据仓储 — 持久化 K线、股票信息、同步状态
/// 所有数据通过 Qt6::Sql 访问 SQLite
class DataRepository {
public:
    DataRepository();
    ~DataRepository();

    /// 打开数据库并创建表结构
    bool init(const std::string& dbPath);

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
    void createSchema();
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
