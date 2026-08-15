#include "data/multi_provider.h"
#include <utility>

namespace st {

MultiProvider::MultiProvider(std::unique_ptr<IDataProvider> primary,
                             std::unique_ptr<IDataProvider> fallback)
    : primary_(std::move(primary)), fallback_(std::move(fallback)) {}

IDataProvider* MultiProvider::preferred() const {
    if (primary_ && primary_->isConnected()) return primary_.get();
    if (fallback_ && fallback_->isConnected()) return fallback_.get();
    return primary_ ? primary_.get() : fallback_.get();
}

IDataProvider* MultiProvider::other(IDataProvider* p) const {
    return (p == primary_.get()) ? fallback_.get() : primary_.get();
}

std::string MultiProvider::providerName() const {
    return "multi(" + (primary_ ? primary_->providerName() : std::string("?")) +
           "\xe2\x86\x92" +  // → (UTF-8)
           (fallback_ ? fallback_->providerName() : std::string("?")) + ")";
}

bool MultiProvider::connect() {
    bool ok = false;
    if (primary_) ok = primary_->connect();
    if (!ok && fallback_) ok = fallback_->connect();
    return ok;
}

void MultiProvider::disconnect() {
    if (primary_) primary_->disconnect();
    if (fallback_) fallback_->disconnect();
}

bool MultiProvider::isConnected() const {
    return (primary_ && primary_->isConnected()) ||
           (fallback_ && fallback_->isConnected());
}

std::optional<StockInfo> MultiProvider::getStockInfo(const StockCode& code) {
    IDataProvider* p = preferred();
    if (auto r = p->getStockInfo(code)) return r;
    if (IDataProvider* o = other(p)) return o->getStockInfo(code);
    return std::nullopt;
}

std::vector<StockInfo> MultiProvider::getStockList(Market market) {
    IDataProvider* p = preferred();
    auto r = p->getStockList(market);
    if (!r.empty()) return r;
    if (IDataProvider* o = other(p)) return o->getStockList(market);
    return {};
}

void MultiProvider::invalidateStockListCache() {
    if (IDataProvider* p = preferred()) p->invalidateStockListCache();
}

std::vector<StockInfo> MultiProvider::getSectorIndices() {
    IDataProvider* p = preferred();
    auto r = p->getSectorIndices();
    if (!r.empty()) return r;
    if (IDataProvider* o = other(p)) return o->getSectorIndices();
    return {};
}

std::vector<Bar> MultiProvider::getBars(const StockCode& code, BarPeriod period,
                                        DateTime start, DateTime end) {
    IDataProvider* p = preferred();
    auto bars = p->getBars(code, period, start, end);
    if (!bars.empty()) return bars;
    if (IDataProvider* o = other(p)) return o->getBars(code, period, start, end);
    return {};
}

void MultiProvider::subscribeQuote(const StockCode& code) {
    preferred()->subscribeQuote(code);
}

void MultiProvider::unsubscribeQuote(const StockCode& code) {
    preferred()->unsubscribeQuote(code);
}

std::vector<Quote> MultiProvider::batchQuote(const std::vector<StockCode>& codes) {
    IDataProvider* p = preferred();
    auto quotes = p->batchQuote(codes);
    if (!quotes.empty()) return quotes;
    if (IDataProvider* o = other(p)) return o->batchQuote(codes);
    return {};
}

std::vector<Quote> MultiProvider::batchQuoteInteractive(const std::vector<StockCode>& codes) {
    IDataProvider* p = preferred();
    auto quotes = p->batchQuoteInteractive(codes);
    if (!quotes.empty()) return quotes;
    if (IDataProvider* o = other(p)) return o->batchQuoteInteractive(codes);
    return {};
}

std::optional<IntradayData> MultiProvider::getIntraday(const StockCode& code) {
    IDataProvider* p = preferred();
    if (auto r = p->getIntraday(code)) return r;
    if (IDataProvider* o = other(p)) return o->getIntraday(code);
    return std::nullopt;
}

std::optional<MarketDepth> MultiProvider::getMarketDepth(const StockCode& code) {
    IDataProvider* p = preferred();
    if (auto r = p->getMarketDepth(code)) return r;
    if (IDataProvider* o = other(p)) return o->getMarketDepth(code);
    return std::nullopt;
}

std::vector<Tick> MultiProvider::getTransactions(const StockCode& code, int limit) {
    IDataProvider* p = preferred();
    auto ticks = p->getTransactions(code, limit);
    if (!ticks.empty()) return ticks;
    if (IDataProvider* o = other(p)) return o->getTransactions(code, limit);
    return {};
}

std::optional<QuoteFundamentals> MultiProvider::getQuoteFundamentals(const StockCode& code) {
    IDataProvider* p = preferred();
    if (auto r = p->getQuoteFundamentals(code)) return r;
    if (IDataProvider* o = other(p)) return o->getQuoteFundamentals(code);
    return std::nullopt;
}

std::vector<QuoteFundamentals> MultiProvider::batchQuoteFundamentals(
    const std::vector<StockCode>& codes) {
    // 主源整体批量优先；全部无效时整体回退备源（不拼接，避免口径混用）
    IDataProvider* p = preferred();
    auto out = p->batchQuoteFundamentals(codes);
    bool anyValid = false;
    for (const auto& f : out) {
        if (f.valid) { anyValid = true; break; }
    }
    if (anyValid) return out;
    if (IDataProvider* o = other(p)) return o->batchQuoteFundamentals(codes);
    return out;
}

void MultiProvider::refreshQuotes() {
    preferred()->refreshQuotes();
}

} // namespace st
