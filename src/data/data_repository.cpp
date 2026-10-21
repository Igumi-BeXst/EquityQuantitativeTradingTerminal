#include "data/data_repository.h"
#include "foundation/utils/datetime.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDir>
#include <QDateTime>
#include <QVariant>
#include <QFileInfo>
#include <QThread>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <unordered_map>

namespace st {

DataRepository::DataRepository() = default;

DataRepository::~DataRepository() {
    close();
}

bool DataRepository::init(const std::string& dbPath) {
    if (open_ && dbPath_ == dbPath && db_.isOpen()) return true;
    if (db_.isValid()) close();
    dbPath_ = dbPath;

    // Ensure directory exists
    QDir dir;
    QFileInfo fi(QString::fromStdString(dbPath));
    dir.mkpath(fi.absolutePath());

    // Use a named connection to avoid conflicts
    static std::atomic<uint64_t> connId{0};
    QString connName = QString("stock_%1_%2")
        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(QThread::currentThreadId())))
        .arg(connId.fetch_add(1, std::memory_order_relaxed));
    db_ = QSqlDatabase::addDatabase("QSQLITE", connName);
    db_.setDatabaseName(QString::fromStdString(dbPath));

    if (!db_.open()) {
        qWarning() << "Failed to open SQLite database:" << db_.lastError().text();
        return false;
    }

    open_ = true;
    executeSql("PRAGMA busy_timeout = 5000");
    executeSql("PRAGMA journal_mode = WAL");
    executeSql("PRAGMA synchronous = NORMAL");
    if (!createSchema()) {
        close();
        return false;
    }
    return true;
}

DataRepository& DataRepository::forCurrentThread(const std::string& dbPath) {
    thread_local std::unordered_map<std::string, std::unique_ptr<DataRepository>> repositories;
    const auto absolutePath = QFileInfo(QString::fromStdString(dbPath)).absoluteFilePath().toStdString();
    auto& repository = repositories[absolutePath];
    if (!repository) {
        repository = std::make_unique<DataRepository>();
        repository->init(absolutePath);
    } else if (!repository->isOpen()) {
        repository->init(absolutePath);
    }
    return *repository;
}

void DataRepository::close() {
    if (open_ && db_.isOpen()) {
        db_.close();
    }
    open_ = false;
    if (db_.isValid()) {
        QString connName = db_.connectionName();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connName);
    }
}

bool DataRepository::isOpen() const {
    return open_ && db_.isOpen();
}

