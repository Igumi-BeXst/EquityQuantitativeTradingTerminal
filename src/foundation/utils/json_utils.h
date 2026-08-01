#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>

namespace st::utils {

using Json = nlohmann::json;

/// Load JSON from file
[[nodiscard]] std::optional<Json> loadJsonFile(const std::string& path);

/// Save JSON to file (pretty-printed)
bool saveJsonFile(const std::string& path, const Json& j);

/// Get nested value safely: "a.b.c"
[[nodiscard]] const Json* getNested(const Json& j, const std::string& path);

/// Set nested value, creating intermediate objects as needed
void setNested(Json& j, const std::string& path, const Json& value);

} // namespace st::utils
