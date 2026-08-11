#pragma once

#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include "data/tdx/tdx_socket.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
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
    std::vector<StockInfo> getSectorIndices() override;  // 通达信板块指数（880行业/885概念）
    std::vector<Bar> getBars(const StockCode& code, BarPeriod period,
                             DateTime start, DateTime end) override;
    std::optional<IntradayData> getIntraday(const StockCode& code) override;
    std::vector<Quote> batchQuote(const std::vector<StockCode>& codes) override;
    /// 交互优先批量报价：用户主动请求（板块面板等）标记为交互，可抢占后台批量刷新
    std::vector<Quote> batchQuoteInteractive(const std::vector<StockCode>& codes);
    std::optional<MarketDepth> getMarketDepth(const StockCode& code) override;
    std::vector<Tick> getTransactions(const StockCode& code, int limit = 50) override;
    /// 全天逐笔成交明细（0x0FC5 分页拉全当日，含买卖方向；最新在前）。
    /// 用于内盘/外盘归属验证与成交明细全貌展示。
    std::vector<Tick> getDayTransactions(const StockCode& code);
    void subscribeQuote(const StockCode& code) override;
    void unsubscribeQuote(const StockCode& code) override;
    void refreshQuotes() override;

    // 配置（默认值读 ConfigManager，setter 供测试/运行时覆盖）
    void setServers(std::vector<std::string> servers);
    void setRequestTimeoutMs(int ms);
    void setQuoteChunkSize(size_t n);
    void setPollIntervalMs(int ms);
    void setTransport(std::unique_ptr<tdx::TdxTransport> transport);
    /// 传输工厂注入（doConnect 用它创建连接，默认 TdxSocket；测试注入 Fake）
    void setTransportFactory(std::function<std::unique_ptr<tdx::TdxTransport>()> factory);

private:
    struct Resp {
        bool ok = false;
        std::vector<uint8_t> payload;  // 已解压
    };

    /// 交互请求（K线/分时/盘口/板块面板）RAII 守卫：在途计数 +1，析构归零并唤醒批量循环。
    /// 批量报价（batchQuote）在 chunk 间等待计数归零 → 交互请求优先插入连接。
    struct InteractiveGuard {
        explicit InteractiveGuard(TdxProvider* p) : p_(p) { p_->interactiveWaiters_.fetch_add(1); }
        ~InteractiveGuard() {
            if (p_->interactiveWaiters_.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lk(p_->bulkMutex_);
                p_->bulkCv_.notify_one();
            }
        }
        TdxProvider* p_;
    };
    /// 批量循环在 chunk 间等待交互请求排空（最多 ~500ms，避免异常时永久暂停）
    void yieldToInteractive();
    /// 批量报价核心循环；yield=true 时 chunk 间让位于交互请求
    std::vector<Quote> batchQuoteImpl(const std::vector<StockCode>& codes, bool yield);

    Resp executeCommand(tdx::Cmd cmd, const std::vector<uint8_t>& req,
                        int timeoutMs);
    bool ensureConnected(std::unique_lock<std::mutex>& lk, int timeoutMs);
    void doConnect();
    void closeSocketLocked();
    void heartbeatLoop();
    void pollLoop();
    void doPollOnce();

    std::vector<Tick> toTicks(const StockCode& code,
                              const std::vector<tdx::TdxTickRec>& recs) const;
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
    /// 连接创建工厂（doConnect 使用，默认 TdxSocket；测试注入 Fake）
    std::function<std::unique_ptr<tdx::TdxTransport>()> transportFactory_;
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
    std::mutex sectorMutex_;
    std::vector<StockInfo> sectorIndices_;  // 板块指数缓存（880/885 过滤）

    std::thread pollThread_, heartbeatThread_;
    std::atomic<bool> stopThreads_ = false;
    // 线程可中断等待（disconnect 立即唤醒，避免 join 阻塞整个轮询周期）
    std::mutex threadMutex_;
    std::condition_variable threadCv_;

    int requestTimeoutMs_ = 10000;
    size_t quoteChunk_ = 80;  // 服务器单次报价批上限（实测 200 只请求返回 80）
    int pollIntervalMs_ = 5000;
    static constexpr uint16_t kMaxBarsPerRequest = 800;

    // ---- 交互优先级（batchQuote 让位于 K线/分时/盘口/板块面板）----
    std::atomic<int> interactiveWaiters_{0};
    std::mutex bulkMutex_;
    std::condition_variable bulkCv_;
};

} // namespace st
