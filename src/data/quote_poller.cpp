#include "data/quote_poller.h"
#include "data/tencent_provider.h"
#include "core/event_bus.h"
#include "core/log_manager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>

namespace st {

namespace {
constexpr size_t kChunkSize = 50;          // 腾讯单次批量请求上限
constexpr int kQuoteTimeoutMs = 10000;
const char* kQuoteBaseUrl = "https://qt.gtimg.cn/q=";
}  // namespace

QuotePoller::QuotePoller(QObject* parent) : QObject(parent) {
    qnam_.setProxy(QNetworkProxy::NoProxy);  // 只影响本数据源，不影响全局 VPN
    connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
}

void QuotePoller::start(int intervalMs) {
    if (subscribed_.empty()) return;
    timer_.start(intervalMs);
    // 延迟到当前事件循环结束后立即刷新 —— 等待订阅方一次性订阅多只（如指数条 4 指数）
    QTimer::singleShot(0, this, [this] { refreshNow(); });
}

void QuotePoller::stop() {
    timer_.stop();
    pollInFlight_ = false;
    pendingChunks_ = 0;
}

void QuotePoller::addCode(const StockCode& code) {
    if (!code.isValid()) return;
    for (const auto& c : subscribed_) {
        if (c == code) return;  // 已订阅
    }
    subscribed_.push_back(code);
}

void QuotePoller::removeCode(const StockCode& code) {
    auto it = std::find(subscribed_.begin(), subscribed_.end(), code);
    if (it != subscribed_.end()) subscribed_.erase(it);
}

void QuotePoller::refreshNow() {
    if (subscribed_.empty()) return;
    tick();
}

void QuotePoller::tick() {
    if (pollInFlight_) return;  // 上一轮未完成，跳过本 tick 防堆积
    if (subscribed_.empty()) return;
    pollInFlight_ = true;

    // 按 ≤50 码分块
    std::vector<std::vector<StockCode>> chunks;
    for (size_t i = 0; i < subscribed_.size(); i += kChunkSize) {
        size_t end = std::min(subscribed_.size(), i + kChunkSize);
        chunks.emplace_back(subscribed_.begin() + static_cast<std::ptrdiff_t>(i),
                            subscribed_.begin() + static_cast<std::ptrdiff_t>(end));
    }
    pendingChunks_ = static_cast<int>(chunks.size());
    for (const auto& chunk : chunks) {
        pollChunk(chunk);
    }
}

void QuotePoller::pollChunk(const std::vector<StockCode>& chunk) {
    // 构建批量 URL
    QString url{QLatin1String(kQuoteBaseUrl)};
    for (size_t i = 0; i < chunk.size(); ++i) {
        if (i > 0) url += QLatin1Char(',');
        url += QString::fromStdString(TencentProvider::toTencentCode(chunk[i]));
    }

    if (fetcher_) {
        // 测试注入模式：同步获取（QCoreApplication 环境可用）
        onData(fetcher_(url));
        return;
    }

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    request.setRawHeader("Referer", "https://gu.qq.com/");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
    request.setTransferTimeout(kQuoteTimeoutMs);

    QNetworkReply* reply = qnam_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        QByteArray body = reply->readAll();
        reply->deleteLater();
        onData(body);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError err) {
        Q_UNUSED(err);
        LogManager::instance()->log(LogLevel::Warn,
            "QuotePoller fetch failed: {}", reply->errorString().toStdString());
    });
}

void QuotePoller::onData(const QByteArray& body) {
    if (!body.isEmpty()) {
        auto quotes = TencentProvider::parseQuotes(body.toStdString());
        for (const auto& q : quotes) {
            QVariantMap map;
            map[QStringLiteral("code")]      = QString::fromStdString(q.code.fullCode());
            map[QStringLiteral("lastPrice")] = q.lastPrice;
            map[QStringLiteral("change")]    = q.change;
            map[QStringLiteral("open")]      = q.open;
            map[QStringLiteral("high")]      = q.high;
            map[QStringLiteral("low")]       = q.low;
            map[QStringLiteral("preClose")]  = q.preClose;
            map[QStringLiteral("volume")]    = static_cast<qlonglong>(q.volume);
            map[QStringLiteral("amount")]    = q.amount;
            EventBus::instance()->publish(events::QuoteReceived, map);
        }
    }
    --pendingChunks_;
    if (pendingChunks_ <= 0) {
        pendingChunks_ = 0;
        pollInFlight_ = false;
    }
}

} // namespace st