bool DataRepository::createSchema() {
    QStringList statements = {
        // 股票信息
        "CREATE TABLE IF NOT EXISTS stocks ("
        "  code TEXT PRIMARY KEY,"
        "  market TEXT NOT NULL,"
        "  name TEXT,"
        "  pinyin TEXT,"
        "  pinyin_initials TEXT,"
        "  industry TEXT,"
        "  concept_tags TEXT,"
        "  exchange TEXT,"
        "  board TEXT,"
        "  list_date TEXT"
        ")",
        // 日线 K 线
        "CREATE TABLE IF NOT EXISTS daily_bars ("
        "  code TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  open REAL, high REAL, low REAL, close REAL,"
        "  volume INTEGER, amount REAL, turnover REAL,"
        "  PRIMARY KEY (code, date)"
        ")",
        // 分钟 K 线
        "CREATE TABLE IF NOT EXISTS minute_bars ("
        "  code TEXT NOT NULL,"
        "  datetime TEXT NOT NULL,"
        "  period INTEGER NOT NULL,"
        "  open REAL, high REAL, low REAL, close REAL,"
        "  volume INTEGER, amount REAL,"
        "  PRIMARY KEY (code, datetime, period)"
        ")",
        // 数据同步日志
        "CREATE TABLE IF NOT EXISTS data_sync_log ("
        "  code TEXT PRIMARY KEY,"
        "  last_sync_time TEXT,"
        "  status INTEGER"
        ")",
        // Source- and adjustment-aware history. Existing legacy tables remain untouched.
        "CREATE TABLE IF NOT EXISTS market_bars ("
        "  source_id TEXT NOT NULL, code TEXT NOT NULL, period INTEGER NOT NULL,"
        "  price_mode INTEGER NOT NULL, datetime TEXT NOT NULL,"
        "  open REAL, high REAL, low REAL, close REAL, volume INTEGER,"
        "  amount REAL, turnover REAL,"
        "  PRIMARY KEY (source_id, code, period, price_mode, datetime)"
        ")",
        "CREATE INDEX IF NOT EXISTS idx_market_bars_lookup ON market_bars"
        " (source_id, code, period, price_mode, datetime)",
        "CREATE TABLE IF NOT EXISTS market_bar_coverage ("
        "  source_id TEXT NOT NULL, code TEXT NOT NULL, period INTEGER NOT NULL,"
        "  price_mode INTEGER NOT NULL, start_time TEXT NOT NULL, end_time TEXT NOT NULL,"
        "  checked_at TEXT NOT NULL,"
        "  PRIMARY KEY (source_id, code, period, price_mode, start_time, end_time)"
        ")",
        "CREATE TABLE IF NOT EXISTS market_bar_sync_state ("
        "  source_id TEXT NOT NULL, code TEXT NOT NULL, period INTEGER NOT NULL,"
        "  price_mode INTEGER NOT NULL, last_current_success_at TEXT,"
        "  last_current_attempt_at TEXT, last_current_attempt_success INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (source_id, code, period, price_mode)"
        ")",
        "CREATE TABLE IF NOT EXISTS market_adjustment_events ("
        "  source_id TEXT NOT NULL, code TEXT NOT NULL, ordinal INTEGER NOT NULL,"
        "  ex_date TEXT NOT NULL, category INTEGER NOT NULL, cash_dividend REAL,"
        "  rights_issue_price REAL, bonus_ratio REAL, rights_ratio REAL,"
        "  PRIMARY KEY (source_id, code, ordinal)"
        ")",
        "CREATE TABLE IF NOT EXISTS market_adjustment_sync ("
        "  source_id TEXT NOT NULL, code TEXT NOT NULL, version TEXT NOT NULL,"
        "  fetched_at TEXT NOT NULL, PRIMARY KEY (source_id, code)"
        ")",
        "CREATE TABLE IF NOT EXISTS market_reference ("
        "  source_id TEXT NOT NULL, kind TEXT NOT NULL, code TEXT NOT NULL,"
        "  market TEXT NOT NULL, name TEXT, pinyin TEXT, pinyin_initials TEXT,"
        "  industry TEXT, concept_tags TEXT, exchange TEXT, board TEXT, list_date TEXT,"
        "  PRIMARY KEY (source_id, kind, code)"
        ")",
        "CREATE TABLE IF NOT EXISTS market_reference_sync ("
        "  source_id TEXT NOT NULL, kind TEXT NOT NULL, market TEXT NOT NULL,"
        "  synced_at TEXT NOT NULL, PRIMARY KEY (source_id, kind, market)"
        ")"
    };
    bool ok = true;
    for (const auto& sql : statements) ok = executeSql(sql) && ok;
    if (ok) ok = executeSql("PRAGMA user_version = 3");
    return ok;
}

bool DataRepository::executeSql(const QString& sql) {
    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        qWarning() << "SQL execute failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QString DataRepository::marketToString(Market m) {
    switch (m) {
        case Market::SH: return "SH";
        case Market::SZ: return "SZ";
        case Market::BJ: return "BJ";
        case Market::HK: return "HK";
        case Market::US: return "US";
        default: return "UNKNOWN";
    }
}

Market DataRepository::stringToMarket(const QString& s) {
    if (s == "SH") return Market::SH;
    if (s == "SZ") return Market::SZ;
    if (s == "BJ") return Market::BJ;
    if (s == "HK") return Market::HK;
    if (s == "US") return Market::US;
    return Market::Unknown;
}

QString DataRepository::periodToString(BarPeriod p) {
    switch (p) {
        case BarPeriod::Minute1:  return "1min";
        case BarPeriod::Minute5:  return "5min";
        case BarPeriod::Minute15: return "15min";
        case BarPeriod::Minute30: return "30min";
        case BarPeriod::Minute60: return "60min";
        case BarPeriod::Daily:    return "daily";
        case BarPeriod::Weekly:   return "weekly";
        case BarPeriod::Monthly:  return "monthly";
        default: return "unknown";
    }
}

BarPeriod DataRepository::stringToPeriod(const QString& s) {
    if (s == "1min")  return BarPeriod::Minute1;
    if (s == "5min")  return BarPeriod::Minute5;
    if (s == "15min") return BarPeriod::Minute15;
    if (s == "30min") return BarPeriod::Minute30;
    if (s == "60min") return BarPeriod::Minute60;
    if (s == "daily") return BarPeriod::Daily;
    if (s == "weekly") return BarPeriod::Weekly;
    if (s == "monthly") return BarPeriod::Monthly;
    return BarPeriod::Daily;
}

// --- 股票信息 ---

