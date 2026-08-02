#pragma once

#include "data/idata_provider.h"
#include "foundation/tick.h"
#include "foundation/types.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace st {

class QuotePoller;

/// 分时数据点（A 股日内逐分钟）
struct IntradayPoint {
    DateTime time;        // 当日 HH:MM
    Price    price  = 0.0;
    Volume   volume = 0;  // 股
    Amount   amount = 0.0;// 元
};

/// 分时数据（来自腾讯 minute/query 接口）
struct IntradayData {
    StockCode code;
    DateTime  date;          // 交易日
    Price     preClose = 0.0;// 昨收（取自响应 qt 块，缺失为 0）
    std::vector<IntradayPoint> points;

    [[nodiscard]] bool empty() const { return points.empty(); }
};

/// 腾讯行情数据源
///
/// 使用腾讯公开行情接口（web.ifzq.gtimg.cn / qt.gtimg.cn），国内直连稳定。
/// 网络层独立 NoProxy（thread_local QNetworkAccessManager），不影响全局 VPN。
///
/// 接口:
/// - 日K线(前复权): web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=sh600519,day,,,N,qfq
/// - 实时行情:     qt.gtimg.cn/q=sh600519,sz000858
/// - 股票列表:     内置精选池 + 腾讯批量行情覆盖名称
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

    /// 分时数据（同步，仅 SH/SZ A 股）
    std::optional<IntradayData> getIntraday(const StockCode& code);

    void subscribeQuote(const StockCode& code) override;
    void unsubscribeQuote(const StockCode& code) override;

    /// 批量获取实时行情（同步，测试/CLI 用）。每次请求 ≤50 码，自动分块。
    std::vector<Quote> batchQuote(const std::vector<StockCode>& codes);

    /// 手动立即刷新一次已订阅实时行情（F5）
    void refreshQuotes();

    /// 解析单条行情记录 v_sh600519="1~名称~代码~现价~昨收~今开~...";
    /// 实测绘定的字段索引: [3]现价 [4]昨收 [5]今开 [6]量(手) [9]买一价 [10]买一量
    /// [19]卖一价 [20]卖一量 [31]涨跌额 [32]涨跌幅% [33]最高 [34]最低 [37]成交额(万)
    static Quote parseQuoteRecord(const std::string& record, const StockCode& code);

    /// 解析批量行情响应（多条 v_xxx="..."; 记录）→ Quote 列表（不含名称）
    static std::vector<Quote> parseQuotes(const std::string& body);

    /// 解析单条记录的名称字段（GBK→UTF-8 转码）
    static std::string parseQuoteName(const std::string& record);

    /// 腾讯代码格式: SH600519 → sh600519
    static std::string toTencentCode(const StockCode& code);

    /// 解析日/周/月 K线 JSON: [日期, 开, 收, 高, 低, 量]（键 qfq{day|week|month}）
    static std::vector<Bar> parseFqKline(const std::string& json, const StockCode& code,
                                         BarPeriod period);

    /// 解析分钟 K线 JSON: ["yyyyMMddHHmm", 开, 收, 高, 低, 量, {}, 额]
    static std::vector<Bar> parseMinuteKline(const std::string& json, const StockCode& code,
                                             BarPeriod period);

    /// 解析分时 JSON（minute/query 接口）
    static std::optional<IntradayData> parseIntraday(const std::string& json,
                                                     const StockCode& code);

private:
    struct ParsedQuote {
        Quote quote;
        std::string name;  // 名称（GBK→UTF-8 转码后）
    };

    /// 同步 GET（NoProxy + 重试 + 超时），任意线程可安全调用
    std::string fetch(const std::string& url, int maxRetries = 3);

    /// 拉取日/周/月K线（qfq 前复权）。start==epoch 表示拉最近 640 根。
    std::vector<Bar> fetchKlineBars(const StockCode& code, BarPeriod period,
                                    DateTime start, DateTime end);

    /// 拉取分钟K线（m5/m15/m30/m60，320 根）
    std::vector<Bar> fetchMinuteBars(const StockCode& code, BarPeriod period);

    /// 拉取分时数据（A 股日内逐分钟）
    std::optional<IntradayData> fetchIntraday(const StockCode& code);

    /// 解析一条记录 → Quote + 名称
    static ParsedQuote parseQuoteWithName(const std::string& record, const StockCode& code);

    /// 解析批量行情响应（多条 v_xxx="..."; 记录）
    static std::vector<ParsedQuote> parseQuoteBatch(const std::string& body);

    /// 批量拉取行情（分块 ≤50），返回 {quote, name} 列表
    std::vector<ParsedQuote> fetchBatch(const std::vector<StockCode>& codes);

    /// 周期 → 腾讯 fqkline 关键字 (day/week/month)
    static const char* periodToFqKeyword(BarPeriod period);
    /// 周期 → 腾讯分钟关键字 (m5/m15/m30/m60)
    static const char* periodToMinuteKeyword(BarPeriod period);

    bool connected_ = false;
    std::unique_ptr<QuotePoller> poller_;  // 实时行情轮询器（主线程亲和）
};

} // namespace st
