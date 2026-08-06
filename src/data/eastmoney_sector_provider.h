#pragma once

#include <string>
#include <vector>

namespace st {

/// 板块类型
enum class SectorType { Industry, Concept };  // 行业板块 / 概念板块

/// 板块行情快照（东财 clist/get 板块列表）
struct SectorBoard {
    std::string code;            // 板块代码 BKxxxx
    std::string name;            // 板块名称（如 银行）
    double index = 0.0;          // 板块指数（最新价）
    double changePct = 0.0;      // 涨跌幅 %
    double amount = 0.0;         // 成交额（元，treemap 面积权重）
    double turnover = 0.0;       // 换手率 %
    int upCount = 0;             // 上涨家数
    int downCount = 0;           // 下跌家数
    int flatCount = 0;           // 平盘家数
    std::string leadingStock;    // 领涨股名称
    double leadingChangePct = 0.0;  // 领涨股涨跌幅 %
};

/// 单页解析结果（含 total 用于分页）
struct SectorBoardPage {
    std::vector<SectorBoard> boards;
    int total = 0;  // 服务端总数
};

/// 东方财富板块行情源 — 行业/概念板块列表（akshare stock_board_industry_name_em 同款）
///
/// fetchBoards 可任意线程调用（IO 池）；复用 thread_local QNAM + QEventLoop 同步模式，
/// Referer 用东财域名。解析为纯静态函数可单测。
class EastMoneySectorProvider {
public:
    /// 拉取板块列表（按涨跌幅降序，自动分页直到 total；失败返回空）
    std::vector<SectorBoard> fetchBoards(SectorType type);

    /// 板块类型 → clist 的 fs 过滤串（行业 m:90+t:2 / 概念 m:90+t:3）
    static std::string fsFor(SectorType type);

    /// 纯静态解析单页（可单测，无网络）：解析 clist/get 响应 body → 板块列表 + total。
    /// 兼容 data.diff 为数组或对象两种形态；字段缺失取默认值；畸形返回空。
    static SectorBoardPage parsePage(const std::string& body);

    /// 兼容便捷接口：只取解析结果的 boards
    static std::vector<SectorBoard> parseBoards(const std::string& body);

private:
    std::string fetch(const std::string& url, int maxRetries = 3);
};

} // namespace st
