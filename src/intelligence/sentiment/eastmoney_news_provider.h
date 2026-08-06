#pragma once

#include "intelligence/sentiment/sentiment_types.h"
#include <string>
#include <vector>

namespace st::sentiment {

/// 东方财富资讯源 — 按个股代码搜索东财资讯接口（akshare stock_news_em 同款）
///
/// fetchNews 可任意线程调用（IO 池）；复用 TencentProvider 的线程本地
/// QNAM + QEventLoop 同步模式，Referer 用东财域名。解析为纯静态函数可单测。
class EastMoneyNewsProvider : public ISentimentProvider {
public:
    std::vector<NewsItem> fetchNews(const StockCode& code, int limit) override;

    /// 纯静态解析（可单测，无网络）：剥离 JSONP 包装后走 nlohmann/json，
    /// 取 result.cmsArticleWebOld[] → NewsItem{title, content前200, source=mediaName, date前10位}。
    /// 任何异常/畸形返回空；结果截断到 limit 条。
    static std::vector<NewsItem> parseNews(const std::string& jsonpBody, int limit);

private:
    std::string fetch(const std::string& url, int maxRetries = 3);
};

} // namespace st::sentiment
