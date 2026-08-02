#pragma once

#include "data/idata_provider.h"
#include "foundation/tick.h"
#include "foundation/types.h"
#include <memory>
#include <string>
#include <vector>

namespace st {

class QuotePoller;

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

private:
    struct ParsedQuote {
        Quote quote;
        std::string name;  // 名称（GBK→UTF-8 转码后）
    };

    /// 同步 GET（NoProxy + 重试 + 超时），任意线程可安全调用
    std::string fetch(const std::string& url, int maxRetries = 3);

    /// 拉取日K线（qfq 前复权）
    std::vector<Bar> fetchDailyBars(const StockCode& code, DateTime start, DateTime end);

    /// 解析一条记录 → Quote + 名称
    static ParsedQuote parseQuoteWithName(const std::string& record, const StockCode& code);

    /// 解析批量行情响应（多条 v_xxx="..."; 记录）
    static std::vector<ParsedQuote> parseQuoteBatch(const std::string& body);

    /// 批量拉取行情（分块 ≤50），返回 {quote, name} 列表
    std::vector<ParsedQuote> fetchBatch(const std::vector<StockCode>& codes);

    /// 解析腾讯K线 JSON: [日期, 开, 收, 高, 低, 量]
    static std::vector<Bar> parseDailyKline(const std::string& json, const StockCode& code);

    bool connected_ = false;
    std::unique_ptr<QuotePoller> poller_;  // 实时行情轮询器（主线程亲和）
};

} // namespace st
