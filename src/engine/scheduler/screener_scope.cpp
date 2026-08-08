#include "engine/scheduler/screener_scope.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include <nlohmann/json.hpp>

namespace st {

std::vector<StockCode> ScopeResolver::allAShares(IDataProvider* provider) {
    std::vector<StockCode> out;
    if (!provider) return out;
    auto sh = provider->getStockList(Market::SH);
    auto sz = provider->getStockList(Market::SZ);
    for (auto& s : sh) {
        if (tdx::isTradableAShare(s.code) || tdx::isIndexCode(s.code))
            out.push_back(s.code);
    }
    for (auto& s : sz) {
        if (tdx::isTradableAShare(s.code) || tdx::isIndexCode(s.code))
            out.push_back(s.code);
    }
    return out;
}

std::vector<StockCode> ScopeResolver::sectorStocks(IDataProvider*, const std::string& code) {
    // 板块成分股解析：v1 无数据源接口（IDataProvider 仅提供板块指数列表，无成分股接口），
    // 无法解析成分 → 返回空池。调用方（runScheduledTask）应检测空池并提示"暂不支持"，
    // 而不是返回非法代码（如 SHBK0475）导致选股/抓数据静默无效。
    (void)code;
    return {};
}

std::vector<StockCode> ScopeResolver::resolve(const std::string& targetJson,
                                              IDataProvider* provider,
                                              const std::vector<StockCode>& lastConfig) {
    try {
        auto j = nlohmann::json::parse(targetJson);
        const std::string scope = j.value("scope", "all");
        if (scope == "sector") {
            return sectorStocks(provider, j.value("sector", ""));
        }
        if (scope == "last") {
            return lastConfig.empty() ? allAShares(provider) : lastConfig;
        }
        return allAShares(provider);
    } catch (const std::exception&) {
        return allAShares(provider);
    }
}

} // namespace st
