#include "data/tencent_provider.h"
#include "data/curated_stocks.h"
#include "data/quote_poller.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <nlohmann/json.hpp>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace st {

namespace {
constexpr int kTimeoutMs = 10000;
constexpr size_t kQuoteChunk = 50;  // 单次批量行情请求的最大代码数

/// 线程本地 QNetworkAccessManager（NoProxy）— 任意线程可安全调用。
/// 各线程首次使用时在当前线程创建，解决 QNAM 线程亲和问题。
QNetworkAccessManager& threadLocalHttp() {
    static thread_local QNetworkAccessManager qnam;
    static thread_local bool inited = false;
    if (!inited) {
        qnam.setProxy(QNetworkProxy::NoProxy);  // 只影响本数据源，不影响全局 VPN
        inited = true;
    }
    return qnam;
}

/// 容错数值解析（空串/特殊标记 → 0）
double parseNumber(const std::string& s) {
    if (s.empty() || s == "-") return 0.0;
    return std::strtod(s.c_str(), nullptr);
}

/// GBK → UTF-8 转码（腾讯行情接口返回 GBK，Qt6 core 无 QTextCodec）
std::string gbkToUtf8(const std::string& gbk) {
#ifdef _WIN32
    if (gbk.empty()) return {};
    int wlen = MultiByteToWideChar(936, 0, gbk.data(),
                                   static_cast<int>(gbk.size()), nullptr, 0);
    if (wlen <= 0) return gbk;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(936, 0, gbk.data(), static_cast<int>(gbk.size()),
                        &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                                   nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return gbk;
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                        &utf8[0], ulen, nullptr, nullptr);
    return utf8;
#else
    return gbk;
#endif
}

/// 规范化腾讯名称: 去除空格（半角/全角）、全角字母数字转半角。
/// 腾讯返回 "五 粮 液" / "京东方Ａ" 等，影响按名称搜索与展示。
std::string normalizeName(std::string utf8) {
    std::string out;
    out.reserve(utf8.size());
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            if (c == ' ') { ++i; continue; }  // 去半角空格
            out.push_back(utf8[i]);
            ++i;
            continue;
        }
        // UTF-8 多字节解码
        uint32_t cp = 0;
        int len = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else { out.append(utf8, i, 1); ++i; continue; }
        if (i + len > n) break;
        for (int k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        if (len == 2) {
            out.append(utf8, i, static_cast<size_t>(len));
        } else {
            if (cp == 0x3000) {
                // 全角空格去除
            } else if (cp >= 0xFF21 && cp <= 0xFF3A) {
                out.push_back(static_cast<char>('A' + (cp - 0xFF21)));  // Ａ-Ｚ
            } else if (cp >= 0xFF41 && cp <= 0xFF5A) {
                out.push_back(static_cast<char>('a' + (cp - 0xFF41)));  // ａ-ｚ
            } else if (cp >= 0xFF10 && cp <= 0xFF19) {
                out.push_back(static_cast<char>('0' + (cp - 0xFF10)));  // ０-９
            } else {
                out.append(utf8, i, static_cast<size_t>(len));
            }
        }
        i += static_cast<size_t>(len);
    }
    return out;
}

}  // namespace

TencentProvider::TencentProvider() = default;

TencentProvider::~TencentProvider() {
    disconnect();
}

bool TencentProvider::connect() {
    connected_ = true;
    LogManager::instance()->log(LogLevel::Info, "TencentProvider connected");
    return true;
}

void TencentProvider::disconnect() {
    if (poller_) {
        poller_->stop();
    }
    connected_ = false;
}

bool TencentProvider::isConnected() const {
    return connected_;
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
    auto& http = threadLocalHttp();

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
        QNetworkReply* reply = http.get(request);

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
    std::string beg = utils::toDateString(start);
    std::string endStr = utils::toDateString(end);
    std::string url = "https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param="
                      + toTencentCode(code) + ",day," + beg + "," + endStr + ",640,qfq";
    auto body = fetch(url);
    return parseDailyKline(body, code);
}