void DataRepository::saveStockInfos(const std::vector<StockInfo>& infos) {
    if (infos.empty() || !isOpen()) return;
    QSqlQuery query(db_);
    query.prepare(
        "INSERT OR REPLACE INTO stocks (code, market, name, pinyin, pinyin_initials,"
        "  industry, concept_tags, exchange, board, list_date)"
        "  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    for (const auto& info : infos) {
        query.addBindValue(QString::fromStdString(info.code.fullCode()));
        query.addBindValue(marketToString(info.code.market()));
        query.addBindValue(QString::fromStdString(info.name));
        query.addBindValue(QString::fromStdString(info.pinyin));
        query.addBindValue(QString::fromStdString(info.pinyinInitials));
        query.addBindValue(QString::fromStdString(info.industry));
        query.addBindValue(QString::fromStdString(info.conceptTags));
        query.addBindValue(QString::fromStdString(info.exchange));
        query.addBindValue(QString::fromStdString(info.board));
        query.addBindValue(QString::fromStdString(info.listDate == DateTime{} ? ""
                            : utils::toDateString(info.listDate)));
        if (!query.exec()) {
            qWarning() << "saveStockInfos failed:" << query.lastError().text();
        }
    }
}

std::vector<StockInfo> DataRepository::loadStockInfos() {
    std::vector<StockInfo> result;
    if (!isOpen()) return result;
    QSqlQuery query(db_);
    if (!query.exec("SELECT * FROM stocks")) {
        qWarning() << "loadStockInfos failed:" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        StockInfo info;
        auto full = query.value("code").toString();
        // 数据库存储的是 fullCode (如 SH600519)，用完整解析构造函数
        info.code = StockCode(full.toStdString());
        info.name = query.value("name").toString().toStdString();
        info.pinyin = query.value("pinyin").toString().toStdString();
        info.pinyinInitials = query.value("pinyin_initials").toString().toStdString();
        info.industry = query.value("industry").toString().toStdString();
        info.conceptTags = query.value("concept_tags").toString().toStdString();
        info.exchange = query.value("exchange").toString().toStdString();
        info.board = query.value("board").toString().toStdString();
        info.valid = !info.name.empty();
        result.push_back(std::move(info));
    }
    return result;
}

std::optional<StockInfo> DataRepository::loadStockInfo(const StockCode& code) {
    if (!isOpen()) return std::nullopt;
    QSqlQuery query(db_);
    query.prepare("SELECT * FROM stocks WHERE code = ?");
    query.addBindValue(QString::fromStdString(code.fullCode()));
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    StockInfo info;
    info.code = StockCode(query.value("code").toString().toStdString());
    info.name = query.value("name").toString().toStdString();
    info.pinyin = query.value("pinyin").toString().toStdString();
    info.pinyinInitials = query.value("pinyin_initials").toString().toStdString();
    info.industry = query.value("industry").toString().toStdString();
    info.conceptTags = query.value("concept_tags").toString().toStdString();
    info.exchange = query.value("exchange").toString().toStdString();
    info.board = query.value("board").toString().toStdString();
    info.valid = true;
    return info;
}

// --- K线数据 ---

