#include "data/tdx/tdx_provider.h"
#include "data/tdx/tdx_protocol.h"
#include "data/cn_encoding.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "core/event_bus.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/pinyin.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <thread>

namespace st {

namespace {

// 公开通达信行情主站（IP 可能变更，失败自动切换，config data.tdx.servers 可覆盖）
const std::vector<std::string> kDefaultServers = {
    "124.71.187.122:7709", "180.153.18.170:7709",
    "60.12.136.250:7709",  "218.75.126.9:7709",
    "115.238.90.165:7709", "119.147.212.81:7709",
    "101.227.73.20:7709",  "221.231.141.60:7709",
};

bool isMinutePeriod(BarPeriod p) {
    return p == BarPeriod::Minute1 || p == BarPeriod::Minute5 ||
           p == BarPeriod::Minute15 || p == BarPeriod::Minute30 ||
           p == BarPeriod::Minute60;
}

}  // namespace

TdxProvider::TdxProvider()
    : transport_(std::make_unique<tdx::TdxSocket>()),
      transportFactory_([] { return std::make_unique<tdx::TdxSocket>(); }),
      servers_(kDefaultServers) {}

TdxProvider::~TdxProvider() {
    disconnect();
}

void TdxProvider::setServers(std::vector<std::string> servers) {
    std::lock_guard<std::mutex> lk(mutex_);
    servers_ = std::move(servers);
}
void TdxProvider::setRequestTimeoutMs(int ms) { requestTimeoutMs_ = ms; }
void TdxProvider::setQuoteChunkSize(size_t n) { quoteChunk_ = std::max<size_t>(1, n); }
void TdxProvider::setPollIntervalMs(int ms) { pollIntervalMs_ = std::max(100, ms); }
void TdxProvider::setTransport(std::unique_ptr<tdx::TdxTransport> transport) {
    std::lock_guard<std::mutex> lk(mutex_);
    transport_ = std::move(transport);
}

void TdxProvider::setTransportFactory(std::function<std::unique_ptr<tdx::TdxTransport>()> factory) {
    std::lock_guard<std::mutex> lk(mutex_);
    transportFactory_ = std::move(factory);
}

bool TdxProvider::connect() {
    stopThreads_ = false;  // 重新运行标记（disconnect 后不再重置，重连由 connect 恢复）
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (state_ == State::Connected) return true;
        if (state_ != State::Connecting) {
            state_ = State::Connecting;
            std::thread([this] { doConnect(); }).detach();
        }
    }
    // 启动心跳与订阅轮询
    if (!heartbeatThread_.joinable()) {
        heartbeatThread_ = std::thread([this] { heartbeatLoop(); });
    }
    if (!pollThread_.joinable()) {
        pollThread_ = std::thread([this] { pollLoop(); });
    }
    return true;
}

void TdxProvider::disconnect() {
    {
        std::lock_guard<std::mutex> lk(threadMutex_);
        stopThreads_ = true;
    }
    threadCv_.notify_all();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (transport_) transport_->close();
        state_ = State::Disconnected;
        cv_.notify_all();
    }
    if (heartbeatThread_.joinable()) heartbeatThread_.join();
    if (pollThread_.joinable()) pollThread_.join();
    // 注意：不再重置 stopThreads_=false。若在 close 窗口内 detached doConnect 线程
    // 仍在尝试连接，keep true 使它在获取 mutex_ 后立即退出，避免 provider 析构后
    // 该线程访问已释放的 this → 关闭时堆损坏。
}

bool TdxProvider::isConnected() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_ == State::Connected;
}

// ============================================================
// 连接状态机
// ============================================================
bool TdxProvider::ensureConnected(std::unique_lock<std::mutex>& lk, int timeoutMs) {
    if (stopThreads_.load()) return false;  // 已 disconnect，不再触发重连（防 detached 线程悬垂）
    if (state_ == State::Connected && transport_) return true;
    if (state_ != State::Connecting) {
        state_ = State::Connecting;
        std::thread([this] { doConnect(); }).detach();
    }
    return cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                        [this] { return state_ == State::Connected; });
}