TencentProvider::ParsedQuote TencentProvider::parseQuoteWithName(
    const std::string& record, const StockCode& code) {
    ParsedQuote pq;
    pq.quote.code = code;
    pq.quote.time = utils::now();

    std::vector<std::string> f;
    std::istringstream iss(record);
    std::string tok;
    while (std::getline(iss, tok, '~')) f.push_back(tok);
    if (f.size() < 4) return pq;

    pq.name = normalizeName(gbkToUtf8(f[1]));

    // 实测绘定的字段索引
    pq.quote.lastPrice = parseNumber(f[3]);
    pq.quote.preClose  = parseNumber(f[4]);
    pq.quote.open      = parseNumber(f[5]);
    pq.quote.volume    = static_cast<Volume>(parseNumber(f[6]) * 100.0);  // 手→股
    if (f.size() > 9)  pq.quote.bidPrice1 = parseNumber(f[9]);
    if (f.size() > 10) pq.quote.bidVol1   = static_cast<Volume>(parseNumber(f[10]) * 100.0);  // 手→股
    if (f.size() > 19) pq.quote.askPrice1 = parseNumber(f[19]);
    if (f.size() > 20) pq.quote.askVol1   = static_cast<Volume>(parseNumber(f[20]) * 100.0);  // 手→股

    // 时间戳锚定相对取值 —— 股票(时间@30)与指数(时间@32)布局不同，用 14 位
    // 时间戳字段定位，之后相对偏移: +1 涨跌额, +2 涨跌幅%, +3 最高, +4 最低, +7 成交额(万)
    int timeIdx = -1;
    for (int i = 0; i < static_cast<int>(f.size()); ++i) {
        const auto& s = f[static_cast<size_t>(i)];
        if (s.size() == 14) {
            bool digits = true;
            for (unsigned char c : s) {
                if (!std::isdigit(c)) { digits = false; break; }
            }
            if (digits) { timeIdx = i; break; }
        }
    }
    if (timeIdx >= 0) {
        const int t = timeIdx;
        if (f.size() > static_cast<size_t>(t + 2)) pq.quote.change = parseNumber(f[static_cast<size_t>(t + 2)]);
        if (f.size() > static_cast<size_t>(t + 3)) pq.quote.high   = parseNumber(f[static_cast<size_t>(t + 3)]);
        if (f.size() > static_cast<size_t>(t + 4)) pq.quote.low    = parseNumber(f[static_cast<size_t>(t + 4)]);
        if (f.size() > static_cast<size_t>(t + 7)) pq.quote.amount = parseNumber(f[static_cast<size_t>(t + 7)]) * 10000.0;
    }
    return pq;
}

Quote TencentProvider::parseQuoteRecord(const std::string& record, const StockCode& code) {
    return parseQuoteWithName(record, code).quote;
}

std::vector<Quote> TencentProvider::parseQuotes(const std::string& body) {
    auto parsed = parseQuoteBatch(body);
    std::vector<Quote> result;
    result.reserve(parsed.size());
    for (auto& p : parsed) result.push_back(std::move(p.quote));
    return result;
}

std::string TencentProvider::parseQuoteName(const std::string& record) {
    // 名称在字段 [1]（~ 分隔）
    auto first = record.find('~');
    if (first == std::string::npos) return {};
    auto end = record.find('~', first + 1);
    if (end == std::string::npos) return {};
    return normalizeName(gbkToUtf8(record.substr(first + 1, end - first - 1)));
}

std::vector<TencentProvider::ParsedQuote> TencentProvider::parseQuoteBatch(
    const std::string& body) {
    std::vector<ParsedQuote> result;
    size_t startPos = 0;
    while (startPos < body.size()) {
        auto vpos = body.find("v_", startPos);
        if (vpos == std::string::npos) break;
        auto eq = body.find('=', vpos);
        if (eq == std::string::npos) break;
        auto semi = body.find(';', eq);
        if (semi == std::string::npos) break;
        std::string rec = body.substr(eq + 1, semi - eq - 1);
        std::string tc = body.substr(vpos + 2, eq - vpos - 2);  // "sh600519" / "sh000001"
        // 市场从前缀解析（自动检测会把 sh000001 误判为 SZ）
        std::string marketPrefix = tc.substr(0, 2);
        Market m = (marketPrefix == "sz") ? Market::SZ : Market::SH;
        std::string codeStr = tc.substr(2);
        StockCode scode(m, codeStr);
        auto pq = parseQuoteWithName(rec, scode);
        if (pq.quote.lastPrice > 0.0 || !pq.name.empty() || !rec.empty()) {
            result.push_back(std::move(pq));
        }
        startPos = semi + 1;
    }
    return result;
}