void DataRepository::saveBars(const std::vector<Bar>& bars) {
    if (bars.empty() || !isOpen()) return;

    bool isDaily = bars[0].period == BarPeriod::Daily;
    QSqlQuery query(db_);
    if (isDaily) {
        query.prepare(
            "INSERT OR REPLACE INTO daily_bars (code, date, open, high, low, close,"
            "  volume, amount, turnover) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    } else {
        query.prepare(
            "INSERT OR REPLACE INTO minute_bars (code, datetime, period, open, high, low,"
            "  close, volume, amount) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    }

    for (const auto& bar : bars) {
        query.addBindValue(QString::fromStdString(bar.code.fullCode()));
        auto timeStr = QString::fromStdString(
            isDaily ? utils::toDateString(bar.time)
                    : utils::toDateTimeString(bar.time));
        query.addBindValue(timeStr);
        if (!isDaily) {
            query.addBindValue(static_cast<int>(bar.period));
        }
        query.addBindValue(bar.open);
        query.addBindValue(bar.high);
        query.addBindValue(bar.low);
        query.addBindValue(bar.close);
        query.addBindValue(static_cast<qint64>(bar.volume));
        query.addBindValue(bar.amount);
        if (isDaily) {
            query.addBindValue(bar.turnoverRate);
        }
        if (!query.exec()) {
            qWarning() << "saveBars failed:" << query.lastError().text();
        }
    }
}

std::vector<Bar> DataRepository::loadBars(const StockCode& code, BarPeriod period,
                                          DateTime start, DateTime end) {
    std::vector<Bar> result;
    if (!isOpen()) return result;

    bool isDaily = period == BarPeriod::Daily;
    QString sql;
    if (isDaily) {
        sql = "SELECT * FROM daily_bars WHERE code = ? AND date >= ? AND date <= ? ORDER BY date";
    } else {
        sql = "SELECT * FROM minute_bars WHERE code = ? AND period = ? AND datetime >= ?"
              " AND datetime <= ? ORDER BY datetime";
    }

    QSqlQuery query(db_);
    query.prepare(sql);
    query.addBindValue(QString::fromStdString(code.fullCode()));
    if (isDaily) {
        query.addBindValue(QString::fromStdString(utils::toDateString(start)));
        query.addBindValue(QString::fromStdString(utils::toDateString(end)));
    } else {
        query.addBindValue(static_cast<int>(period));
        query.addBindValue(QString::fromStdString(utils::toDateTimeString(start)));
        query.addBindValue(QString::fromStdString(utils::toDateTimeString(end)));
    }

    if (!query.exec()) {
        qWarning() << "loadBars failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        Bar bar;
        auto full = query.value("code").toString();
        auto market = stringToMarket(full.left(2));
        bar.code = StockCode(market, full.toStdString());
        bar.period = period;
        bar.open = query.value("open").toDouble();
        bar.high = query.value("high").toDouble();
        bar.low = query.value("low").toDouble();
        bar.close = query.value("close").toDouble();
        bar.volume = query.value("volume").toLongLong();
        bar.amount = query.value("amount").toDouble();
        if (isDaily) {
            bar.turnoverRate = query.value("turnover").toDouble();
            bar.time = utils::parseDate(query.value("date").toString().toStdString());
        } else {
            bar.time = utils::parseDateTime(query.value("datetime").toString().toStdString());
        }
        result.push_back(std::move(bar));
    }
    return result;
}

// --- 同步日志 ---

void DataRepository::updateSyncLog(const StockCode& code, DateTime time, bool success) {
    if (!isOpen()) return;
    QSqlQuery query(db_);
    query.prepare("INSERT OR REPLACE INTO data_sync_log (code, last_sync_time, status)"
                  " VALUES (?, ?, ?)");
    query.addBindValue(QString::fromStdString(code.fullCode()));
    query.addBindValue(QString::fromStdString(utils::toDateTimeString(time)));
    query.addBindValue(success ? 1 : 0);
    query.exec();
}

std::optional<DateTime> DataRepository::getLastSyncTime(const StockCode& code) {
    if (!isOpen()) return std::nullopt;
    QSqlQuery query(db_);
    query.prepare("SELECT last_sync_time FROM data_sync_log WHERE code = ?");
    query.addBindValue(QString::fromStdString(code.fullCode()));
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    auto str = query.value(0).toString().toStdString();
    if (str.empty()) return std::nullopt;
    return utils::parseDateTime(str);
}

std::vector<Bar> DataRepository::loadCachedBars(const std::string& sourceId,
                                                const StockCode& code,
                                                BarPeriod period, BarPriceMode mode,
                                                DateTime start, DateTime end) {
    std::vector<Bar> result;
    if (!isOpen()) return result;
    QSqlQuery query(db_);
    query.prepare("SELECT datetime, open, high, low, close, volume, amount, turnover "
                  "FROM market_bars WHERE source_id=? AND code=? AND period=? "
                  "AND price_mode=? AND datetime>=? AND datetime<=? ORDER BY datetime");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(code.fullCode()));
    query.addBindValue(static_cast<int>(period));
    query.addBindValue(static_cast<int>(mode));
    query.addBindValue(QString::fromStdString(utils::toDateTimeString(start)));
    query.addBindValue(QString::fromStdString(utils::toDateTimeString(end)));
    if (!query.exec()) {
        qWarning() << "loadCachedBars failed:" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        Bar bar;
        bar.code = code;
        bar.period = period;
        bar.time = utils::parseDateTime(query.value(0).toString().toStdString());
        bar.open = query.value(1).toDouble();
        bar.high = query.value(2).toDouble();
        bar.low = query.value(3).toDouble();
        bar.close = query.value(4).toDouble();
        bar.volume = query.value(5).toLongLong();
        bar.amount = query.value(6).toDouble();
        bar.turnoverRate = query.value(7).toDouble();
        result.push_back(std::move(bar));
    }
    return result;
}

bool DataRepository::saveCachedBars(const std::string& sourceId, BarPriceMode mode,
                                   const std::vector<Bar>& bars) {
    if (!isOpen() || bars.empty() || !db_.transaction()) return false;
    QSqlQuery query(db_);
    query.prepare("INSERT OR REPLACE INTO market_bars "
                  "(source_id,code,period,price_mode,datetime,open,high,low,close,volume,amount,turnover) "
                  "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto& bar : bars) {
        query.bindValue(0, QString::fromStdString(sourceId));
        query.bindValue(1, QString::fromStdString(bar.code.fullCode()));
        query.bindValue(2, static_cast<int>(bar.period));
        query.bindValue(3, static_cast<int>(mode));
        query.bindValue(4, QString::fromStdString(utils::toDateTimeString(bar.time)));
        query.bindValue(5, bar.open);
        query.bindValue(6, bar.high);
        query.bindValue(7, bar.low);
        query.bindValue(8, bar.close);
        query.bindValue(9, static_cast<qint64>(bar.volume));
        query.bindValue(10, bar.amount);
        query.bindValue(11, bar.turnoverRate);
        if (!query.exec()) {
            qWarning() << "saveCachedBars failed:" << query.lastError().text();
            db_.rollback();
            return false;
        }
    }
    if (!db_.commit()) {
        qWarning() << "saveCachedBars commit failed:" << db_.lastError().text();
        db_.rollback();
        return false;
    }
    return true;
}

std::vector<DataRepository::CoverageRange> DataRepository::loadBarCoverage(
    const std::string& sourceId, const StockCode& code, BarPeriod period, BarPriceMode mode) {
    std::vector<CoverageRange> result;
    if (!isOpen()) return result;
    QSqlQuery query(db_);
    query.prepare("SELECT start_time,end_time FROM market_bar_coverage "
                  "WHERE source_id=? AND code=? AND period=? AND price_mode=? ORDER BY start_time");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(code.fullCode()));
    query.addBindValue(static_cast<int>(period));
    query.addBindValue(static_cast<int>(mode));
    if (!query.exec()) return result;
    while (query.next()) {
        result.push_back({utils::parseDateTime(query.value(0).toString().toStdString()),
                          utils::parseDateTime(query.value(1).toString().toStdString())});
    }
    return result;
}

bool DataRepository::addBarCoverage(const std::string& sourceId, const StockCode& code,
                                    BarPeriod period, BarPriceMode mode,
                                    DateTime start, DateTime end, DateTime syncedAt) {
    if (!isOpen() || end < start || !db_.transaction()) return false;
    auto ranges = loadBarCoverage(sourceId, code, period, mode);
    ranges.push_back({start, end});
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
        return a.start < b.start;
    });
    std::vector<CoverageRange> merged;
    for (const auto& range : ranges) {
        if (merged.empty() || range.start > merged.back().end + std::chrono::seconds(1)) {
            merged.push_back(range);
        } else if (range.end > merged.back().end) {
            merged.back().end = range.end;
        }
    }
    QSqlQuery remove(db_);
    remove.prepare("DELETE FROM market_bar_coverage WHERE source_id=? AND code=? AND period=? AND price_mode=?");
    remove.addBindValue(QString::fromStdString(sourceId));
    remove.addBindValue(QString::fromStdString(code.fullCode()));
    remove.addBindValue(static_cast<int>(period));
    remove.addBindValue(static_cast<int>(mode));
    if (!remove.exec()) {
        qWarning() << "addBarCoverage delete failed:" << remove.lastError().text();
        db_.rollback();
        return false;
    }
    QSqlQuery insert(db_);
    insert.prepare("INSERT INTO market_bar_coverage "
                   "(source_id,code,period,price_mode,start_time,end_time,checked_at) VALUES (?,?,?,?,?,?,?)");
    for (const auto& range : merged) {
        insert.bindValue(0, QString::fromStdString(sourceId));
        insert.bindValue(1, QString::fromStdString(code.fullCode()));
        insert.bindValue(2, static_cast<int>(period));
        insert.bindValue(3, static_cast<int>(mode));
        insert.bindValue(4, QString::fromStdString(utils::toDateTimeString(range.start)));
        insert.bindValue(5, QString::fromStdString(utils::toDateTimeString(range.end)));
        insert.bindValue(6, QString::fromStdString(utils::toDateTimeString(syncedAt)));
        if (!insert.exec()) {
            qWarning() << "addBarCoverage insert failed:" << insert.lastError().text();
            db_.rollback();
            return false;
        }
    }
    if (!db_.commit()) {
        qWarning() << "addBarCoverage commit failed:" << db_.lastError().text();
        db_.rollback();
        return false;
    }
    return true;
}

