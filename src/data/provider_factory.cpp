#include "data/provider_factory.h"
#include "data/tdx/tdx_provider.h"
#include "data/tencent_provider.h"
#include "data/akshare_provider.h"
#include "data/multi_provider.h"
#include "data/cached_data_provider.h"
#include "core/app_paths.h"
#include "core/config_manager.h"

namespace st {

namespace {

/// 按名称创建单数据源（默认 tdx）
std::unique_ptr<IDataProvider> makeDataProviderByName(const std::string& name) {
    if (name == "tencent") return std::make_unique<TencentProvider>();
    if (name == "akshare") return std::make_unique<AKShareProvider>();
    return std::make_unique<TdxProvider>();
}

std::unique_ptr<IDataProvider> makeCachedProvider(const std::string& name) {
    // 数据目录不可用时传空路径，让缓存层自行降级（不写盘但仍可拉数据）
    const std::string dataDir = AppPaths::dataDir();
    const std::string dbPath = dataDir.empty() ? std::string() : dataDir + "/market_data.sqlite";
    return std::make_unique<CachedDataProvider>(makeDataProviderByName(name), name, dbPath);
}

}  // namespace

std::unique_ptr<IDataProvider> makeDataProvider() {
    std::string name = "multi";
    if (auto* cfg = ConfigManager::instance()) {
        name = cfg->get<std::string>("data.provider", "multi");
    }
    if (name == "multi") {
        // TDX 主源 + AKShare 容灾备源
        std::string primary = "tdx", fallback = "akshare";
        if (auto* cfg = ConfigManager::instance()) {
            primary = cfg->get<std::string>("data.multi.primary", "tdx");
            fallback = cfg->get<std::string>("data.multi.fallback", "akshare");
        }
        return std::make_unique<MultiProvider>(makeCachedProvider(primary),
                                               makeCachedProvider(fallback));
    }
    return makeCachedProvider(name);
}

} // namespace st
