#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace st::utils {

/// Trim whitespace from both ends
[[nodiscard]] inline std::string trim(std::string_view s) {
    auto start = std::find_if_not(s.begin(), s.end(),
        [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(s.rbegin(), s.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    return (start < end) ? std::string(start, end) : std::string();
}

/// Split string by delimiter
[[nodiscard]] std::vector<std::string> split(std::string_view s, char delimiter);

/// Convert string to lowercase
[[nodiscard]] inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

/// Convert string to uppercase
[[nodiscard]] inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return s;
}

/// Check if haystack starts with needle
[[nodiscard]] inline bool startsWith(std::string_view haystack, std::string_view needle) {
    return haystack.size() >= needle.size() &&
           haystack.substr(0, needle.size()) == needle;
}

/// Check if haystack ends with needle
[[nodiscard]] inline bool endsWith(std::string_view haystack, std::string_view needle) {
    return haystack.size() >= needle.size() &&
           haystack.substr(haystack.size() - needle.size()) == needle;
}

/// Simple pinyin initials extraction for Chinese characters (basic)
[[nodiscard]] std::string toPinyinInitials(std::string_view input);

} // namespace st::utils
