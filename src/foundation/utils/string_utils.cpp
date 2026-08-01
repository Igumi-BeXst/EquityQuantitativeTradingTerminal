#include "foundation/utils/string_utils.h"
#include <sstream>

namespace st::utils {

std::vector<std::string> split(std::string_view s, char delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = s.find(delimiter);
    while (end != std::string_view::npos) {
        tokens.emplace_back(s.substr(start, end - start));
        start = end + 1;
        end = s.find(delimiter, start);
    }
    tokens.emplace_back(s.substr(start));
    return tokens;
}

std::string toPinyinInitials(std::string_view input) {
    // Simple implementation: extract first letter of each character/word
    // For ASCII characters, use the first letter directly
    // For Chinese characters, this is a placeholder — proper pinyin
    // conversion requires a lookup table (not implemented yet)
    std::ostringstream result;
    bool newWord = true;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        // ASCII letters
        if (std::isalpha(c)) {
            if (newWord) {
                result << static_cast<char>(std::toupper(c));
                newWord = false;
            }
        }
        // Chinese character (UTF-8: 3 bytes starting with 0xE4-0xE9)
        else if (c >= 0xE4 && c <= 0xE9 && i + 2 < input.size()) {
            if (newWord) {
                // Placeholder: store Unicode codepoint as identifier
                // Real pinyin requires a full mapping table
                uint32_t cp = ((c & 0x0F) << 12) |
                              ((static_cast<unsigned char>(input[i+1]) & 0x3F) << 6) |
                              (static_cast<unsigned char>(input[i+2]) & 0x3F);
                result << "U" << cp;  // Placeholder
                newWord = false;
            }
            i += 2;
        }
        // Space or separator
        else if (std::isspace(c) || c == '-' || c == '_' || c == '.') {
            newWord = true;
        }
    }
    return result.str();
}

} // namespace st::utils
