#include "data/eastmoney_sector_provider.h"
#include "data/cn_encoding.h"
#include "core/log_manager.h"
#include <nlohmann/json.hpp>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace st {

namespace {

constexpr int kTimeoutMs = 10000;

/// 东财 clist 连续全败次数阈值 — 达到后本轮进程内跳过东财直接新浪，
/// 避免东财被封期间每次刷新都等 4 主机 × 3 重试的超时；重启自动复测东财。
constexpr int kEastMoneyMaxStrikes = 2;
std::atomic<int> g_eastMoneyStrikes{0};

/// 板块列表字段：f2最新价 f3涨跌幅 f6成交额 f8换手率 f12代码 f14名称
///             f104上涨家数 f105下跌家数 f106平盘家数 f128领涨股 f136领涨股涨跌幅
constexpr char kFields[] =
    "f2,f3,f6,f8,f12,f14,f104,f105,f106,f128,f136";

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

std::string EastMoneySectorProvider::fsFor(SectorType type) {
    return type == SectorType::Industry ? "m:90+t:2" : "m:90+t:3";
}

std::vector<SectorBoard> EastMoneySectorProvider::fetchBoards(SectorType type) {
    // 东财 clist 被 IP 级封锁/限流时（HTTP 000 空回复），连续全败达阈值后暂时跳过，
    // 直接降级新浪，避免每次刷新都等 4 主机 × 3 重试。东财恢复即自动回切富数据源。
    if (g_eastMoneyStrikes.load() < kEastMoneyMaxStrikes) {
        auto boards = fetchEastMoneyBoards(type);
        if (!boards.empty()) {
            g_eastMoneyStrikes = 0;
            return boards;
        }
        g_eastMoneyStrikes.fetch_add(1);
        LogManager::instance()->log(LogLevel::Warn,
            "东财板块 clist 不可用（连续 {} 次全败），降级新浪板块行情",
            g_eastMoneyStrikes.load());
    }
    return fetchSinaBoards(type);
}

std::vector<SectorBoard> EastMoneySectorProvider::fetchEastMoneyBoards(SectorType type) {
    // 主域偶发限流（返回空），回退编号子域（东财负载均衡节点，ulist.np 同主机群）
    static const char* kHosts[] = {
        "push2.eastmoney.com",
        "2.push2.eastmoney.com",
        "1.push2.eastmoney.com",
        "3.push2.eastmoney.com",
    };
    constexpr int kPageSize = 100;  // 板块接口每页上限 100
    constexpr int kMaxPages = 20;
    for (const char* host : kHosts) {
        std::vector<SectorBoard> all;
        for (int pn = 1; pn <= kMaxPages; ++pn) {
            const std::string url = std::string("http://") + host +
                "/api/qt/clist/get?po=1&np=1&fltt=2&invt=2&fid=f3&pn=" +
                std::to_string(pn) + "&pz=" + std::to_string(kPageSize) +
                "&fs=" + fsFor(type) + "&fields=" + kFields;
            // 有新浪兜底 → 东财只试 1 次/主机：硬封锁时失败瞬时（空回复），
            // 降低首刷等待；真限流时 4 主机里总有机会命中可用节点。
            const std::string body = fetch(url, /*maxRetries=*/1);
            if (body.empty()) break;
            auto page = parsePage(body);
            if (page.boards.empty()) break;
            for (auto& b : page.boards) all.push_back(std::move(b));
            if (static_cast<int>(all.size()) >= page.total ||
                page.boards.size() < kPageSize) {
                break;
            }
        }
        if (!all.empty()) return all;
    }
    return {};
}

std::string EastMoneySectorProvider::sinaUrlFor(SectorType type) {
    return type == SectorType::Industry
        ? "http://vip.stock.finance.sina.com.cn/q/view/newSinaHy.php"
        : "http://money.finance.sina.com.cn/q/view/newFLJK.php?param=class";
}

std::vector<SectorBoard> EastMoneySectorProvider::fetchSinaBoards(SectorType type) {
    const std::string body = fetch(sinaUrlFor(type));
    if (body.empty()) return {};
    return parseSinaPage(body).boards;
}

SectorBoardPage EastMoneySectorProvider::parseSinaPage(const std::string& gbkBody) {
    SectorBoardPage page;
    try {
        // 新浪返回 GBK；整体先转 UTF-8（GBK 第二字节可能含 0x22/0x5C，先转码再解析 JSON 才安全）
        std::string body = gbkToUtf8(gbkBody);
        const auto eq = body.find('=');
        if (eq == std::string::npos) return page;
        std::string jsonText = body.substr(eq + 1);
        const auto first = jsonText.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return page;
        jsonText = jsonText.substr(first);
        while (!jsonText.empty() && (jsonText.back() == ';' || jsonText.back() == ' ' ||
                                     jsonText.back() == '\r' || jsonText.back() == '\n')) {
            jsonText.pop_back();
        }
        const auto json = nlohmann::json::parse(jsonText);
        if (!json.is_object()) return page;

        auto toNum = [](const std::string& s) -> double {
            if (s.empty()) return 0.0;
            try { return std::stod(s); } catch (...) { return 0.0; }
        };
        for (auto it = json.begin(); it != json.end(); ++it) {
            if (!it.value().is_string()) continue;
            const std::string val = it.value().get<std::string>();
            // 逗号分隔：代码,名称,公司家数,平均价,涨跌额,涨跌幅,量,额,领涨代码,?,?,领涨涨跌幅,领涨名称
            std::vector<std::string> f;
            f.reserve(13);
            size_t start = 0;
            for (size_t i = 0; i <= val.size(); ++i) {
                if (i == val.size() || val[i] == ',') {
                    f.push_back(val.substr(start, i - start));
                    start = i + 1;
                }
            }
            if (f.size() < 8) continue;  // 至少 代码/名称/涨跌幅/成交额
            SectorBoard b;
            b.code = f[0];
            b.name = f[1];
            if (b.code.empty() || b.name.empty()) continue;
            if (f.size() > 3) b.index = toNum(f[3]);        // 平均价（新浪无真实板块指数）
            if (f.size() > 5) b.changePct = toNum(f[5]);
            if (f.size() > 7) b.amount = toNum(f[7]);
            if (f.size() > 11) b.leadingChangePct = toNum(f[11]);
            if (f.size() > 12) b.leadingStock = f[12];      // 领涨股名称
            page.boards.push_back(std::move(b));
        }
    } catch (...) {
        return {};
    }
    return page;
}

SectorBoardPage EastMoneySectorProvider::parsePage(const std::string& body) {
    SectorBoardPage page;
    try {
        const auto json = nlohmann::json::parse(body);
        const auto& data = json.value("data", nlohmann::json());
        if (!data.is_object()) return page;
        if (data.contains("total") && data["total"].is_number()) {
            page.total = data["total"].get<int>();
        }
        const auto& diff = data.value("diff", nlohmann::json());
        if (!diff.is_array() && !diff.is_object()) return page;

        auto appendItem = [&page](const nlohmann::json& it) {
            if (!it.is_object()) return;
            SectorBoard b;
            auto getStr = [&it](const char* key) -> std::string {
                if (!it.contains(key) || !it[key].is_string()) return {};
                return it[key].get<std::string>();
            };
            auto getNum = [&it](const char* key) -> double {
                if (!it.contains(key)) return 0.0;
                if (it[key].is_number()) return it[key].get<double>();
                if (it[key].is_string()) {  // 偶发字符串数字
                    try { return std::stod(it[key].get<std::string>()); }
                    catch (...) { return 0.0; }
                }
                return 0.0;
            };
            b.code = getStr("f12");
            b.name = getStr("f14");
            if (b.code.empty() || b.name.empty()) return;  // 缺核心字段跳过
            b.index = getNum("f2");
            b.changePct = getNum("f3");
            b.amount = getNum("f6");
            b.turnover = getNum("f8");
            b.upCount = static_cast<int>(getNum("f104"));
            b.downCount = static_cast<int>(getNum("f105"));
            b.flatCount = static_cast<int>(getNum("f106"));
            b.leadingStock = getStr("f128");
            b.leadingChangePct = getNum("f136");
            page.boards.push_back(std::move(b));
        };

        if (diff.is_array()) {
            for (const auto& it : diff) appendItem(it);
        } else {
            for (auto it = diff.begin(); it != diff.end(); ++it) appendItem(it.value());
        }
    } catch (...) {
        return {};
    }
    return page;
}

std::vector<SectorBoard> EastMoneySectorProvider::parseBoards(const std::string& body) {
    return parsePage(body).boards;
}

std::string EastMoneySectorProvider::fetch(const std::string& url, int maxRetries) {
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
            if (!body.isEmpty()) return body.toStdString();
        } else {
            LogManager::instance()->log(LogLevel::Warn,
                "板块 fetch 失败 (第 {} 次) {}: {}", attempt + 1,
                reply->errorString().toStdString(), url);
        }
        reply->deleteLater();
    }
    return {};
}

} // namespace st
