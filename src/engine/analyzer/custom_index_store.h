#pragma once

#include "engine/analyzer/custom_index.h"
#include <string>
#include <vector>

namespace st {

/// 自定义指数持久化 — JSON 数组文件（configDir/custom_indexes.json）
///
/// 字段: id / name / baseValue / baseDate("YYYY-MM-DD"，可选) / constituents[{code, name, weight}]
class CustomIndexStore {
public:
    /// 读取全部自定义指数；文件缺失或损坏 → 空列表
    std::vector<CustomIndex> load(const std::string& path) const;

    /// 覆盖写入全部自定义指数；失败返回 false
    bool save(const std::string& path, const std::vector<CustomIndex>& indexes) const;
};

} // namespace st
