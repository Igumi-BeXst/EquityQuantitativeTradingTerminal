#include "data/provider_factory.h"
#include "data/tdx/tdx_provider.h"
#include "data/tencent_provider.h"
#include "data/akshare_provider.h"
#include "data/multi_provider.h"
#include "core/config_manager.h"

namespace st {

namespace {

/// 按名称创建单数据源（默认 tdx）
std::unique_ptr<IDataProvider> makeDataProviderByName(const std::string& name) {
    if (name == "tencent") return std::make_unique<TencentProvider>();
    if (name == "akshare") return std::make_unique<AKShareProvider>();
    return std::make_unique<TdxProvider>();
}

}  // namespace

std::unique_ptr<IDataProvider> makeDataProvider() {
    std::string name = "tdx";
    if (auto* cfg = ConfigManager::instance()) {
        name = cfg->get<std::string>("data.provider", "tdx");
    }
    if (name == "multi") {
        // 主源 + 备源 fallback；默认 tdx → tencent
        std::string primary = "tdx", fallback = "tencent";
        if (auto* cfg = ConfigManager::instance()) {
            primary = cfg->get<std::string>("data.multi.primary", "tdx");
            fallback = cfg->get<std::string>("data.multi.fallback", "tencent");
        }
        return std::make_unique<MultiProvider>(makeDataProviderByName(primary),
                                               makeDataProviderByName(fallback));
    }
    return makeDataProviderByName(name);
}

} // namespace st