std::vector<TencentProvider::ParsedQuote> TencentProvider::fetchBatch(
    const std::vector<StockCode>& codes) {
    std::vector<ParsedQuote> result;
    for (size_t n = 0; n < codes.size(); n += kQuoteChunk) {
        std::string u = "https://qt.gtimg.cn/q=";
        bool first = true;
        for (size_t j = n; j < codes.size() && j < n + kQuoteChunk; ++j) {
            if (!first) u += ",";
            first = false;
            u += toTencentCode(codes[j]);
        }
        auto body = fetch(u);
        if (body.empty()) continue;
        auto recs = parseQuoteBatch(body);
        for (auto& r : recs) result.push_back(std::move(r));
    }
    return result;
}

std::vector<Quote> TencentProvider::batchQuote(const std::vector<StockCode>& codes) {
    auto parsed = fetchBatch(codes);
    std::vector<Quote> result;
    result.reserve(parsed.size());
    for (auto& p : parsed) result.push_back(std::move(p.quote));
    return result;
}

std::optional<StockInfo> TencentProvider::getStockInfo(const StockCode& code) {
    std::string url = "https://qt.gtimg.cn/q=" + toTencentCode(code);
    auto body = fetch(url);
    if (body.empty()) return std::nullopt;
    auto parsed = parseQuoteBatch(body);
    if (parsed.empty()) return std::nullopt;
    StockInfo info;
    info.code = parsed[0].quote.code;
    info.name = parsed[0].name;
    info.valid = !info.name.empty();
    if (!info.valid) return std::nullopt;
    return info;
}

std::vector<StockInfo> TencentProvider::getStockList(Market market) {
    std::vector<StockInfo> result;
    const auto& table = (market == Market::SH) ? kCuratedSH : kCuratedSZ;
    if (table.empty()) return result;

    // 内置精选池做骨架
    std::vector<StockCode> codes;
    codes.reserve(table.size());
    for (const auto& c : table) {
        StockCode sc(market, c.code);
        if (sc.isValid()) codes.push_back(std::move(sc));
    }

    // 批量拉取实时名称（真实数据），失败回退静态名称（离线可用）
    std::unordered_map<std::string, std::string> liveName;  // key = displayCode
    auto parsed = fetchBatch(codes);
    for (const auto& p : parsed) {
        liveName[p.quote.code.displayCode()] = p.name;
    }

    for (const auto& c : table) {
        StockInfo info;
        info.code = StockCode(market, c.code);
        info.pinyinInitials = c.initials;
        auto it = liveName.find(info.code.displayCode());
        if (it != liveName.end() && !it->second.empty()) {
            info.name = it->second;  // 实时名称
        } else {
            info.name = c.name;      // 离线回退
        }
        info.valid = info.code.isValid() && !info.name.empty();
        result.push_back(std::move(info));
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

// ============================================================
// 实时行情轮询（QuotePoller 在 Step 2 实现，此处转发）
// ============================================================
void TencentProvider::subscribeQuote(const StockCode& code) {
    if (!poller_) {
        poller_ = std::make_unique<QuotePoller>();
    }
    poller_->addCode(code);
    if (!poller_->isActive()) {
        poller_->start(5000);
    }
}

void TencentProvider::unsubscribeQuote(const StockCode& code) {
    if (poller_) {
        poller_->removeCode(code);
        if (poller_->codeCount() == 0) {
            poller_->stop();
        }
    }
}

void TencentProvider::refreshQuotes() {
    if (poller_) {
        poller_->refreshNow();
    }
}

} // namespace st
