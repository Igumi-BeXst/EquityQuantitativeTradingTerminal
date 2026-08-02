#include "data/tencent_provider.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <thread>
#include <chrono>
#include <sstream>

namespace st {

namespace {
constexpr int kTimeoutMs = 10000;
}

TencentProvider::TencentProvider()
    : http_(std::make_unique<QNetworkAccessManager>()) {
    // 只影响本数据源，不影响全局 VPN 代理
    http_->setProxy(QNetworkProxy::NoProxy);
}

TencentProvider::~TencentProvider() {
    disconnect();
}

bool TencentProvider::connect() {
    if (!http_) {
        http_ = std::make_unique<QNetworkAccessManager>();
        http_->setProxy(QNetworkProxy::NoProxy);
    }
    connected_ = true;
    LogManager::instance()->log(LogLevel::Info, "TencentProvider connected");
    return true;
}

void TencentProvider::disconnect() {
    if (http_) {
        http_->clearAccessCache();
    }
    connected_ = false;
}

bool TencentProvider::isConnected() const {
    return connected_ && http_ != nullptr;
}

std::string TencentProvider::toTencentCode(const StockCode& code) {
    std::string prefix;
    switch (code.market()) {
        case Market::SH: prefix = "sh"; break;
        case Market::SZ: prefix = "sz"; break;
        case Market::BJ: prefix = "bj"; break;
        case Market::HK: prefix = "hk"; break;
        case Market::US: prefix = "us"; break;
        default: prefix = "sh"; break;
    }
    return prefix + code.code();
}

std::string TencentProvider::fetch(const std::string& url, int maxRetries) {
    if (!isConnected()) {
        connect();
    }
    if (!http_) return {};

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (attempt > 0) {
            // 重试前短暂等待
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        QNetworkRequest request{QUrl(QString::fromStdString(url))};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        request.setRawHeader("Referer", "https://gu.qq.com/");
        request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
        request.setTransferTimeout(kTimeoutMs);

        QEventLoop loop;
        QNetworkReply* reply = http_->get(request);

        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(kTimeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            auto body = reply->readAll();
            reply->deleteLater();
            if (!body.isEmpty()) {
                return body.toStdString();
            }
        } else {
            LogManager::instance()->log(LogLevel::Warn,
                "Tencent fetch failed (attempt {}): {} - {}",
                attempt + 1, url, reply->errorString().toStdString());
        }
        reply->deleteLater();
    }
    return {};
}

std::vector<Bar> TencentProvider::parseDailyKline(const std::string& json,
                                                  const StockCode& code) {
    std::vector<Bar> result;
    if (json.empty()) return result;

    try {
        auto doc = nlohmann::json::parse(json);
        auto& data = doc["data"];
        if (data.is_null()) return result;

        // 键名: qfqday (前复权日线) / day (不复权)
        const nlohmann::json* klines = nullptr;
        auto key = toTencentCode(code);
        if (data.contains(key)) {
            auto& stock = data[key];
            if (stock.contains("qfqday")) {
                klines = &stock["qfqday"];
            } else if (stock.contains("day")) {
                klines = &stock["day"];
            }
        }
        if (!klines) return result;

        for (const auto& k : *klines) {
            if (!k.is_array() || k.size() < 6) continue;
            Bar bar;
            bar.code = code;
            bar.period = BarPeriod::Daily;
            bar.time = utils::parseDate(k[0].get<std::string>());
            // 腾讯价格是字符串（如 "1544.661"），需转 double
            bar.open = k[1].is_number() ? k[1].get<double>() : std::stod(k[1].get<std::string>());
            bar.close = k[2].is_number() ? k[2].get<double>() : std::stod(k[2].get<std::string>());
            bar.high = k[3].is_number() ? k[3].get<double>() : std::stod(k[3].get<std::string>());
            bar.low = k[4].is_number() ? k[4].get<double>() : std::stod(k[4].get<std::string>());
            bar.volume = k[5].is_number()
                ? static_cast<Volume>(k[5].get<double>())
                : static_cast<Volume>(std::stod(k[5].get<std::string>()));
            result.push_back(std::move(bar));
        }
    } catch (const std::exception& e) {
        LogManager::instance()->log(LogLevel::Warn, "Parse Tencent kline failed: {}", e.what());
    }
    return result;
}

std::vector<Bar> TencentProvider::fetchDailyBars(const StockCode& code,
                                                 DateTime start, DateTime end) {
    // param=sh600519,day,开始日期,结束日期,数量,qfq
    // 日期格式 YYYY-MM-DD
    std::string beg = utils::toDateString(start);
    std::string endStr = utils::toDateString(end);
    std::string url = "https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param="
                      + toTencentCode(code) + ",day," + beg + "," + endStr + ",640,qfq";
    auto body = fetch(url);
    return parseDailyKline(body, code);
}

StockInfo TencentProvider::parseQuote(const std::string& body, const StockCode& code) {
    StockInfo info;
    info.code = code;
    // v_sh600519="1~贵州茅台~600519~现价~昨收~今开~成交量~...~名称~代码~..."
    auto pos = body.find('~');
    if (pos == std::string::npos) {
        info.valid = false;
        return info;
    }
    // 用 ~ 分割
    std::vector<std::string> fields;
    std::string token;
    std::istringstream iss(body);
    while (std::getline(iss, token, '~')) {
        fields.push_back(token);
    }
    if (fields.size() >= 2) {
        info.name = fields[1];
        info.valid = !info.name.empty();
    }
    return info;
}

std::optional<StockInfo> TencentProvider::getStockInfo(const StockCode& code) {
    std::string url = "https://qt.gtimg.cn/q=" + toTencentCode(code);
    auto body = fetch(url);
    if (body.empty()) return std::nullopt;
    auto info = parseQuote(body, code);
    if (!info.valid) return std::nullopt;
    return info;
}

std::vector<StockInfo> TencentProvider::getStockList(Market market) {
    // 腾讯批量行情接口获取部分代表性股票
    // 注: 完整股票列表需分板块遍历，此处返回主要指数成分（简化）
    std::vector<StockInfo> result;
    // 沪深300 部分成分 + 常见标的（验证用）
    std::vector<std::string> codes;
    if (market == Market::SH) {
        codes = {"sh600519", "sh601318", "sh600036", "sh600000", "sh600030",
                 "sh601857", "sh600028", "sh601088", "sh600016", "sh600104"};
    } else {
        codes = {"sz000001", "sz000858", "sz000002", "sz000333", "sz000651",
                 "sz300750", "sz002594", "sz000725", "sz300059", "sz002415"};
    }
    std::string url = "https://qt.gtimg.cn/q=";
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) url += ",";
        url += codes[i];
    }
    auto body = fetch(url);
    if (body.empty()) return result;

    // 每条记录形如: v_sh600519="...";
    size_t startPos = 0;
    while (startPos < body.size()) {
        auto vpos = body.find("v_", startPos);
        if (vpos == std::string::npos) break;
        auto eq = body.find('=', vpos);
        if (eq == std::string::npos) break;
        auto semicolon = body.find(';', eq);
        if (semicolon == std::string::npos) break;
        std::string rec = body.substr(eq + 1, semicolon - eq - 1);
        // 从代码反推 StockCode
        std::string tc = body.substr(vpos + 2, eq - vpos - 2);
        std::string codeStr = tc.substr(2);
        StockCode sc(codeStr);
        auto info = parseQuote(rec, sc);
        if (info.valid) {
            result.push_back(std::move(info));
        }
        startPos = semicolon + 1;
    }
    return result;
}

std::vector<Bar> TencentProvider::getBars(const StockCode& code, BarPeriod period,
                                          DateTime start, DateTime end) {
    if (period == BarPeriod::Daily || period == BarPeriod::Weekly ||
        period == BarPeriod::Monthly) {
        return fetchDailyBars(code, start, end);
    }
    // 分钟线暂未实现
    LogManager::instance()->log(LogLevel::Warn,
        "TencentProvider: 分钟线暂未实现 (period={})", static_cast<int>(period));
    return {};
}

void TencentProvider::subscribeQuote(const StockCode&) {}
void TencentProvider::unsubscribeQuote(const StockCode&) {}

} // namespace st
