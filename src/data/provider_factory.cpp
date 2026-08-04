#include "data/provider_factory.h"
#include "data/tdx/tdx_provider.h"
#include "data/tencent_provider.h"
#include "core/config_manager.h"

namespace st {

std::unique_ptr<IDataProvider> makeDataProvider() {
    std::string name = "tdx";
    if (auto* cfg = ConfigManager::instance()) {
        name = cfg->get<std::string>("data.provider", "tdx");
    }
    if (name == "tencent") {
        return std::make_unique<TencentProvider>();
    }
    return std::make_unique<TdxProvider>();
}

} // namespace st
