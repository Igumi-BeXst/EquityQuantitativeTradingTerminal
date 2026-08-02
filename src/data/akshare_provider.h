#pragma once

#include "data/idata_provider.h"
#include "foundation/types.h"
#include <httplib.h>
#include <string>
#include <memory>

namespace st {

/// AKShare 免费数据源
/// 通过 HTTP 调用 AKShare 的 REST API (akshare.gtimg.cn / 东财等)
/// 参考: https://github.com/akfamily/akshare
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

private:
    /// 东财日线行情接口 (JSON)
    /// https://push2his.eastmoney.com/api/qt/stock/kline/get
    std::vector<Bar> fetchDailyBars(const StockCode& code, DateTime start, DateTime end);

    /// 分时/分钟线接口
    std::vector<Bar> fetchMinuteBars(const StockCode& code, BarPeriod period,
                                     DateTime start, DateTime end);

    /// 东财股票列表接口
    std::vector<StockInfo> fetchStockList(Market market);

    std::string fetch(const std::string& url);

    std::shared_ptr<httplib::Client> http_;
    bool connected_ = false;
    std::string host_ = "push2his.eastmoney.com";
};

} // namespace st
