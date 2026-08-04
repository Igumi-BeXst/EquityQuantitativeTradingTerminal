#pragma once

#include "foundation/stock_code.h"
#include "foundation/stock_info.h"
#include "foundation/bar.h"
#include "foundation/tick.h"
#include "foundation/types.h"
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

    /// 当日分时数据（分时图用）
    virtual std::optional<IntradayData> getIntraday(const StockCode& code) = 0;

    /// 手动刷新一次实时报价（F5）
    virtual void refreshQuotes() = 0;
};

} // namespace st
