#pragma once

#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include "data/tdx/tdx_socket.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace st {

/// 通达信行情数据源 — 直连通达信行情主站（TCP :7709，非官方私有协议）
///
/// 参考 pytdx / gotdx / injoyai/tdx 的协议实现移植。单连接 + mutex 串行所有请求，
/// 断线自动重连 + 服务器列表 failover。网络调用全在 IO 池线程。
class TdxProvider : public IDataProvider {
public:
    TdxProvider();
    ~TdxProvider() override;

    std::string providerName() const override { return "tdx"; }

    bool connect() override;      // 异步：submitIO(doConnect)，立即返回
    void disconnect() override;
    bool isConnected() const override;

    std::optional<StockInfo> getStockInfo(const StockCode& code) override;
    std::vector<StockInfo> getStockList(Market market) override;
    std::vector<Bar> getBars(const StockCode& code, BarPeriod period,
                             DateTime start, DateTime end) override;
    std::optional<IntradayData> getIntraday(const StockCode& code) override;
    std::vector<Quote> batchQuote(const std::vector<StockCode>& codes) override;
    void subscribeQuote(const StockCode& code) override;
    void unsubscribeQuote(const StockCode& code) override;
    void refreshQuotes() override;

    // 配置（默认值读 ConfigManager，setter 供测试/运行时覆盖）
    void setServers(std::vector<std::string> servers);
    void setRequestTimeoutMs(int ms);
    void setQuoteChunkSize(size_t n);
    void setPollIntervalMs(int ms);
    void setTransport(std::unique_ptr<tdx::TdxTransport> transport);

private:
    struct Resp {
        bool ok = false;
        std::vector<uint8_t> payload;  // 已解压
    };

    Resp executeCommand(tdx::Cmd cmd, const std::vector<uint8_t>& req,
                        int timeoutMs);
    bool ensureConnected(std::unique_lock<std::mutex>& lk, int timeoutMs);
    void doConnect();
    void closeSocketLocked();
    void heartbeatLoop();
    void pollLoop();
    void doPollOnce();

    std::vector<tdx::TdxGbbqRec> ensureGbbq(const StockCode& code);
    std::vector<Bar> fetchBarsRaw(const StockCode& code, BarPeriod period,
                                  uint16_t startIdx, uint16_t count);
    std::vector<Bar> qfqAdjust(std::vector<Bar> bars,
                               const std::vector<tdx::TdxGbbqRec>& gbbq);
    std::vector<StockInfo> loadStockList(Market market);

    // ---- 连接状态（mutex_ + cv_ 保护）----
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    enum class State { Disconnected, Connecting, Connected, Failed };
    State state_ = State::Disconnected;
    std::unique_ptr<tdx::TdxTransport> transport_;
    std::vector<std::string> servers_;
    size_t serverIdx_ = 0;
    std::chrono::steady_clock::time_point lastConnectAttempt_;
    uint32_t nextMsgId_ = 1;

    // ---- 缓存（独立锁）----
    std::mutex subMutex_;
    std::vector<StockCode> subscribed_;
    std::mutex gbbqMutex_;
    std::unordered_map<std::string, std::vector<tdx::TdxGbbqRec>> gbbqCache_;
    std::mutex listMutex_;
    std::unordered_map<int, std::vector<StockInfo>> stockListCache_;

    std::thread pollThread_, heartbeatThread_;
    std::atomic<bool> stopThreads_ = false;

    int requestTimeoutMs_ = 10000;
    size_t quoteChunk_ = 60;
    int pollIntervalMs_ = 5000;
    static constexpr uint16_t kMaxBarsPerRequest = 800;
};

} // namespace st