DataRepository::BarSyncState DataRepository::loadBarSyncState(
    const std::string& sourceId, const StockCode& code, BarPeriod period, BarPriceMode mode) {
    BarSyncState state;
    if (!isOpen()) return state;
    QSqlQuery query(db_);
    query.prepare("SELECT last_current_success_at,last_current_attempt_at,"
                  "last_current_attempt_success FROM market_bar_sync_state "
                  "WHERE source_id=? AND code=? AND period=? AND price_mode=?");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(code.fullCode()));
    query.addBindValue(static_cast<int>(period));
    query.addBindValue(static_cast<int>(mode));
    if (!query.exec() || !query.next()) return state;
    const auto successAt = query.value(0).toString().toStdString();
    const auto attemptAt = query.value(1).toString().toStdString();
    if (!successAt.empty()) state.lastCurrentSuccessAt = utils::parseDateTime(successAt);
    if (!attemptAt.empty()) state.lastCurrentAttemptAt = utils::parseDateTime(attemptAt);
    state.lastCurrentAttemptSucceeded = query.value(2).toBool();
    return state;
}

void DataRepository::recordBarSyncAttempt(const std::string& sourceId, const StockCode& code,
                                         BarPeriod period, BarPriceMode mode,
                                         DateTime attemptedAt, bool success,
                                         bool currentWindow) {
    if (!isOpen() || !currentWindow) return;
    QSqlQuery query(db_);
    query.prepare("INSERT OR IGNORE INTO market_bar_sync_state "
                  "(source_id,code,period,price_mode) VALUES (?,?,?,?)");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(code.fullCode()));
    query.addBindValue(static_cast<int>(period));
    query.addBindValue(static_cast<int>(mode));
    if (!query.exec()) {
        qWarning() << "recordBarSyncAttempt initialize failed:" << query.lastError().text();
        return;
    }

    QSqlQuery update(db_);
    if (success) {
        update.prepare("UPDATE market_bar_sync_state SET last_current_attempt_at=?, "
                       "last_current_attempt_success=1,last_current_success_at=? "
                       "WHERE source_id=? AND code=? AND period=? AND price_mode=?");
        update.addBindValue(QString::fromStdString(utils::toDateTimeString(attemptedAt)));
        update.addBindValue(QString::fromStdString(utils::toDateTimeString(attemptedAt)));
    } else {
        update.prepare("UPDATE market_bar_sync_state SET last_current_attempt_at=?, "
                       "last_current_attempt_success=0 "
                       "WHERE source_id=? AND code=? AND period=? AND price_mode=?");
        update.addBindValue(QString::fromStdString(utils::toDateTimeString(attemptedAt)));
    }
    update.addBindValue(QString::fromStdString(sourceId));
    update.addBindValue(QString::fromStdString(code.fullCode()));
    update.addBindValue(static_cast<int>(period));
    update.addBindValue(static_cast<int>(mode));
    if (!update.exec()) qWarning() << "recordBarSyncAttempt update failed:" << update.lastError().text();
}