void TdxProvider::doConnect() {
    const std::lock_guard<std::mutex> lk(mutex_);
    for (size_t i = 0; i < servers_.size(); ++i) {
        if (stopThreads_) break;
        const size_t idx = (serverIdx_ + i) % servers_.size();
        const std::string& addr = servers_[idx];
        const size_t colon = addr.rfind(':');
        const std::string host = addr.substr(0, colon);
        const int port = (colon != std::string::npos)
            ? std::atoi(addr.substr(colon + 1).c_str()) : 7709;

        auto transport = transportFactory_();
        if (!transport) continue;
        if (!transport->open(host, port, 3000)) continue;

        // 登录握手 0x000D
        auto connectFrame = tdx::encodeRequest(tdx::Cmd::Connect,
                                               tdx::buildConnectReq(), nextMsgId_++);
        if (!transport->sendAll(connectFrame.data(), connectFrame.size())) {
            transport->close();
            continue;
        }
        std::vector<uint8_t> raw;
        if (!transport->recvFrame(raw, 3000)) {
            transport->close();
            continue;
        }
        const auto resp = tdx::decodeResponse(raw);
        if (!resp.ok) {
            transport->close();
            continue;
        }
        transport_ = std::move(transport);
        serverIdx_ = idx;
        state_ = State::Connected;
        cv_.notify_all();
        LogManager::instance()->log(LogLevel::Info,
            "TDX 已连接: {} (第 {} 个服务器)", addr, idx);
        return;
    }
    state_ = State::Failed;
    cv_.notify_all();
    LogManager::instance()->log(LogLevel::Error,
        "TDX 全部服务器连接失败 ({})", servers_.size());
}

void TdxProvider::closeSocketLocked() {
    if (transport_) transport_->close();
}

TdxProvider::Resp TdxProvider::executeCommand(tdx::Cmd cmd,
                                              const std::vector<uint8_t>& req,
                                              int timeoutMs) {
    std::unique_lock<std::mutex> lk(mutex_);
    if (!ensureConnected(lk, timeoutMs)) return {};
    if (!transport_) return {};
    const auto frame = tdx::encodeRequest(cmd, req, nextMsgId_++);
    if (!transport_->sendAll(frame.data(), frame.size())) {
        closeSocketLocked();
        state_ = State::Disconnected;
        cv_.notify_all();
        return {};
    }
    std::vector<uint8_t> rawFrame;
    if (!transport_->recvFrame(rawFrame, timeoutMs)) {
        closeSocketLocked();
        state_ = State::Disconnected;
        cv_.notify_all();
        return {};
    }
    const auto resp = tdx::decodeResponse(rawFrame);
    Resp r;
    if (resp.ok) {
        r.ok = true;
        r.payload = resp.payload;
    }
    return r;
}

void TdxProvider::heartbeatLoop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lk(threadMutex_);
            if (stopThreads_.load()) break;
            if (threadCv_.wait_for(lk, std::chrono::seconds(30),
                                   [this] { return stopThreads_.load(); })) {
                break;  // 被唤醒 = 停止
            }
        }
        executeCommand(tdx::Cmd::Heart, tdx::buildHeartReq(), 5000);
    }
}

// ============================================================
// K线
// ============================================================
std::vector<Bar> TdxProvider::fetchBarsRaw(const StockCode& code, BarPeriod period,
                                           uint16_t startIdx, uint16_t count) {
    const int mkt = tdx::tdxMarket(code.market());
    const int cat = tdx::klineCategory(period);
    if (mkt < 0 || cat < 0) return {};
    const auto req = tdx::buildKlineReq(static_cast<uint8_t>(mkt), code.code(),
                                        static_cast<uint8_t>(cat), startIdx, count);
    const auto resp = executeCommand(tdx::Cmd::Kline, req, requestTimeoutMs_);
    if (!resp.ok) return {};

    const auto recs = tdx::decodeKline(resp.payload, static_cast<uint8_t>(cat),
                                       tdx::isIndexCode(code) || tdx::isSectorIndexCode(code));
    std::vector<Bar> bars;
    bars.reserve(recs.size());
    for (const auto& r : recs) {
        Bar b;
        b.code = code;
        b.period = period;
        b.time = r.time;
        b.open = r.open;
        b.high = r.high;
        b.low = r.low;
        b.close = r.close;
        b.volume = static_cast<Volume>(r.volume);
        b.amount = r.amount;
        bars.push_back(b);
    }
    return bars;
}

