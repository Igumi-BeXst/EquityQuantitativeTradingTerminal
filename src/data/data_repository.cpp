#include "data/data_repository.h"
#include "foundation/utils/datetime.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDir>
#include <QDateTime>
#include <QVariant>
#include <cstdint>

namespace st {

DataRepository::DataRepository() = default;

DataRepository::~DataRepository() {
    close();
}

bool DataRepository::init(const std::string& dbPath) {
    dbPath_ = dbPath;

    // Ensure directory exists
    QDir dir;
    QFileInfo fi(QString::fromStdString(dbPath));
    dir.mkpath(fi.absolutePath());

    // Use a named connection to avoid conflicts
    static int connId = 0;
    QString connName = QString("stock_%1").arg(connId++);
    db_ = QSqlDatabase::addDatabase("QSQLITE", connName);
    db_.setDatabaseName(QString::fromStdString(dbPath));

    if (!db_.open()) {
        qWarning() << "Failed to open SQLite database:" << db_.lastError().text();
        return false;
    }

    open_ = true;
    createSchema();
    return true;
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

void DataRepository::createSchema() {
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
        ")"
    };
    for (const auto& sql : statements) {
        executeSql(sql);
    }
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

} // namespace st
