#include "data/akshare_provider.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <optional>
#include <thread>
#include <unordered_map>

namespace st {

namespace {
constexpr int kTimeoutMs = 10000;
constexpr int kFundCacheSeconds = 30;  // 东财 A 股整表缓存时长

/// 东财 push2 集群多主机回退表：push2delay（延时主机）实测可用（2026-08 主域封锁期）
/// 置首位；其余编号子域与主域作后备。ulist/clist 共用同一主机群。
const char* kEastMoneyPush2Hosts[] = {
    "push2delay.eastmoney.com",
    "push2.eastmoney.com",
    "1.push2.eastmoney.com",
    "2.push2.eastmoney.com",
    "3.push2.eastmoney.com",
};

/// 线程本地 QNetworkAccessManager（NoProxy）— 任意 IO 线程可安全调用。
/// 基本面接口从 ThreadPool IO 线程调用，不能用主线程亲和的成员 QNAM。
QNetworkAccessManager& threadLocalHttp() {
    static thread_local QNetworkAccessManager qnam;
    static thread_local bool inited = false;
    if (!inited) {
        qnam.setProxy(QNetworkProxy::NoProxy);
        inited = true;
    }
    return qnam;
}

/// 同步 GET（线程安全），失败重试 3 次
std::string httpGet(const std::string& url) {
    auto& http = threadLocalHttp();
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        QNetworkRequest request{QUrl(QString::fromStdString(url))};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        request.setRawHeader("Referer", "https://quote.eastmoney.com/");
        request.setRawHeader("Accept", "application/json, text/plain, */*");
        request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
        request.setTransferTimeout(kTimeoutMs);

        QEventLoop loop;
        QNetworkReply* reply = http.get(request);
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(kTimeoutMs);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            const auto body = reply->readAll();
            reply->deleteLater();
            if (!body.isEmpty()) return body.toStdString();
        } else {
            LogManager::instance()->log(LogLevel::Warn,
                "东财 fetch 失败 (第 {} 次): {}", attempt + 1, url);
        }
        reply->deleteLater();
    }
    return {};
}

}  // namespace

AKShareProvider::AKShareProvider()
    : http_(std::make_unique<QNetworkAccessManager>()) {}

AKShareProvider::~AKShareProvider() {
    disconnect();
}

bool AKShareProvider::connect() {
    if (!http_) {
        http_ = std::make_unique<QNetworkAccessManager>();
    }
    connected_ = true;
    LogManager::instance()->log(LogLevel::Info, "AKShareProvider connected (Qt Network)");
    return true;
}

void AKShareProvider::disconnect() {
    // 不访问成员 QNAM（http_）：本 provider 可能在 IO 线程被析构
    // （fundProvider_ 的 shared_ptr 最后一个引用在 IO 线程释放），
    // 跨线程调 QNAM::clearAccessCache 有堆损坏风险；清缓存非必需，跳过。
    connected_ = false;
}

bool AKShareProvider::isConnected() const {
    return connected_ && http_ != nullptr;
}

std::string AKShareProvider::fetch(const std::string& url) {
    if (!isConnected()) {
        connect();
    }
    if (!http_) return {};

    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    request.setRawHeader("Referer", "https://quote.eastmoney.com/");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
    request.setTransferTimeout(kTimeoutMs);

    QEventLoop loop;
    QNetworkReply* reply = http_->get(request);

    // 超时定时器
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kTimeoutMs);

    // 完成或错误 → 退出事件循环
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray body;
    if (reply->error() == QNetworkReply::NoError) {
        body = reply->readAll();
    } else {
        LogManager::instance()->log(LogLevel::Warn, "HTTP fetch failed for {}: {}",
                                    url, reply->errorString().toStdString());
    }

    reply->deleteLater();
    return body.toStdString();
}

// --- 股票列表 ---

