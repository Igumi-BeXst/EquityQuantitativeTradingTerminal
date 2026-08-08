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
    // v1：板块指数自身作为池（TDX 板块指数代码，如 BK0475 → StockCode("SH"+code)）
    if (code.empty()) return {};
    return { StockCode("SH" + code) };
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
