#pragma once

#include "data/idata_provider.h"
#include "foundation/types.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <memory>
#include <string>

namespace st {

/// 腾讯行情数据源
///
/// 使用腾讯公开行情接口（web.ifzq.gtimg.cn / qt.gtimg.cn），国内直连稳定。
/// 独立 QNetworkAccessManager + NoProxy，不影响全局 VPN 配置。
///
/// 接口:
/// - 日K线(前复权): web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=sh600519,day,,,N,qfq
/// - 实时行情:     qt.gtimg.cn/q=sh600519,sz000858
/// - 股票列表:     由实时行情批量 + 指数推导
class TencentProvider : public IDataProvider {
public:
    TencentProvider();
    ~TencentProvider() override;

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
    /// 同步 GET（NoProxy + 重试 + 超时）
    std::string fetch(const std::string& url, int maxRetries = 3);

    /// 拉取日K线（qfq 前复权）
    std::vector<Bar> fetchDailyBars(const StockCode& code, DateTime start, DateTime end);

    /// 腾讯代码格式: SH600519 → sh600519, SZ000001 → sz000001
    static std::string toTencentCode(const StockCode& code);

    /// 解析腾讯K线 JSON
    /// 格式: ["2024-05-31","1516.161","1514.111","1529.161","1514.111","21857",...]
    ///       [日期, 开, 收, 高, 低, 成交量]
    static std::vector<Bar> parseDailyKline(const std::string& json, const StockCode& code);

    /// 解析腾讯实时行情: v_sh600519="1~贵州茅台~600519~现价~昨收~今开~..."
    static StockInfo parseQuote(const std::string& body, const StockCode& code);

    std::unique_ptr<QNetworkAccessManager> http_;
    bool connected_ = false;
};

} // namespace st