std::optional<DataRepository::AdjustmentSnapshot> DataRepository::loadAdjustmentSnapshot(
    const std::string& sourceId, const StockCode& code) {
    if (!isOpen()) return std::nullopt;
    QSqlQuery meta(db_);
    meta.prepare("SELECT version,fetched_at FROM market_adjustment_sync WHERE source_id=? AND code=?");
    meta.addBindValue(QString::fromStdString(sourceId));
    meta.addBindValue(QString::fromStdString(code.fullCode()));
    if (!meta.exec() || !meta.next()) return std::nullopt;
    AdjustmentSnapshot snapshot;
    snapshot.version = meta.value(0).toString().toStdString();
    snapshot.fetchedAt = utils::parseDateTime(meta.value(1).toString().toStdString());
    QSqlQuery events(db_);
    events.prepare("SELECT ex_date,category,cash_dividend,rights_issue_price,bonus_ratio,rights_ratio "
                   "FROM market_adjustment_events WHERE source_id=? AND code=? ORDER BY ordinal");
    events.addBindValue(QString::fromStdString(sourceId));
    events.addBindValue(QString::fromStdString(code.fullCode()));
    if (!events.exec()) return std::nullopt;
    while (events.next()) {
        AdjustmentEvent event;
        event.exDate = utils::parseDate(events.value(0).toString().toStdString());
        event.category = events.value(1).toInt();
        event.cashDividend = events.value(2).toDouble();
        event.rightsIssuePrice = events.value(3).toDouble();
        event.bonusRatio = events.value(4).toDouble();
        event.rightsRatio = events.value(5).toDouble();
        snapshot.events.push_back(event);
    }
    return snapshot;
}

