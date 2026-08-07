#pragma once

#include "foundation/bar.h"
#include "foundation/tick.h"
#include <optional>
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

/// 板块行情源 — 东财 clist 优先，封锁/限流时自动降级新浪板块行情
///
/// 东财 clist/get 对本机偶发 IP 级封锁（HTTP 000 空回复，curl 可复现，非代码问题）。
/// fetchBoards 先试东财 clist（akshare stock_board_industry_name_em 同款）；连续全败
/// 达阈值后暂时跳过东财（避免每次刷新等 4 主机×3 重试超时），直接走新浪板块行情
/// （akshare stock_sector_spot 同款）。进程重启自动复测东财，封锁解除即恢复富数据
/// （新浪不提供换手率/涨跌平家数 → 对应字段为 0）。
///
/// fetchBoards 可任意线程调用（IO 池）；复用 thread_local QNAM + QEventLoop 同步模式。
/// 两类解析均为纯静态函数可单测。
class EastMoneySectorProvider {
public:
    /// 拉取板块列表（按涨跌幅降序；东财分页，新浪单请求）。全失败返回空。
    std::vector<SectorBoard> fetchBoards(SectorType type);

    /// 板块类型 → clist 的 fs 过滤串（行业 m:90+t:2 / 概念 m:90+t:3）
    static std::string fsFor(SectorType type);

    /// 板块类型 → 新浪板块行情 URL（行业 newSinaHy.php / 概念 newFLJK.php?param=class）
    static std::string sinaUrlFor(SectorType type);

    /// 纯静态解析单页（可单测，无网络）：解析 clist/get 响应 body → 板块列表 + total。
    /// 兼容 data.diff 为数组或对象两种形态；字段缺失取默认值；畸形返回空。
    static SectorBoardPage parsePage(const std::string& body);

    /// 纯静态解析新浪板块行情（可单测，无网络）：入参为原始 GBK 响应体，
    /// 内部先整体 GBK→UTF-8 再解析。每条逗号分隔：
    ///   代码,名称,公司家数,平均价,涨跌额,涨跌幅,总成交量,总成交额,
    ///   领涨股代码,?,?,领涨股涨跌幅,领涨股名称
    /// 新浪无换手率/涨跌平家数 → turnover/up/down/flat 均为 0。
    static SectorBoardPage parseSinaPage(const std::string& gbkBody);

    /// 兼容便捷接口：只取解析结果的 boards
    static std::vector<SectorBoard> parseBoards(const std::string& body);

    // --- 板块历史 K 线 / 分时（叠加对比用，东财 push2his 接口） ---

    /// 板块日/周/月 K 线 URL（secid=90.BKxxxx；host 可换做多主机回退）
    static std::string klineUrlFor(SectorType type, const std::string& code,
                                   BarPeriod period,
                                   const std::string& host = "push2his.eastmoney.com");

    /// 纯静态解析板块 K 线（可单测，无网络）：data.klines 数组 → Bar 升序
    static std::vector<Bar> parseSectorKline(const std::string& body, const std::string& code);

    /// 拉取板块日/周/月 K 线（多主机回退；全失败返回空）。
    /// code 为新浪降级列表的 new_xxx 时自动经 resolveSectorCode 转成东财 BKxxxx。
    std::vector<Bar> fetchSectorKline(SectorType type, const std::string& code,
                                      const std::string& name, BarPeriod period);

    /// 板块当日分时 URL（trends2 接口）
    static std::string trendsUrlFor(SectorType type, const std::string& code,
                                    const std::string& host = "push2his.eastmoney.com");

    /// 纯静态解析板块分时（可单测，无网络）：data.trends 数组 → IntradayData
    static std::optional<IntradayData> parseSectorTrends(const std::string& body,
                                                         const std::string& code);

    /// 拉取板块当日分时（多主机回退；全失败返回 nullopt）。
    /// code 为新浪降级列表的 new_xxx 时自动经 resolveSectorCode 转成东财 BKxxxx。
    std::optional<IntradayData> fetchSectorTrends(SectorType type, const std::string& code,
                                                  const std::string& name);

    // --- 板块名称 → 东财 BK 代码解析（新浪降级列表的 new_xxx 代码需转 BKxxxx 才能用东财 K线/分时） ---

    /// 东财 suggest 搜索 URL（type=14 返回个股+板块，按名称搜板块代码用）
    static std::string suggestUrlFor(const std::string& name);

    /// 纯静态解析 suggest 响应：取第一个 Classify=="BK" 的代码（可单测，无网络）
    static std::string parseSuggestBkCode(const std::string& body);

    /// 板块名称 → 东财 BK 代码（suggest API；失败再尝试去掉"行业/板块"后缀；全失败返回空）
    std::string resolveSectorCode(const std::string& name);

private:
    std::vector<SectorBoard> fetchEastMoneyBoards(SectorType type);
    std::vector<SectorBoard> fetchSinaBoards(SectorType type);
    std::string fetch(const std::string& url, int maxRetries = 3);
};

} // namespace st
