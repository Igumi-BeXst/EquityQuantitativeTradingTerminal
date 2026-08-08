#include "data/eastmoney_funds_provider.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <thread>

namespace st {

namespace {

using json = nlohmann::json;

constexpr int kTimeoutMs = 10000;

/// 线程本地 QNetworkAccessManager（NoProxy）— 任意线程可安全调用
QNetworkAccessManager& threadLocalHttp() {
    static thread_local QNetworkAccessManager qnam;
    static thread_local bool inited = false;
    if (!inited) {
        qnam.setProxy(QNetworkProxy::NoProxy);
        inited = true;
    }
    return qnam;
}

DateTime parseDateStr(const json& j, const char* key) {
    if (j.contains(key) && j[key].is_string()) {
        return utils::parseDateTime(j[key].get<std::string>());
    }
    return utils::parseDate("1970-01-01");
}

}  // namespace

// ============================================================
// 龙虎榜
// ============================================================
std::string EastMoneyFundsProvider::dragonTigerUrl(const std::string& date) {
    return "https://datacenter-web.eastmoney.com/api/data/v1/get"
           "?sortColumns=BILLBOARD_NET_AMT&sortTypes=-1&pageSize=100&pageNumber=1"
           "&reportName=RPT_DAILYBILLBOARD_DETAILSNEW&columns=ALL"
           "&filter=(TRADE_DATE%3D%27" + date + "%27)";
}

std::vector<DragonTigerRecord> EastMoneyFundsProvider::parseDragonTiger(const std::string& body) {
    std::vector<DragonTigerRecord> out;
    try {
        const json root = json::parse(body);
        const auto& result = root.value("result", json());
        for (const auto& j : result.value("data", json::array())) {
            if (!j.is_object()) continue;
            DragonTigerRecord r;
            r.code = j.value("SECURITY_CODE", std::string{});
            r.name = j.value("SECURITY_NAME_ABBR", std::string{});
            r.date = parseDateStr(j, "TRADE_DATE");
            r.closePrice = j.value("CLOSE_PRICE", 0.0);
            r.changeRate = j.value("CHANGE_RATE", 0.0);
            r.netAmt = j.value("BILLBOARD_NET_AMT", 0.0);
            r.buyAmt = j.value("BILLBOARD_BUY_AMT", 0.0);
            r.sellAmt = j.value("BILLBOARD_SELL_AMT", 0.0);
            r.turnoverRate = j.value("TURNOVER_RATE", 0.0);
            r.reason = j.value("EXPLANATION", std::string{});
            if (!r.code.empty()) out.push_back(std::move(r));
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

// ============================================================
// 融资融券
// ============================================================
std::string EastMoneyFundsProvider::marginUrl(const std::string& code) {
    return "https://datacenter-web.eastmoney.com/api/data/v1/get"
           "?sortColumns=DATE&sortTypes=-1&pageSize=120&pageNumber=1"
           "&reportName=RPTA_WEB_RZRQ_GGMX&columns=ALL"
           "&filter=(SCODE%3D%22" + code + "%22)";
}

std::vector<MarginRecord> EastMoneyFundsProvider::parseMargin(const std::string& body) {
    std::vector<MarginRecord> out;
    try {
        const json root = json::parse(body);
        const auto& result = root.value("result", json());
        for (const auto& j : result.value("data", json::array())) {
            if (!j.is_object()) continue;
            MarginRecord r;
            r.date = parseDateStr(j, "DATE");
            r.market = j.value("MARKET", std::string{});
            r.code = j.value("SCODE", std::string{});
            r.name = j.value("SECNAME", std::string{});
            r.financeBalance = j.value("RZYE", 0.0);
            r.shortBalance = j.value("RQYE", 0.0);
            r.marginBalance = j.value("RZRQYE", 0.0);
            r.financeBuy = j.value("RZMRE", 0.0);
            if (!r.code.empty()) out.push_back(std::move(r));
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

std::string EastMoneyFundsProvider::marginMarketUrl() {
    return "https://datacenter-web.eastmoney.com/api/data/v1/get"
           "?sortColumns=DIM_DATE&sortTypes=-1&pageSize=120&pageNumber=1"
           "&reportName=RPTA_RZRQ_LSHJ&columns=ALL";
}

std::vector<MarginMarketRecord> EastMoneyFundsProvider::parseMarginMarket(const std::string& body) {
    std::vector<MarginMarketRecord> out;
    try {
        const json root = json::parse(body);
        const auto& result = root.value("result", json());
        for (const auto& j : result.value("data", json::array())) {
            if (!j.is_object()) continue;
            MarginMarketRecord r;
            r.date = parseDateStr(j, "DIM_DATE");
            r.financeBalance = j.value("RZYE", 0.0);
            r.marginBalance = j.value("RZRQYE", 0.0);
            out.push_back(std::move(r));
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

// ============================================================
// fetch（thread_local QNAM + QEventLoop 同步；多主机回退）
// ============================================================
std::string EastMoneyFundsProvider::fetch(const std::string& url, int maxRetries) {
    auto& http = threadLocalHttp();
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        QNetworkRequest request{QUrl(QString::fromStdString(url))};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        request.setRawHeader("Referer", "https://data.eastmoney.com/");
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
                "资金数据 fetch 失败 (第 {} 次) {}: {}", attempt + 1,
                reply->errorString().toStdString(), url);
        }
        reply->deleteLater();
    }
    return {};
}

std::vector<DragonTigerRecord> EastMoneyFundsProvider::fetchDragonTiger(const std::string& date) {
    return parseDragonTiger(fetch(dragonTigerUrl(date)));
}

std::vector<MarginRecord> EastMoneyFundsProvider::fetchMargin(const std::string& code) {
    return parseMargin(fetch(marginUrl(code)));
}

std::vector<MarginMarketRecord> EastMoneyFundsProvider::fetchMarginMarket() {
    return parseMarginMarket(fetch(marginMarketUrl()));
}

} // namespace st
