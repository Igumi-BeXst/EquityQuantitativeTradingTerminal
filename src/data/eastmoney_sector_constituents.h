#pragma once

#include "foundation/stock_code.h"
#include <string>
#include <vector>

namespace st {

/// 东财板块成分数据源 — datacenter-web（未被 IP 封锁的主机）
/// 接口：RPT_F10_CORETHEME_BOARDTYPE，按板块名称查成分股
/// 实测：datacenter-web.eastmoney.com 可用，按 BOARD_NAME="煤炭" 返回 BK0437 煤炭板块成分
class EastMoneySectorConstituents {
public:
    /// 按板块名称拉取成分股（如 "煤炭" → 该板块全部股票）
    /// 多页拉全（pageSize=100 逐页）；失败/无结果返回空
    std::vector<StockCode> fetchConstituents(const std::string& boardName) const;

    /// URL 构建（可单测）
    static std::string constituentsUrl(const std::string& boardName, int page, int pageSize);

    /// 纯静态解析响应体 → 成分股（可单测，无网络）
    /// 返回 result.data[].SECURITY_CODE 列表
    static std::vector<StockCode> parseConstituents(const std::string& body);

private:
    /// 同步 fetch（thread_local QNAM + 超时 + 重试），仿 EastMoneyFundsProvider
    std::string fetch(const std::string& url, int maxRetries = 3) const;
};

} // namespace st