std::vector<Bar> TdxProvider::getBars(const StockCode& code, BarPeriod period,
                                      DateTime start, DateTime end) {
    if (tdx::tdxMarket(code.market()) < 0 || tdx::klineCategory(period) < 0) {
        return {};
    }
    const bool isEpoch = start < utils::parseDate("2000-01-01");
    const uint16_t desired = static_cast<uint16_t>(isMinutePeriod(period) ? 320 : 640);

    std::vector<Bar> all;
    uint16_t startIdx = 0;
    while (startIdx < 10000) {
        auto seg = fetchBarsRaw(code, period, startIdx, kMaxBarsPerRequest);
        if (seg.empty()) break;
        all.insert(all.end(), seg.begin(), seg.end());
        if (seg.size() < kMaxBarsPerRequest) break;
        const DateTime oldest = seg.front().time;
        if (!isEpoch && oldest <= start) break;
        startIdx += kMaxBarsPerRequest;
    }

    // 合并排序去重
    std::sort(all.begin(), all.end(),
              [](const Bar& a, const Bar& b) { return a.time < b.time; });
    all.erase(std::unique(all.begin(), all.end(),
                          [](const Bar& a, const Bar& b) { return a.time == b.time; }),
              all.end());

    if (isEpoch) {
        if (all.size() > desired) {
            all.erase(all.begin(), all.end() - static_cast<long>(desired));
        }
    } else {
        all.erase(std::remove_if(all.begin(), all.end(),
                                 [&](const Bar& b) { return b.time < start || b.time > end; }),
                  all.end());
    }

    // 日/周/月/季/年前复权
    if (period == BarPeriod::Daily || period == BarPeriod::Weekly ||
        period == BarPeriod::Monthly || period == BarPeriod::Quarterly ||
        period == BarPeriod::Yearly) {
        all = qfqAdjust(std::move(all), ensureGbbq(code));
    }
    return all;
}

// ============================================================
// 复权（gbbq 仿射变换）
// ============================================================
std::vector<tdx::TdxGbbqRec> TdxProvider::ensureGbbq(const StockCode& code) {
    {
        std::lock_guard<std::mutex> lk(gbbqMutex_);
        auto it = gbbqCache_.find(code.fullCode());
        if (it != gbbqCache_.end()) return it->second;
    }
    const int mkt = tdx::tdxMarket(code.market());
    if (mkt < 0) return {};
    const auto req = tdx::buildGbbqReq(static_cast<uint8_t>(mkt), code.code());
    const auto resp = executeCommand(tdx::Cmd::Gbbq, req, requestTimeoutMs_);
    std::vector<tdx::TdxGbbqRec> recs;
    if (resp.ok) recs = tdx::decodeGbbq(resp.payload);
    std::lock_guard<std::mutex> lk(gbbqMutex_);
    gbbqCache_[code.fullCode()] = recs;
    return recs;
}

std::vector<Bar> TdxProvider::qfqAdjust(std::vector<Bar> bars,
                                        const std::vector<tdx::TdxGbbqRec>& gbbq) {
    // 只收集除权除息事件（category==1），按日期降序
    std::vector<const tdx::TdxGbbqRec*> events;
    for (const auto& g : gbbq) {
        if (g.category == 1) events.push_back(&g);
    }
    if (events.empty()) return bars;
    std::sort(events.begin(), events.end(),
              [](const tdx::TdxGbbqRec* a, const tdx::TdxGbbqRec* b) {
                  return a->date > b->date;  // 新→旧
              });

    // 前复权仿射变换：对每根 bar（升序），从最新事件开始累积因子，
    // 直到事件除权日 <= bar 交易日。单事件参考价 P_adj = (P - c)/m。
    const auto barDateOf = [](const Bar& b) -> uint32_t {
        const std::time_t tt = std::chrono::system_clock::to_time_t(b.time);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &tt);
#else
        localtime_r(&tt, &tmv);
#endif
        return static_cast<uint32_t>(
            (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday);
    };

    for (auto& bar : bars) {
        const uint32_t barDate = barDateOf(bar);
        double M = 1.0, A = 0.0;
        for (const auto* e : events) {  // 事件按日期降序
            if (e->date <= barDate) break;  // 该事件不影响 bar 之前的价格
            const double m = (10.0 + e->songZhuanGu + e->peiGu) / 10.0;
            const double c = (e->fenHong - e->peiGu * e->peiGuJia) / 10.0;
            if (m > 1e-12) {
                M = M / m;
                A = (A - c) / m;
            }
        }
        const auto adj = [&](double v) {
            return std::round((v * M + A) * 100.0) / 100.0;
        };
        bar.open = adj(bar.open);
        bar.high = adj(bar.high);
        bar.low = adj(bar.low);
        bar.close = adj(bar.close);
    }
    return bars;
}