std::vector<StockInfo> AKShareProvider::fetchStockList(Market market) {
    std::vector<StockInfo> result;
    // 东财板块股票列表接口: http://push2delay.eastmoney.com/api/qt/clist/get
    std::string fs;
    switch (market) {
        case Market::SH: fs = "m:1+t:2"; break;      // 上海主板
        case Market::SZ: fs = "m:0+t:6"; break;      // 深圳主板
        default:
            fs = "m:0+t:6,m:0+t:80,m:1+t:2,m:1+t:23"; // 全A
            break;
    }
    const std::string query = "/api/qt/clist/get?pn=1&pz=100&po=1&np=1"
        "&fields=f12,f14,f13,f15,f16,f17,f18,f20,f26&fs=" + fs;
    // 多主机回退：当前主机失败自动换下一个东财节点
    for (const char* host : kEastMoneyPush2Hosts) {
        auto body = fetch(std::string("http://") + host + query);
        if (body.empty()) continue;
        try {
            auto json = nlohmann::json::parse(body);
            auto& data = json["data"];
            if (data.is_null()) continue;
            for (auto& item : data["diff"]) {
                StockInfo info;
                // f12 股票代码(数字可能是字符串或数字), f14 名称, f13 市场(1沪/0深)
                std::string codeStr;
                if (item.contains("f12") && item["f12"].is_string()) {
                    codeStr = item["f12"].get<std::string>();
                } else if (item.contains("f12") && item["f12"].is_number()) {
                    codeStr = std::to_string(item["f12"].get<long long>());
                }
                std::string name;
                if (item.contains("f14") && item["f14"].is_string()) {
                    name = item["f14"].get<std::string>();
                } else if (item.contains("f14") && item["f14"].is_number()) {
                    name = std::to_string(item["f14"].get<long long>());
                }
                if (codeStr.empty() || name.empty()) continue;
                // 代码补零到6位
                while (codeStr.size() < 6) codeStr = "0" + codeStr;
                info.code = StockCode(codeStr);
                info.name = name;
                int marketCode = item.contains("f13") && item["f13"].is_number()
                    ? item["f13"].get<int>() : -1;
                info.board = marketCode == 1 ? "沪" : "深";
                info.valid = true;
                result.push_back(std::move(info));
            }
            if (!result.empty()) break;  // 当前主机成功 → 返回
        } catch (const nlohmann::json::exception& e) {
            LogManager::instance()->log(LogLevel::Warn, "Parse stock list failed: {}", e.what());
        }
    }
    LogManager::instance()->log(LogLevel::Info, "Fetched {} stocks", result.size());
    return result;
}

std::vector<StockInfo> AKShareProvider::getStockList(Market market) {
    return fetchStockList(market);
}

std::optional<StockInfo> AKShareProvider::getStockInfo(const StockCode& code) {
    auto list = fetchStockList(code.market());
    for (auto& info : list) {
        if (info.code == code) return info;
    }
    return std::nullopt;
}

// --- K线数据 ---

std::vector<Bar> AKShareProvider::fetchDailyBars(const StockCode& code,
                                                 DateTime start, DateTime end) {
    std::vector<Bar> result;
    // 东财日K接口: push2his.eastmoney.com/api/qt/stock/kline/get
    std::string secid;
    if (code.market() == Market::SH) {
        secid = "1." + code.code();
    } else {
        secid = "0." + code.code();
    }
    std::ostringstream url;
    url << "http://push2his.eastmoney.com/api/qt/stock/kline/get?"
        << "secid=" << secid
        << "&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56,f57,f58,f59,f60,f61"
        << "&klt=101&fqt=1"
        << "&beg=" << utils::toDateString(start)
        << "&end=" << utils::toDateString(end);
    auto body = fetch(url.str());
    if (body.empty()) return result;

    try {
        auto json = nlohmann::json::parse(body);
        auto& data = json["data"];
        if (data.is_null() || !data.contains("klines")) return result;
        for (auto& kline : data["klines"]) {
            auto str = kline.get<std::string>();
            // format: "2024-01-15,open,close,high,low,vol,amount,amplitude,pct,chg,turnover"
            std::vector<std::string> parts;
            std::string token;
            std::istringstream iss(str);
            while (std::getline(iss, token, ',')) parts.push_back(token);
            if (parts.size() < 7) continue;
            Bar bar;
            bar.code = code;
            bar.period = BarPeriod::Daily;
            bar.time = utils::parseDate(parts[0]);
            bar.open = std::stod(parts[1]);
            bar.close = std::stod(parts[2]);
            bar.high = std::stod(parts[3]);
            bar.low = std::stod(parts[4]);
            bar.volume = std::stoll(parts[5]);
            bar.amount = std::stod(parts[6]);
            if (parts.size() > 10) {
                bar.turnoverRate = std::stod(parts[10]) / 100.0;
            }
            result.push_back(std::move(bar));
        }
    } catch (const std::exception& e) {
        LogManager::instance()->log(LogLevel::Warn, "Parse daily bars failed: {}", e.what());
    }
    return result;
}

std::vector<Bar> AKShareProvider::fetchMinuteBars(const StockCode& code, BarPeriod period,
                                                  DateTime start, DateTime end) {
    // 分钟线接口尚未完全实现 — 返回空
    (void)code; (void)period; (void)start; (void)end;
    return {};
}

std::vector<Bar> AKShareProvider::getBars(const StockCode& code, BarPeriod period,
                                          DateTime start, DateTime end) {
    if (period == BarPeriod::Daily) {
        return fetchDailyBars(code, start, end);
    }
    return fetchMinuteBars(code, period, start, end);
}

// --- 实时行情 (桩) ---

void AKShareProvider::subscribeQuote(const StockCode&) {}
void AKShareProvider::unsubscribeQuote(const StockCode&) {}

