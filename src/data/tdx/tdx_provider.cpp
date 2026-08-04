#include "data/tdx/tdx_provider.h"
#include "data/tdx/tdx_protocol.h"
#include "data/cn_encoding.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "core/event_bus.h"
#include "foundation/utils/datetime.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>

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

bool TdxProvider::connect() {
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
    stopThreads_ = true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (transport_) transport_->close();
        state_ = State::Disconnected;
        cv_.notify_all();
    }
    if (heartbeatThread_.joinable()) heartbeatThread_.join();
    if (pollThread_.joinable()) pollThread_.join();
    stopThreads_ = false;
}

bool TdxProvider::isConnected() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_ == State::Connected;
}

// ============================================================
// 连接状态机
// ============================================================
bool TdxProvider::ensureConnected(std::unique_lock<std::mutex>& lk, int timeoutMs) {
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

        auto transport = std::make_unique<tdx::TdxSocket>();
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
    while (!stopThreads_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (stopThreads_.load()) break;
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

    const auto recs = tdx::decodeKline(resp.payload, static_cast<uint8_t>(cat));
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
    // TODO: TDX 当日分时(0x051D)响应字节格式与 injoyai/pytdx 参考实现有出入
    //（数据起点/字段边界待校准），暂返回空避免错误数据。用 0x0FC5 分时成交
    // 明细聚合或抓包 fixture 校准后可启用。
    (void)code;
    return std::nullopt;
#if 0
    const int mkt = tdx::tdxMarket(code.market());
    if (mkt < 0) return std::nullopt;
    const auto req = tdx::buildMinuteReq(static_cast<uint8_t>(mkt), code.code());
    const auto resp = executeCommand(tdx::Cmd::Minute, req, requestTimeoutMs_);
    if (!resp.ok) return std::nullopt;

    const auto recs = tdx::decodeMinute(resp.payload);
    if (recs.empty()) return std::nullopt;

    // 昨收（发一次报价）
    double preClose = 0.0;
    const auto quotes = batchQuote({code});
    if (!quotes.empty()) preClose = quotes.front().preClose;

    IntradayData data;
    data.code = code;
    data.date = utils::now();
    data.preClose = preClose;
    data.points.reserve(recs.size());
    for (const auto& r : recs) {
        IntradayPoint pt;
        // 标准分钟：09:30=0..11:30=120, 13:00=120..15:00=240（TDX 从 09:00 起算）
        int stdMin = r.minute;
        if (r.minute >= 30 && r.minute < 150) {
            stdMin = r.minute - 30;            // 上午 09:30 起
        } else if (r.minute >= 150) {
            stdMin = 120 + (r.minute - 150);   // 下午 13:00 起
        } else {
            continue;  // 集合竞价段丢弃
        }
        if (stdMin < 0 || stdMin > 240) continue;
        // 构建当日 09:30 + stdMin 分钟（std::tm 自动进位规范化）
        const std::time_t nowTt = std::chrono::system_clock::to_time_t(data.date);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &nowTt);
#else
        localtime_r(&nowTt, &tmv);
#endif
        tmv.tm_hour = 9;
        tmv.tm_min = 30 + stdMin;
        tmv.tm_sec = 0;
        const std::time_t ptTt = std::mktime(&tmv);
        pt.time = std::chrono::system_clock::from_time_t(ptTt);
        pt.price = r.price;
        pt.volume = static_cast<Volume>(r.volume);
        pt.amount = r.price * r.volume;  // 估算累计额（TDX 分时不提供额）
        data.points.push_back(pt);
    }
    return data;
#endif
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
                out.push_back(q);
            }
        }
        chunk.clear();
    };
    for (const auto& code : codes) {
        const int mkt = tdx::tdxMarket(code.market());
        if (mkt < 0) continue;
        chunk.emplace_back(static_cast<uint8_t>(mkt), code.code());
        if (chunk.size() >= quoteChunk_) flush();
    }
    flush();
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

    uint16_t start = 0;
    const uint32_t maxStocks = (total > 0 && total < 20000) ? total : 20000;
    while (start < maxStocks) {
        const auto req = tdx::buildCodeReq(static_cast<uint8_t>(mkt), start);
        const auto resp = executeCommand(tdx::Cmd::Code, req, requestTimeoutMs_);
        if (!resp.ok) break;
        const auto recs = tdx::decodeCodeList(resp.payload);
        if (recs.empty()) break;
        for (const auto& r : recs) {
            if (!r.code.isValid()) continue;
            StockInfo info;
            info.code = r.code;
            info.name = r.name;
            info.pinyinInitials = r.name.empty() ? "" : "";
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
    while (!stopThreads_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs_));
        if (stopThreads_.load()) break;
        doPollOnce();
    }
}

} // namespace st
