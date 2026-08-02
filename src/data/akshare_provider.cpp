#include "data/akshare_provider.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace st {

namespace {
constexpr int kTimeoutMs = 10000;
}

AKShareProvider::AKShareProvider() = default;

AKShareProvider::~AKShareProvider() {
    disconnect();
}

bool AKShareProvider::connect() {
    http_ = std::make_shared<httplib::Client>(host_);
    http_->set_connection_timeout(5, 0);
    http_->set_read_timeout(kTimeoutMs, 0);
    http_->set_follow_location(true);
    connected_ = true;
    LogManager::instance()->log(LogLevel::Info, "AKShareProvider connected to {}", host_);
    return true;
}

void AKShareProvider::disconnect() {
    http_.reset();
    connected_ = false;
}

bool AKShareProvider::isConnected() const {
    return connected_ && http_ != nullptr;
}

std::string AKShareProvider::fetch(const std::string& url) {
    if (!isConnected()) {
        connect();
    }
    std::string path = url;
    // Strip scheme+host if present
    auto pos = path.find("//");
    if (pos != std::string::npos) {
        auto slash = path.find('/', pos + 2);
        path = (slash == std::string::npos) ? "/" : path.substr(slash);
    }
    auto res = http_->Get(path);
    if (res && res->status == 200) {
        return res->body;
    }
    int status = res ? res->status : -1;
    LogManager::instance()->log(LogLevel::Warn, "HTTP fetch failed for {}: status={}", url, status);
    return {};
}

// --- 股票列表 ---

std::vector<StockInfo> AKShareProvider::fetchStockList(Market market) {
    std::vector<StockInfo> result;
    // 东财板块股票列表接口: http://push2.eastmoney.com/api/qt/clist/get
    std::string fs;
    switch (market) {
        case Market::SH: fs = "m:1+t:2"; break;      // 上海主板
        case Market::SZ: fs = "m:0+t:6"; break;      // 深圳主板
        default:
            fs = "m:0+t:6,m:0+t:80,m:1+t:2,m:1+t:23"; // 全A
            break;
    }
    std::ostringstream url;
    url << "http://push2.eastmoney.com/api/qt/clist/get?pn=1&pz=100&po=1&np=1"
        << "&fields=f12,f14,f13,f15,f16,f17,f18,f20,f26"
        << "&fs=" << fs;
    auto body = fetch(url.str());
    if (body.empty()) return result;

    try {
        auto json = nlohmann::json::parse(body);
        auto& data = json["data"];
        if (data.is_null()) return result;
        for (auto& item : data["diff"]) {
            StockInfo info;
            std::string codeStr = item.value("f12", "");
            std::string name = item.value("f14", "");
            if (codeStr.empty() || name.empty()) continue;
            info.code = StockCode(codeStr);
            info.name = name;
            info.board = item.value("f13", "") == "1" ? "沪" : "深";
            info.valid = true;
            result.push_back(std::move(info));
        }
    } catch (const nlohmann::json::exception& e) {
        LogManager::instance()->log(LogLevel::Warn, "Parse stock list failed: {}", e.what());
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

} // namespace st
