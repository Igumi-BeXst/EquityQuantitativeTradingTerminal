#include "data/eastmoney_sector_constituents.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

namespace st {

namespace {

/// 线程本地 QNetworkAccessManager（NoProxy）— 任意线程可安全调用（仿 funds provider）
QNetworkAccessManager& threadLocalHttp() {
    static thread_local QNetworkAccessManager qnam;
    static thread_local bool inited = false;
    if (!inited) {
        qnam.setProxy(QNetworkProxy::NoProxy);
        inited = true;
    }
    return qnam;
}

constexpr int kTimeoutMs = 8000;
constexpr int kDefaultPageSize = 100;

} // anonymous namespace

std::string EastMoneySectorConstituents::constituentsUrl(const std::string& boardName,
                                                          int page, int pageSize) {
    // 板块名称需 URL 编码（如 煤炭 → %E7%85%A4%E7%82%AD）
    const auto encoded = QUrl::toPercentEncoding(QString::fromUtf8(boardName.c_str()));
    // datacenter-web 接口：按 BOARD_NAME 过滤查成分股
    return "https://datacenter-web.eastmoney.com/api/data/v1/get"
           "?reportName=RPT_F10_CORETHEME_BOARDTYPE"
           "&columns=SECURITY_CODE,SECURITY_NAME_ABBR"
           "&filter=(BOARD_NAME%3D%22" + encoded.toStdString() + "%22)"
           "&sortColumns=SECURITY_CODE&pageSize=" + std::to_string(pageSize)
         + "&pageNumber=" + std::to_string(page);
}

std::vector<StockCode> EastMoneySectorConstituents::parseConstituents(const std::string& body) {
    std::vector<StockCode> out;
    if (body.empty()) return out;
    try {
        const auto root = nlohmann::json::parse(body);
        const auto& result = root.value("result", nlohmann::json());
        if (result.is_null() || !result.contains("data")) return out;
        for (const auto& item : result["data"]) {
            if (!item.contains("SECURITY_CODE")) continue;
            std::string code;
            if (item["SECURITY_CODE"].is_string()) {
                code = item["SECURITY_CODE"].get<std::string>();
            } else if (item["SECURITY_CODE"].is_number()) {
                code = std::to_string(item["SECURITY_CODE"].get<long long>());
            }
            if (code.empty()) continue;
            // 带市场前缀构造 StockCode（东财 SECURITY_CODE 为 6 位数字）
            StockCode sc;
            if (code.size() == 6) {
                sc = StockCode(code);   // parseFullCode 自动识别 SH/SZ/BJ 前缀
            }
            if (sc.isValid()) out.push_back(sc);
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

std::vector<StockCode> EastMoneySectorConstituents::fetchConstituents(
    const std::string& boardName) const {
    std::vector<StockCode> out;
    if (boardName.empty()) return out;
    // 逐页拉全（最多 20 页 = 2000 只，够任何板块）
    for (int page = 1; page <= 20; ++page) {
        const auto body = fetch(constituentsUrl(boardName, page, kDefaultPageSize));
        if (body.empty()) break;
        auto pageCodes = parseConstituents(body);
        if (pageCodes.empty()) break;   // 无更多数据
        out.insert(out.end(), pageCodes.begin(), pageCodes.end());
        if (pageCodes.size() < kDefaultPageSize) break;   // 不足一页 = 最后一页
    }
    return out;
}

std::string EastMoneySectorConstituents::fetch(const std::string& url, int maxRetries) const {
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
            reply->deleteLater();
        }
    }
    return {};
}

} // namespace st
