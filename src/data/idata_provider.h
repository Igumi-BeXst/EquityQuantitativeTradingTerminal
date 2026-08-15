#pragma once

#include "foundation/stock_code.h"
#include "foundation/stock_info.h"
#include "foundation/bar.h"
#include "foundation/tick.h"
#include "foundation/types.h"
#include "data/quote_fundamentals.h"
#include <vector>
#include <string>
#include <optional>

namespace st {

/// 数据源抽象接口 — 所有行情数据通过该接口接入
/// 实现类: TdxProvider, TencentProvider, AKShareProvider...
class IDataProvider {
public:
    virtual ~IDataProvider() = default;

    /// 数据源名称（状态栏/About 展示）
    virtual std::string providerName() const { return "unknown"; }

    /// 连接/断开数据源
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    /// 股票基本信息
    virtual std::optional<StockInfo> getStockInfo(const StockCode& code) = 0;
    virtual std::vector<StockInfo> getStockList(Market market) = 0;
    /// 使股票列表缓存失效（下次 getStockList 重新拉取；默认 no-op）
    virtual void invalidateStockListCache() {}

    /// 板块指数列表（通达信 880xxx 行业 / 885xxx 概念），默认不支持返回空
    virtual std::vector<StockInfo> getSectorIndices() { return {}; }

    /// K线数据 (返回升序 bar 序列)
    virtual std::vector<Bar> getBars(const StockCode& code,
                                     BarPeriod period,
                                     DateTime start,
                                     DateTime end) = 0;

    /// 实时行情（订阅/取消订阅）
    virtual void subscribeQuote(const StockCode& code) = 0;
    virtual void unsubscribeQuote(const StockCode& code) = 0;

    /// 批量实时报价（市场面板/模拟交易轮询用）
    virtual std::vector<Quote> batchQuote(const std::vector<StockCode>& codes) = 0;
    /// 交互优先批量报价：用户主动请求（板块面板）标记为交互，可抢占后台批量刷新。
    /// 默认等价 batchQuote；TDX 实现走交互优先级。
    virtual std::vector<Quote> batchQuoteInteractive(const std::vector<StockCode>& codes) {
        return batchQuote(codes);
    }

    /// 当日分时数据（分时图用）
    virtual std::optional<IntradayData> getIntraday(const StockCode& code) = 0;

    /// 盘口五档（仅 TDX 支持；其余数据源默认不支持）
    virtual std::optional<MarketDepth> getMarketDepth(const StockCode& code) {
        (void)code;
        return std::nullopt;
    }

    /// 最近成交明细（逐笔，最新在前），默认不支持返回空
    virtual std::vector<Tick> getTransactions(const StockCode& code, int limit = 50) {
        (void)code;
        (void)limit;
        return {};
    }

    /// 手动刷新一次实时报价（F5）
    virtual void refreshQuotes() = 0;

    /// 个股基本面快照（市值/股本/市盈/换手率），默认不支持返回 nullopt
    virtual std::optional<QuoteFundamentals> getQuoteFundamentals(const StockCode& code) {
        (void)code;
        return std::nullopt;
    }

    /// 批量基本面快照（选股估值因子用），返回与入参同序（缺失项 valid=false）
    /// 默认实现：逐个调用 getQuoteFundamentals；腾讯/AKShare 覆盖为批量 HTTP
    virtual std::vector<QuoteFundamentals> batchQuoteFundamentals(
        const std::vector<StockCode>& codes) {
        std::vector<QuoteFundamentals> out;
        out.reserve(codes.size());
        for (const auto& code : codes) {
            auto f = getQuoteFundamentals(code);
            out.push_back(f ? *f : QuoteFundamentals{code});
        }
        return out;
    }
};

} // namespace st