std::vector<Quote> AKShareProvider::batchQuote(const std::vector<StockCode>&) {
    return {};
}
std::optional<IntradayData> AKShareProvider::getIntraday(const StockCode&) {
    return std::nullopt;
}
void AKShareProvider::refreshQuotes() {}

// --- 基本面快照（东财 ulist.np 指定代码批量行情接口）---

std::optional<QuoteFundamentals> AKShareProvider::getQuoteFundamentals(const StockCode& code) {
    if (code.market() != Market::SH && code.market() != Market::SZ) return std::nullopt;
    // 指定代码批量接口（与 clist 同域、明文 HTTP 可用，fltt=2 返回裸值 元/股/%）。
    // 多主机回退：当前主机失败（空回复/超时）自动换下一个东财节点。
    const std::string secid = (code.market() == Market::SH ? "1." : "0.") + code.code();
    const std::string query = "/api/qt/ulist.np/get?secids=" + secid
        + "&fields=f12,f8,f115,f20,f21,f38,f39&fltt=2&invt=2";
    for (const char* host : kEastMoneyPush2Hosts) {
        const auto body = httpGet(std::string("http://") + host + query);
        if (body.empty()) continue;
        try {
            const auto json = nlohmann::json::parse(body);
            const auto& data = json.value("data", nlohmann::json());
            if (!data.is_object() || !data.contains("diff")) continue;
            const auto& diff = data["diff"];
            if (!diff.is_array() || diff.empty()) continue;
            return parseFundamentals(diff.front().dump(), code);
        } catch (...) {
            continue;
        }
    }
    return std::nullopt;
}

std::vector<QuoteFundamentals> AKShareProvider::batchQuoteFundamentals(
    const std::vector<StockCode>& codes) {
    std::vector<QuoteFundamentals> out;
    std::ostringstream secids;
    bool first = true;
    for (const auto& c : codes) {
        if (c.market() != Market::SH && c.market() != Market::SZ) continue;
        if (!first) secids << ',';
        first = false;
        secids << (c.market() == Market::SH ? "1." : "0.") << c.code();
    }
    if (first) return out;  // 无可处理代码

    // 多主机回退：当前主机失败（空回复/超时/解析失败）自动换下一个东财节点
    const std::string query = "/api/qt/ulist.np/get?secids=" + secids.str()
        + "&fields=f12,f8,f115,f20,f21,f38,f39&fltt=2&invt=2";
    for (const char* host : kEastMoneyPush2Hosts) {
        const auto body = httpGet(std::string("http://") + host + query);
        if (body.empty()) continue;
        try {
            const auto json = nlohmann::json::parse(body);
            const auto& data = json.value("data", nlohmann::json());
            if (!data.is_object() || !data.contains("diff")) continue;
            const auto& diff = data["diff"];
            if (!diff.is_array()) continue;
            for (const auto& item : diff) {
                std::string codeStr;
                if (item.contains("f12") && item["f12"].is_string()) {
                    codeStr = item["f12"].get<std::string>();
                } else if (item.contains("f12") && item["f12"].is_number()) {
                    codeStr = std::to_string(item["f12"].get<long long>());
                }
                while (codeStr.size() < 6) codeStr = "0" + codeStr;
                if (codeStr.empty()) continue;
                if (auto f = parseFundamentals(item.dump(), StockCode(codeStr))) {
                    out.push_back(std::move(*f));
                }
            }
            if (!out.empty()) return out;  // 当前主机解析出数据 → 返回
        } catch (...) {
            continue;
        }
    }
    return out;
}

std::optional<QuoteFundamentals> AKShareProvider::parseFundamentals(
    const std::string& itemJson, const StockCode& code) {
    try {
        const auto item = nlohmann::json::parse(itemJson);
        const auto num = [&item](const char* key) -> double {
            const auto v = item.value(key, nlohmann::json());
            if (v.is_number()) return v.get<double>();
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                if (s.empty() || s == "-") return 0.0;
                try { return std::stod(s); } catch (...) { return 0.0; }
            }
            return 0.0;
        };
        QuoteFundamentals f;
        f.code = code;
        f.turnoverRate = num("f8");       // 换手率 %
        f.peStatic = num("f115");         // 市盈(静)
        f.marketCap = num("f20");         // 总市值 元
        f.floatCap = num("f21");          // 流通市值 元
        f.totalShares = num("f38");       // 总股本 股
        f.floatShares = num("f39");       // 流通股 股
        // 换手率(实) = 换手率 × 流通股/总股本（全流通时≈换手率）
        f.turnoverRateReal = f.totalShares > 0.0
            ? f.turnoverRate * f.floatShares / f.totalShares : f.turnoverRate;
        f.valid = f.marketCap > 0.0 || f.totalShares > 0.0;
        if (!f.valid) return std::nullopt;
        return f;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace st
