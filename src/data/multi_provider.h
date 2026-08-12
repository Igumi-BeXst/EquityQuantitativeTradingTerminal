#pragma once

#include "data/idata_provider.h"
#include <memory>

namespace st {

/// 多数据源 — 主源优先，空/失败整体回退备源
///
/// 每类数据先取当前可用源（preferred：主源已连接则主源，否则备源），
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
    void subscribeQuote(const StockCode& code) override;
    void unsubscribeQuote(const StockCode& code) override;
    std::vector<Quote> batchQuote(const std::vector<StockCode>& codes) override;
    std::vector<Quote> batchQuoteInteractive(const std::vector<StockCode>& codes) override;
    std::optional<IntradayData> getIntraday(const StockCode& code) override;
    std::optional<MarketDepth> getMarketDepth(const StockCode& code) override;
    std::vector<Tick> getTransactions(const StockCode& code, int limit = 50) override;
    void refreshQuotes() override;

private:
    /// 当前首选源：主源已连接则主源，否则备源
    IDataProvider* preferred() const;
    IDataProvider* other(IDataProvider* p) const;

    std::unique_ptr<IDataProvider> primary_;
    std::unique_ptr<IDataProvider> fallback_;
};

} // namespace st
