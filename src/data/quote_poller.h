#pragma once

#include "foundation/stock_code.h"
#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QByteArray>
#include <vector>
#include <string>
#include <functional>

namespace st {

/// 实时行情轮询器 — 主线程异步轮询已订阅代码的实时行情
///
/// 通过 QTimer + 异步 QNetworkAccessManager（NoProxy）实现，不阻塞主线程。
/// 每轮 tick 按 ≤50 码/请求 分块拉取，响应解析后发布到 EventBus::QuoteReceived。
/// 网络层可注入（测试用），默认走内部异步 QNAM。
class QuotePoller : public QObject {
    Q_OBJECT

public:
    using Fetcher = std::function<QByteArray(const QString& url)>;

    explicit QuotePoller(QObject* parent = nullptr);

    /// 启动周期轮询（毫秒）；启动后立即刷一次
    void start(int intervalMs);
    void stop();
    bool isActive() const { return timer_.isActive(); }

    /// 立即刷新一次（F5）
    void refreshNow();

    void addCode(const StockCode& code);
    void removeCode(const StockCode& code);
    int codeCount() const { return static_cast<int>(subscribed_.size()); }

    /// 注入网络层（测试用）；默认走内部异步 QNAM
    void setFetcher(Fetcher f) { fetcher_ = std::move(f); }

private:
    void tick();
    void pollChunk(const std::vector<StockCode>& chunk);
    void onData(const QByteArray& body);

    QNetworkAccessManager qnam_;
    QTimer timer_;
    std::vector<StockCode> subscribed_;
    bool pollInFlight_ = false;  // 防上一轮未完成就开下一轮（请求堆积）
    int pendingChunks_ = 0;      // 本轮剩余待返回的块数
    Fetcher fetcher_;            // 测试注入；默认空走内部 QNAM
};

} // namespace st
