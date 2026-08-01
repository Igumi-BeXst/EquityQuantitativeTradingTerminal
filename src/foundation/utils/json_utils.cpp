#include "foundation/utils/json_utils.h"
#include "foundation/utils/string_utils.h"
#include <fstream>

namespace st::utils {

std::optional<Json> loadJsonFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    try {
        Json j;
        file >> j;
        return j;
    } catch (const Json::parse_error&) {
        return std::nullopt;
    }
}

bool saveJsonFile(const std::string& path, const Json& j) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << j.dump(4);
    return file.good();
}

const Json* getNested(const Json& j, const std::string& path) {
    auto parts = split(path, '.');
    const Json* current = &j;
    for (const auto& part : parts) {
        if (!current->is_object() || !current->contains(part)) {
            return nullptr;
        }
        current = &(*current)[part];
    }
    return current;
}

void setNested(Json& j, const std::string& path, const Json& value) {
    auto parts = split(path, '.');
    Json* current = &j;
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        if (!current->contains(parts[i]) || !(*current)[parts[i]].is_object()) {
            (*current)[parts[i]] = Json::object();
        }
        current = &(*current)[parts[i]];
    }
    (*current)[parts.back()] = value;
}

} // namespace st::utils
