#pragma once

#include "foundation/bar.h"
#include <string>
#include <vector>

namespace st::csv {

/// CSV 字段转义：逗号/双引号/换行 → 包双引号并转义内部引号
std::string escape(const std::string& s);

/// 组装一行 CSV（逗号分隔 + 逐字段转义，UTF-8）
std::string joinRow(const std::vector<std::string>& cols);

/// K线数据 → CSV 文本（表头 + 每根一行）
/// 列：日期,开盘,最高,最低,收盘,成交量(股),成交额(元),换手率
std::string klineToCsv(const std::vector<Bar>& bars);

} // namespace st::csv
