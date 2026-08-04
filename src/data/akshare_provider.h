#pragma once

#include "data/idata_provider.h"
#include "foundation/types.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include <string>

namespace st {

/// AKShare 免费数据源
/// 通过 HTTP 调用 AKShare 的 REST API (东方财富等)
/// 使用 Qt Network 栈（自动处理 IPv4/IPv6 回退）
class AKShareProvider : public IDataProvider {
public:
    AKShareProvider();
    ~AKShareProvider() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    std::optional<StockInfo> getStockInfo(const StockCode& code) override;
    std::vector<StockInfo> getStockList(Market market) override;

    std::vector<Bar> getBars(const StockCode& code, BarPeriod period,
                             DateTime start, DateTime end) override;

    void subscribeQuote(const StockCode& code) override;
    void unsubscribeQuote(const StockCode& code) override;

    std::vector<Quote> batchQuote(const std::vector<StockCode>& codes) override;
    std::optional<IntradayData> getIntraday(const StockCode& code) override;
    void refreshQuotes() override;

private:
    /// 东财日线行情接口 (JSON)
    std::vector<Bar> fetchDailyBars(const StockCode& code, DateTime start, DateTime end);

    /// 分时/分钟线接口
    std::vector<Bar> fetchMinuteBars(const StockCode& code, BarPeriod period,
                                     DateTime start, DateTime end);

    /// 东财股票列表接口
    std::vector<StockInfo> fetchStockList(Market market);

    /// 同步 GET 请求（阻塞，内部用事件循环等待）
    std::string fetch(const std::string& url);

    std::unique_ptr<QNetworkAccessManager> http_;
    bool connected_ = false;
};

} // namespace st