bool DataRepository::saveAdjustmentSnapshot(const std::string& sourceId, const StockCode& code,
                                            const std::vector<AdjustmentEvent>& events,
                                            const std::string& version, DateTime fetchedAt) {
    if (!isOpen() || !db_.transaction()) return false;
    auto previous = loadAdjustmentSnapshot(sourceId, code);
    QSqlQuery remove(db_);
    remove.prepare("DELETE FROM market_adjustment_events WHERE source_id=? AND code=?");
    remove.addBindValue(QString::fromStdString(sourceId));
    remove.addBindValue(QString::fromStdString(code.fullCode()));
    if (!remove.exec()) { db_.rollback(); return false; }
    QSqlQuery insert(db_);
    insert.prepare("INSERT INTO market_adjustment_events "
                   "(source_id,code,ordinal,ex_date,category,cash_dividend,rights_issue_price,bonus_ratio,rights_ratio) "
                   "VALUES (?,?,?,?,?,?,?,?,?)");
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        insert.bindValue(0, QString::fromStdString(sourceId));
        insert.bindValue(1, QString::fromStdString(code.fullCode()));
        insert.bindValue(2, static_cast<qulonglong>(i));
        insert.bindValue(3, QString::fromStdString(utils::toDateString(event.exDate)));
        insert.bindValue(4, event.category);
        insert.bindValue(5, event.cashDividend);
        insert.bindValue(6, event.rightsIssuePrice);
        insert.bindValue(7, event.bonusRatio);
        insert.bindValue(8, event.rightsRatio);
        if (!insert.exec()) { db_.rollback(); return false; }
    }
    QSqlQuery sync(db_);
    sync.prepare("INSERT OR REPLACE INTO market_adjustment_sync "
                 "(source_id,code,version,fetched_at) VALUES (?,?,?,?)");
    sync.addBindValue(QString::fromStdString(sourceId));
    sync.addBindValue(QString::fromStdString(code.fullCode()));
    sync.addBindValue(QString::fromStdString(version));
    sync.addBindValue(QString::fromStdString(utils::toDateTimeString(fetchedAt)));
    if (!sync.exec() || !db_.commit()) { db_.rollback(); return false; }
    return !previous || previous->version != version;
}

bool DataRepository::saveReferenceInfos(const std::string& sourceId, const std::string& kind,
                                        const std::vector<StockInfo>& infos) {
    if (!isOpen() || infos.empty() || !db_.transaction()) return false;
    if (kind != "stock_info") {
        if (kind == "stock_list") {
            for (Market market : {Market::SH, Market::SZ, Market::BJ, Market::HK, Market::US}) {
                const bool included = std::any_of(infos.begin(), infos.end(),
                    [market](const StockInfo& info) { return info.code.market() == market; });
                if (!included) continue;
                QSqlQuery remove(db_);
                remove.prepare("DELETE FROM market_reference WHERE source_id=? AND kind=? AND market=?");
                remove.addBindValue(QString::fromStdString(sourceId));
                remove.addBindValue(QString::fromStdString(kind));
                remove.addBindValue(marketToString(market));
                if (!remove.exec()) { db_.rollback(); return false; }
            }
        } else {
            QSqlQuery remove(db_);
            remove.prepare("DELETE FROM market_reference WHERE source_id=? AND kind=?");
            remove.addBindValue(QString::fromStdString(sourceId));
            remove.addBindValue(QString::fromStdString(kind));
            if (!remove.exec()) { db_.rollback(); return false; }
        }
    }
    QSqlQuery insert(db_);
    insert.prepare("INSERT OR REPLACE INTO market_reference "
                   "(source_id,kind,code,market,name,pinyin,pinyin_initials,industry,concept_tags,exchange,board,list_date) "
                   "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto& info : infos) {
        insert.bindValue(0, QString::fromStdString(sourceId));
        insert.bindValue(1, QString::fromStdString(kind));
        insert.bindValue(2, QString::fromStdString(info.code.fullCode()));
        insert.bindValue(3, marketToString(info.code.market()));
        insert.bindValue(4, QString::fromStdString(info.name));
        insert.bindValue(5, QString::fromStdString(info.pinyin));
        insert.bindValue(6, QString::fromStdString(info.pinyinInitials));
        insert.bindValue(7, QString::fromStdString(info.industry));
        insert.bindValue(8, QString::fromStdString(info.conceptTags));
        insert.bindValue(9, QString::fromStdString(info.exchange));
        insert.bindValue(10, QString::fromStdString(info.board));
        insert.bindValue(11, QString::fromStdString(info.listDate == DateTime{} ? "" : utils::toDateString(info.listDate)));
        if (!insert.exec()) { db_.rollback(); return false; }
    }
    if (!db_.commit()) {
        db_.rollback();
        return false;
    }
    return true;
}

