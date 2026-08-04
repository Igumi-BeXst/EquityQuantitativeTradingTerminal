#pragma once

#include "data/idata_provider.h"
#include <memory>

namespace st {

/// 按配置 data.provider 创建数据源（默认 "tdx"）
std::unique_ptr<IDataProvider> makeDataProvider();

} // namespace st
