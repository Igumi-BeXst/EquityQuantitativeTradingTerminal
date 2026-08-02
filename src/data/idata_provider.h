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
/// 实现类: AKShareProvider, TushareProvider, WindProvider...
class IDataProvider {
public:
    virtual ~IDataProvider() = default;

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
};

} // namespace st
