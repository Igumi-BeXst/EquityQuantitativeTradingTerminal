#include "intelligence/sentiment/eastmoney_news_provider.h"
#include "core/log_manager.h"
#include <nlohmann/json.hpp>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace st::sentiment {

namespace {

constexpr int kTimeoutMs = 10000;
constexpr char kSearchBase[] =
    "https://search-api-web.eastmoney.com/search/jsonp?cb=jQuery112300&param=";

/// 简单百分号编码（查询串里的 JSON 参数需要）
std::string urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

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

}  // namespace

std::vector<NewsItem> EastMoneyNewsProvider::fetchNews(const StockCode& code, int limit) {
    if (!code.isValid() || limit <= 0) return {};
    // type=["cmsArticleWebOld"]：东财资讯（含股吧/新闻），按关键词=代码搜索
    const std::string param =
        std::string(R"({"uid":"","keyword":")") + code.displayCode() +
        R"(","type":["cmsArticleWebOld"],"client":"web","clientType":"web","clientVersion":"curr",)"
        R"("param":{"cmsArticleWebOld":{"searchScope":"default","sort":"default","pageIndex":1,)"
        R"("pageSize":)" + std::to_string(limit) +
        R"(,"preTag":"","postTag":""}}})";
    const std::string url = std::string(kSearchBase) + urlEncode(param);
    const std::string body = fetch(url);
    if (body.empty()) return {};
    return parseNews(body, limit);
}

std::vector<NewsItem> EastMoneyNewsProvider::parseNews(const std::string& body, int limit) {
    std::vector<NewsItem> out;
    if (limit <= 0) return out;
    // JSONP: cb({...}) → 取首个 '(' 与最后一个 ')' 之间的 JSON
    const size_t open = body.find('(');
    const size_t close = body.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return out;
    }
    try {
        const auto json = nlohmann::json::parse(body.substr(open + 1, close - open - 1));
        const auto& result = json.value("result", nlohmann::json());
        if (!result.is_object()) return out;
        const auto& items = result.value("cmsArticleWebOld", nlohmann::json::array());
        if (!items.is_array()) return out;
        for (const auto& it : items) {
            if (static_cast<int>(out.size()) >= limit) break;
            NewsItem n;
            if (it.contains("title") && it["title"].is_string()) {
                n.title = it["title"].get<std::string>();
            }
            if (it.contains("content") && it["content"].is_string()) {
                n.content = it["content"].get<std::string>();
                if (n.content.size() > 200) n.content.resize(200);
            }
            if (it.contains("mediaName") && it["mediaName"].is_string()) {
                n.source = it["mediaName"].get<std::string>();
            } else if (it.contains("source") && it["source"].is_string()) {
                n.source = it["source"].get<std::string>();
            }
            if (it.contains("date") && it["date"].is_string()) {
                const std::string d = it["date"].get<std::string>();
                n.date = d.substr(0, std::min<size_t>(10, d.size()));
            }
            if (n.title.empty() && n.content.empty()) continue;
            out.push_back(std::move(n));
        }
    } catch (...) {
        return {};
    }
    return out;
}

std::string EastMoneyNewsProvider::fetch(const std::string& url, int maxRetries) {
    auto& http = threadLocalHttp();
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        QNetworkRequest request{QUrl(QString::fromStdString(url))};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        request.setRawHeader("Referer", "https://www.eastmoney.com/");
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
            if (!body.isEmpty()) {
                return body.toStdString();
            }
        } else {
            LogManager::instance()->log(LogLevel::Warn,
                "东财新闻 fetch 失败 (第 {} 次): {}", attempt + 1, url);
        }
        reply->deleteLater();
    }
    return {};
}

} // namespace st::sentiment
