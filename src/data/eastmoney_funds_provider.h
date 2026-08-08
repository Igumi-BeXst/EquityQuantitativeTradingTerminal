#pragma once

#include "foundation/types.h"
#include <string>
#include <vector>

namespace st {

/// 龙虎榜记录（当日上榜个股）
struct DragonTigerRecord {
    std::string code;
    std::string name;
    DateTime date;
    double closePrice = 0.0;      // 收盘价
    double changeRate = 0.0;      // 涨跌幅 %
    double netAmt = 0.0;          // 净买额（元，负=净卖）
    double buyAmt = 0.0;          // 买入额
    double sellAmt = 0.0;         // 卖出额
    double turnoverRate = 0.0;    // 换手率 %
    std::string reason;           // 上榜原因
};

/// 融资融券个股明细
struct MarginRecord {
    DateTime date;
    std::string market;            // 融资融券_沪证 / _深证
    std::string code;
    std::string name;
    double financeBalance = 0.0;   // 融资余额（元）
    double shortBalance = 0.0;     // 融券余额（元）
    double marginBalance = 0.0;    // 两融余额（元）
    double financeBuy = 0.0;       // 融资买入额（元）
};

/// 沪深两市融资融券余额（每日）
struct MarginMarketRecord {
    DateTime date;
    double financeBalance = 0.0;   // 融资余额（元）
    double marginBalance = 0.0;    // 两融余额（元）
};

/// 资金数据源 — 东财数据中心（datacenter-web）接口
///
/// 与板块 clist/K线 接口不同，datacenter-web 对本机未被封锁（实测可用）。
/// 全部为可任意线程调用的同步方法（thread_local QNAM + QEventLoop）；解析纯静态可单测。
/// 注: 北向资金自 2024-05-13 起交易所停止盘中/盘后实时披露（仅保留每日成交总额与
/// 十大成交活跃股，且数据中心接口不可用），故本提供器不含北向资金。
class EastMoneyFundsProvider {
public:
    /// 龙虎榜：指定日期（YYYY-MM-DD）榜单，按净买额降序
    std::vector<DragonTigerRecord> fetchDragonTiger(const std::string& date);
    /// 融资融券个股明细（最新在前，最多 ~120 条）
    std::vector<MarginRecord> fetchMargin(const std::string& code);
    /// 沪深两市融资融券余额（最新在前，最多 ~120 条）
    std::vector<MarginMarketRecord> fetchMarginMarket();

    // --- 纯静态 URL 构建 + 解析（可单测，无网络） ---
    static std::string dragonTigerUrl(const std::string& date);
    static std::vector<DragonTigerRecord> parseDragonTiger(const std::string& body);

    static std::string marginUrl(const std::string& code);
    static std::vector<MarginRecord> parseMargin(const std::string& body);

    static std::string marginMarketUrl();
    static std::vector<MarginMarketRecord> parseMarginMarket(const std::string& body);

private:
    std::string fetch(const std::string& url, int maxRetries = 3);
};

} // namespace st