// ============================================================
// 分时
// ============================================================
std::optional<IntradayData> TdxProvider::getIntraday(const StockCode& code) {
    // 0x0FC5 逐笔成交明细 → 分页拉全当日 → 按分钟聚合 → IntradayData。
    // （0x051D 当日分时响应在此服务器为非标准变体，pytdx 同失败，弃用）
    const int mkt = tdx::tdxMarket(code.market());
    if (mkt < 0) return std::nullopt;

    std::vector<tdx::TdxTickRec> ticks;
    uint32_t start = 0;
    constexpr uint16_t kPage = 1000;
    while (start <= 20000) {  // 安全上限（≥20 页 ≈ 200万手）
        const auto req = tdx::buildTransactionReq(static_cast<uint8_t>(mkt), code.code(),
                                                  static_cast<uint16_t>(start), kPage);
        const auto resp = executeCommand(tdx::Cmd::MinuteTrade, req, requestTimeoutMs_);
        if (!resp.ok) break;
        auto page = tdx::decodeTransaction(resp.payload);
        if (page.empty()) break;
        ticks.insert(ticks.end(), page.begin(), page.end());
        if (page.size() < kPage) break;
        start += kPage;
    }
    if (ticks.empty()) return std::nullopt;
    const bool isIndex = tdx::isIndexCode(code);

    // 按分钟聚合（09:30=0..11:30=119, 13:00=120..15:00=239）
    struct MinuteAgg {
        double open = 0.0, high = 0.0, low = 0.0, close = 0.0;
        double vol = 0.0;    // 股
        double amount = 0.0; // 元
        int n = 0;
    };
    std::map<int, MinuteAgg> mins;
    for (const auto& t : ticks) {
        const int minsOfDay = t.hour * 60 + t.minute;
        if (minsOfDay < 9 * 60 + 25) continue;                 // 09:25 集合竞价计入第 1 分钟（开盘价/均价线需要）
        if (minsOfDay >= 12 * 60 && minsOfDay < 13 * 60) continue;  // 午休
        if (minsOfDay > 15 * 60) continue;                      // 盘后异常记录丢弃（防末分钟量膨胀）
        const int idx = (minsOfDay < 13 * 60)
            ? std::min(std::max(minsOfDay - 570, 0), 119)
            : std::min(120 + minsOfDay - 780, 239);
        auto& a = mins[idx];
        if (a.n == 0) { a.open = t.price; a.low = t.price; }
        a.high = std::max(a.high, t.price);
        a.low = std::min(a.low, t.price);
        a.close = t.price;
        if (isIndex) {
            // 指数 0x0FC5 无成交量字段：volume 字段实为「成交额/100（百元）」→ 按分钟累计成交额
            a.amount += t.volume * 100.0;  // 元
        } else {
            const double shares = t.volume * 100.0;            // 手→股
            a.vol += shares;
            a.amount += shares * t.price;
        }
        ++a.n;
    }
    if (mins.empty()) return std::nullopt;

    // 日K最新一根：确定实际交易日（分时数据来自最近交易日，不能直接用 utils::now()）
    const auto dayBars = fetchBarsRaw(code, BarPeriod::Daily, 0, 1);
    const DateTime tradeDate = (!dayBars.empty()) ? dayBars[0].time : utils::now();

    // 指数无成交量字段 → 用当日日均价（日K量额比）把每分成交额换算成成交量
    if (isIndex && !dayBars.empty() && dayBars[0].volume > 0 && dayBars[0].amount > 0) {
        const double volPerYuan = static_cast<double>(dayBars[0].volume) / dayBars[0].amount;
        for (auto& kv : mins) kv.second.vol = kv.second.amount * volPerYuan;
    }

    // 昨收（发一次报价）
    double preClose = 0.0;
    const auto quotes = batchQuote({code});
    if (!quotes.empty()) preClose = quotes.front().preClose;

    IntradayData data;
    data.code = code;
    data.date = tradeDate;
    data.preClose = preClose;
    // 满 240 点数组（缺分钟价格 carry-forward、量不变），使量柱=单分钟量、均价/MACD 连续。
    // 只建到最后一个有成交的分钟——未来分钟不生成点，避免未到时间在图上拉一条直线。
    const int lastIdxWithData = mins.rbegin()->first;
    double cumVol = 0.0, cumAmt = 0.0;
    double lastPrice = 0.0;
    for (int idx = 0; idx <= lastIdxWithData; ++idx) {
        const auto it = mins.find(idx);
        if (it != mins.end()) {
            const auto& a = it->second;
            cumVol += a.vol;
            cumAmt += a.amount;
            // 第 1 分钟显示开盘价（含集合竞价），其余分钟显示该分钟收盘价
            lastPrice = (idx == 0) ? a.open : a.close;
        }
        if (lastPrice <= 0) continue;  // 尚未开盘
        IntradayPoint pt;
        const std::time_t nowTt = std::chrono::system_clock::to_time_t(data.date);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &nowTt);
#else
        localtime_r(&nowTt, &tmv);
#endif
        if (idx < 120) { tmv.tm_hour = 9; tmv.tm_min = 30 + idx; }
        else { tmv.tm_hour = 13; tmv.tm_min = idx - 120; }
        tmv.tm_sec = 0;
        const std::time_t ptTt = std::mktime(&tmv);
        pt.time = std::chrono::system_clock::from_time_t(ptTt);
        pt.price = lastPrice;                    // 缺分钟 carry-forward
        pt.volume = static_cast<Volume>(cumVol); // 累计量（与腾讯语义一致）
        pt.amount = cumAmt;                      // 累计额
        data.points.push_back(std::move(pt));
    }
    return data;
}