std::vector<StockInfo> DataRepository::loadReferenceInfos(const std::string& sourceId,
                                                           const std::string& kind,
                                                           Market market) {
    std::vector<StockInfo> result;
    if (!isOpen()) return result;
    QSqlQuery query(db_);
    query.prepare(market == Market::Unknown
        ? "SELECT * FROM market_reference WHERE source_id=? AND kind=? ORDER BY code"
        : "SELECT * FROM market_reference WHERE source_id=? AND kind=? AND market=? ORDER BY code");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(kind));
    if (market != Market::Unknown) query.addBindValue(marketToString(market));
    if (!query.exec()) return result;
    while (query.next()) {
        StockInfo info;
        info.code = StockCode(query.value("code").toString().toStdString());
        info.name = query.value("name").toString().toStdString();
        info.pinyin = query.value("pinyin").toString().toStdString();
        info.pinyinInitials = query.value("pinyin_initials").toString().toStdString();
        info.industry = query.value("industry").toString().toStdString();
        info.conceptTags = query.value("concept_tags").toString().toStdString();
        info.exchange = query.value("exchange").toString().toStdString();
        info.board = query.value("board").toString().toStdString();
        const auto listDate = query.value("list_date").toString().toStdString();
        if (!listDate.empty()) info.listDate = utils::parseDate(listDate);
        info.valid = info.code.isValid() && !info.name.empty();
        result.push_back(std::move(info));
    }
    return result;
}

std::optional<StockInfo> DataRepository::loadReferenceInfo(const std::string& sourceId,
                                                            const std::string& kind,
                                                            const StockCode& code) {
    if (!isOpen()) return std::nullopt;
    QSqlQuery query(db_);
    query.prepare("SELECT * FROM market_reference WHERE source_id=? AND kind=? AND code=?");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(kind));
    query.addBindValue(QString::fromStdString(code.fullCode()));
    if (!query.exec() || !query.next()) return std::nullopt;
    StockInfo info;
    info.code = code;
    info.name = query.value("name").toString().toStdString();
    info.pinyin = query.value("pinyin").toString().toStdString();
    info.pinyinInitials = query.value("pinyin_initials").toString().toStdString();
    info.industry = query.value("industry").toString().toStdString();
    info.conceptTags = query.value("concept_tags").toString().toStdString();
    info.exchange = query.value("exchange").toString().toStdString();
    info.board = query.value("board").toString().toStdString();
    const auto listDate = query.value("list_date").toString().toStdString();
    if (!listDate.empty()) info.listDate = utils::parseDate(listDate);
    info.valid = !info.name.empty();
    return info;
}

std::optional<DateTime> DataRepository::getReferenceSyncTime(const std::string& sourceId,
                                                              const std::string& kind,
                                                              Market market) {
    if (!isOpen()) return std::nullopt;
    QSqlQuery query(db_);
    query.prepare("SELECT synced_at FROM market_reference_sync WHERE source_id=? AND kind=? AND market=?");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(kind));
    query.addBindValue(marketToString(market));
    if (!query.exec() || !query.next()) return std::nullopt;
    return utils::parseDateTime(query.value(0).toString().toStdString());
}

void DataRepository::updateReferenceSyncTime(const std::string& sourceId,
                                             const std::string& kind, Market market,
                                             DateTime syncedAt) {
    if (!isOpen()) return;
    QSqlQuery query(db_);
    query.prepare("INSERT OR REPLACE INTO market_reference_sync "
                  "(source_id,kind,market,synced_at) VALUES (?,?,?,?)");
    query.addBindValue(QString::fromStdString(sourceId));
    query.addBindValue(QString::fromStdString(kind));
    query.addBindValue(marketToString(market));
    query.addBindValue(QString::fromStdString(utils::toDateTimeString(syncedAt)));
    if (!query.exec()) qWarning() << "updateReferenceSyncTime failed:" << query.lastError().text();
}

} // namespace st
