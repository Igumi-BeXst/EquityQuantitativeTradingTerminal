#pragma once

#include "data/idata_provider.h"
#include <atomic>
#include <memory>

namespace st {

/// 多数据源 — 主源优先，空/失败整体回退备源
///
/// 每类数据先取当前可用源（已连上的主源优先；异步首次连接期间先探测主源，
/// 确认不可用后转备源），
/// 结果为空/无值时再尝试另一个源。整体替换、不拼接（避免价量口径错配）。
class MultiProvider : public IDataProvider {
public:
    MultiProvider(std::unique_ptr<IDataProvider> primary,
                  std::unique_ptr<IDataProvider> fallback);

    std::string providerName() const override;
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    std::optional<StockInfo> getStockInfo(const StockCode& code) override;
    std::vector<StockInfo> getStockList(Market market) override;
    void invalidateStockListCache() override;
    std::vector<StockInfo> getSectorIndices() override;
    std::vector<Bar> getBars(const StockCode& code, BarPeriod period,
                             DateTime start, DateTime end) override;
    std::vector<Bar> getRawBars(const StockCode& code, BarPeriod period,
                                DateTime start, DateTime end) override;
    std::vector<Bar> getHfqBars(const StockCode& code, BarPeriod period,
                                DateTime start, DateTime end) override;
    void subscribeQuote(const StockCode& code) override;
    void unsubscribeQuote(const StockCode& code) override;
    std::vector<Quote> batchQuote(const std::vector<StockCode>& codes) override;
    std::vector<Quote> batchQuoteInteractive(const std::vector<StockCode>& codes) override;
    std::optional<IntradayData> getIntraday(const StockCode& code) override;
    std::optional<MarketDepth> getMarketDepth(const StockCode& code) override;
    std::vector<Tick> getTransactions(const StockCode& code, int limit = 50) override;
    std::optional<QuoteFundamentals> getQuoteFundamentals(const StockCode& code) override;
    std::vector<QuoteFundamentals> batchQuoteFundamentals(
        const std::vector<StockCode>& codes) override;
    void refreshQuotes() override;

private:
    /// 当前首选源：已连接的主源优先；首次异步连接尚未确认失败时也先尝试主源。
    IDataProvider* preferred() const;
    IDataProvider* other(IDataProvider* p) const;
    /// 主源请求失败且此时未连接时，后续请求应直接走备源。
    void notePrimaryUnavailable(IDataProvider* attempted) const;

    std::unique_ptr<IDataProvider> primary_;
    std::unique_ptr<IDataProvider> fallback_;
    mutable std::atomic<bool> primaryConnectAccepted_{false};
    mutable std::atomic<bool> primaryEverConnected_{false};
    mutable std::atomic<bool> primaryUnavailable_{false};
};

} // namespace st