// ============================================================
// 实时报价
// ============================================================
std::vector<Quote> TdxProvider::batchQuote(const std::vector<StockCode>& codes) {
    std::vector<Quote> out;
    out.reserve(codes.size());
    std::vector<std::pair<uint8_t, std::string>> chunk;
    chunk.reserve(quoteChunk_);
    auto flush = [&]() {
        if (chunk.empty()) return;
        const auto req = tdx::buildQuoteReq(chunk);
        const auto resp = executeCommand(tdx::Cmd::Quote, req, requestTimeoutMs_);
        if (resp.ok) {
            for (const auto& r : tdx::decodeQuote(resp.payload)) {
                Quote q;
                q.code = r.code;
                q.time = utils::now();
                q.lastPrice = r.price;
                q.open = r.open;
                q.high = r.high;
                q.low = r.low;
                q.preClose = r.preClose;
                q.volume = static_cast<Volume>(r.volume);
                q.amount = r.amount;
                q.change = r.preClose > 0
                    ? (r.price - r.preClose) / r.preClose * 100.0 : 0.0;
                q.outerVol = static_cast<Volume>(r.bVol);  // 外盘（主动买）
                q.innerVol = static_cast<Volume>(r.sVol);  // 内盘（主动卖）
                out.push_back(q);
            }
        }
        chunk.clear();
    };
    for (const auto& code : codes) {
        const int mkt = tdx::tdxMarket(code.market());
        if (mkt < 0) continue;
        chunk.emplace_back(static_cast<uint8_t>(mkt), code.code());
        if (chunk.size() >= quoteChunk_) {
            flush();
            // 每个 chunk 间让出片刻：TDX 连接为每命令加锁，等待中的交互请求
            // （选股 getBars / 盘口）得以插入，避免全市场批量报价长时间独占连接。
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    flush();
    return out;
}

std::optional<MarketDepth> TdxProvider::getMarketDepth(const StockCode& code) {
    const int mkt = tdx::tdxMarket(code.market());
    if (mkt < 0) return std::nullopt;
    const auto req = tdx::buildQuoteReq({{static_cast<uint8_t>(mkt), code.code()}});
    const auto resp = executeCommand(tdx::Cmd::Quote, req, requestTimeoutMs_);
    if (!resp.ok) return std::nullopt;
    const auto recs = tdx::decodeQuote(resp.payload);
    if (recs.empty()) return std::nullopt;

    const auto& r = recs.front();
    MarketDepth md;
    md.code = r.code;
    md.time = utils::now();
    for (int k = 0; k < 5; ++k) {
        md.bids[k] = {r.bids[k].price, static_cast<Volume>(r.bids[k].volume)};
        md.asks[k] = {r.asks[k].price, static_cast<Volume>(r.asks[k].volume)};
    }
    return md;
}

std::vector<Tick> TdxProvider::getTransactions(const StockCode& code, int limit) {
    const int mkt = tdx::tdxMarket(code.market());
    if (mkt < 0 || limit <= 0) return {};
    const auto req = tdx::buildTransactionReq(static_cast<uint8_t>(mkt), code.code(), 0,
                                              static_cast<uint16_t>(limit));
    const auto resp = executeCommand(tdx::Cmd::MinuteTrade, req, requestTimeoutMs_);
    if (!resp.ok) return {};
    auto recs = tdx::decodeTransaction(resp.payload);
    // 页内按时间升序（最新=收盘在最后）→ 只留最近 limit 条并反转（最新在前）
    if (recs.size() > static_cast<size_t>(limit)) {
        recs.erase(recs.begin(), recs.end() - static_cast<ptrdiff_t>(limit));
    }
    return toTicks(code, recs);
}

std::vector<Tick> TdxProvider::getDayTransactions(const StockCode& code) {
    const int mkt = tdx::tdxMarket(code.market());
    if (mkt < 0) return {};
    // 0x0FC5：start 为「从当日末尾倒数」的偏移（start=0 最新），分页向前拉全当日
    std::vector<tdx::TdxTickRec> recs;
    uint32_t start = 0;
    constexpr uint16_t kPage = 1000;
    while (start <= 20000) {  // 安全上限（≥20 页 ≈ 2000 万手）
        const auto req = tdx::buildTransactionReq(static_cast<uint8_t>(mkt), code.code(),
                                                  static_cast<uint16_t>(start), kPage);
        const auto resp = executeCommand(tdx::Cmd::MinuteTrade, req, requestTimeoutMs_);
        if (!resp.ok) break;
        auto page = tdx::decodeTransaction(resp.payload);
        if (page.empty()) break;
        recs.insert(recs.end(), page.begin(), page.end());
        if (page.size() < kPage) break;
        start += kPage;
    }
    return toTicks(code, recs);
}

std::vector<Tick> TdxProvider::toTicks(const StockCode& code,
                                       const std::vector<tdx::TdxTickRec>& recs) const {
    const std::string today = utils::toDateString(utils::now());
    std::vector<Tick> out;
    out.reserve(recs.size());
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
        Tick t;
        t.code = code;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s %02d:%02d:00", today.c_str(), it->hour, it->minute);
        t.time = utils::parseDateTime(buf);
        t.price = it->price;
        t.volume = static_cast<Volume>(it->volume * 100.0);  // 手 → 股
        t.direction = (it->buyorsell == 1) ? Direction::Sell : Direction::Buy;
        out.push_back(t);
    }
    return out;
}

// ============================================================
// 股票列表 / 基本信息
// ============================================================
std::vector<StockInfo> TdxProvider::loadStockList(Market market) {
    const int mkt = tdx::tdxMarket(market);
    if (mkt < 0) return {};
    {
        std::lock_guard<std::mutex> lk(listMutex_);
        auto it = stockListCache_.find(mkt);
        if (it != stockListCache_.end()) return it->second;
    }
    std::vector<StockInfo> infos;
    const auto countResp = executeCommand(tdx::Cmd::Count,
                                          tdx::buildCountReq(static_cast<uint8_t>(mkt)),
                                          requestTimeoutMs_);
    const uint32_t total = countResp.ok ? tdx::decodeCount(countResp.payload) : 0;

    uint32_t start = 0;
    // 加载完整列表（Count 返回真实总数）；安全上限防异常值。
    // start 以 uint16 编码进请求，必须 ≤65535 防回绕（否则 while 永不终止）。
    const uint32_t maxStocks = (total > 0 && total < 100000) ? total : 100000;
    while (start < maxStocks && start <= 65535u) {
        const auto req = tdx::buildCodeReq(static_cast<uint8_t>(mkt),
                                           static_cast<uint16_t>(start));
        const auto resp = executeCommand(tdx::Cmd::Code, req, requestTimeoutMs_);
        if (!resp.ok) break;
        const auto recs = tdx::decodeCodeList(resp.payload, market);
        if (recs.empty()) break;
        for (const auto& r : recs) {
            if (!r.code.isValid()) continue;
            StockInfo info;
            info.code = r.code;
            info.name = r.name;
            info.pinyinInitials = utils::pinyinInitials(r.name);
            info.pinyin = utils::pinyinFull(r.name);
            info.exchange = (r.code.market() == Market::SH) ? "SH" : "SZ";
            info.valid = true;
            infos.push_back(std::move(info));
        }
        if (recs.size() < 1000) break;
        start += 1000;
    }
    std::lock_guard<std::mutex> lk(listMutex_);
    stockListCache_[mkt] = infos;
    return infos;
}

std::vector<StockInfo> TdxProvider::getStockList(Market market) {
    return loadStockList(market);
}

std::vector<StockInfo> TdxProvider::getSectorIndices() {
    {
        std::lock_guard<std::mutex> lk(sectorMutex_);
        if (!sectorIndices_.empty()) return sectorIndices_;
    }
    // 通达信板块指数（880xxx）位于 SH 列表开头，定向抓取前几页即可，避免全量 30s。
    // 一直拉取直到某页不再含 880xxx（板块块结束）。失败返回空且不缓存（下次重试，
    // 避免连接未就绪时误缓存空列表导致永久空）。
    const int mkt = tdx::tdxMarket(Market::SH);
    if (mkt < 0) return {};
    std::vector<StockInfo> out;
    out.reserve(256);
    uint32_t start = 0;
    while (start <= 65535u) {
        const auto req = tdx::buildCodeReq(static_cast<uint8_t>(mkt),
                                           static_cast<uint16_t>(start));
        const auto resp = executeCommand(tdx::Cmd::Code, req, requestTimeoutMs_);
        if (!resp.ok) break;
        const auto recs = tdx::decodeCodeList(resp.payload, Market::SH);
        if (recs.empty()) break;
        bool sawSector = false;
        for (const auto& r : recs) {
            if (!r.code.isValid() || !tdx::isSectorIndexCode(r.code)) continue;
            sawSector = true;
            StockInfo info;
            info.code = r.code;
            info.name = r.name;
            info.pinyinInitials = utils::pinyinInitials(r.name);
            info.pinyin = utils::pinyinFull(r.name);
            info.exchange = "SH";
            info.valid = true;
            out.push_back(std::move(info));
        }
        if (!sawSector) break;  // 880xxx 块已结束
        if (recs.size() < 1000) break;
        start += 1000;
    }
    std::lock_guard<std::mutex> lk(sectorMutex_);
    if (!out.empty()) sectorIndices_ = out;  // 空则不缓存，失败可重试
    return out;
}

std::optional<StockInfo> TdxProvider::getStockInfo(const StockCode& code) {
    const auto list = loadStockList(code.market());
    for (const auto& info : list) {
        if (info.code == code) return info;
    }
    return std::nullopt;
}

// ============================================================
// 订阅轮询
// ============================================================
void TdxProvider::subscribeQuote(const StockCode& code) {
    {
        std::lock_guard<std::mutex> lk(subMutex_);
        for (const auto& c : subscribed_) {
            if (c == code) return;
        }
        subscribed_.push_back(code);
    }
    connect();  // 确保线程已启动
}

void TdxProvider::unsubscribeQuote(const StockCode& code) {
    std::lock_guard<std::mutex> lk(subMutex_);
    subscribed_.erase(std::remove(subscribed_.begin(), subscribed_.end(), code),
                      subscribed_.end());
}

void TdxProvider::refreshQuotes() {
    if (stopThreads_.load()) return;  // 关闭中不提交任务
    ThreadPool::submitIO([this] { doPollOnce(); });
}

void TdxProvider::doPollOnce() {
    std::vector<StockCode> codes;
    {
        std::lock_guard<std::mutex> lk(subMutex_);
        codes = subscribed_;
    }
    if (codes.empty()) return;
    const auto quotes = batchQuote(codes);
    auto* bus = EventBus::instance();
    for (const auto& q : quotes) {
        QVariantMap map;
        map["code"] = QString::fromStdString(q.code.fullCode());
        map["lastPrice"] = q.lastPrice;
        map["change"] = q.change;
        map["open"] = q.open;
        map["high"] = q.high;
        map["low"] = q.low;
        map["preClose"] = q.preClose;
        map["volume"] = static_cast<qlonglong>(q.volume);
        map["amount"] = q.amount;
        bus->publish(events::QuoteReceived, map);
    }
}

void TdxProvider::pollLoop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lk(threadMutex_);
            if (stopThreads_.load()) break;
            if (threadCv_.wait_for(lk, std::chrono::milliseconds(pollIntervalMs_),
                                   [this] { return stopThreads_.load(); })) {
                break;  // 被唤醒 = 停止
            }
        }
        doPollOnce();
    }
}

} // namespace st
