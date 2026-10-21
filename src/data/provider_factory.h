#pragma once

#include "data/idata_provider.h"
#include <memory>

namespace st {

/// 按配置 data.provider 创建数据源；每个具体来源都通过 SQLite 缓存包装后返回。
std::unique_ptr<IDataProvider> makeDataProvider();

} // namespace st
