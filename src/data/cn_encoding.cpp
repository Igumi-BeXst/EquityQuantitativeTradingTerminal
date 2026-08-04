#include "data/cn_encoding.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace st {

std::string gbkToUtf8(const std::string& gbk) {
#ifdef _WIN32
    if (gbk.empty()) return {};
    int wlen = MultiByteToWideChar(936, 0, gbk.data(),
                                   static_cast<int>(gbk.size()), nullptr, 0);
    if (wlen <= 0) return gbk;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(936, 0, gbk.data(), static_cast<int>(gbk.size()),
                        &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                                   nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return gbk;
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen,
                        &utf8[0], ulen, nullptr, nullptr);
    return utf8;
#else
    return gbk;
#endif
}

std::string normalizeName(std::string utf8) {
    std::string out;
    out.reserve(utf8.size());
    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            if (c == ' ') { ++i; continue; }  // 去半角空格
            out.push_back(utf8[i]);
            ++i;
            continue;
        }
        // UTF-8 多字节解码
        uint32_t cp = 0;
        int len = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else { out.append(utf8, i, 1); ++i; continue; }
        if (i + len > n) break;
        for (int k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        if (len == 2) {
            out.append(utf8, i, static_cast<size_t>(len));
        } else {
            if (cp == 0x3000) {
                // 全角空格去除
            } else if (cp >= 0xFF21 && cp <= 0xFF3A) {
                out.push_back(static_cast<char>('A' + (cp - 0xFF21)));  // Ａ-Ｚ
            } else if (cp >= 0xFF41 && cp <= 0xFF5A) {
                out.push_back(static_cast<char>('a' + (cp - 0xFF41)));  // ａ-ｚ
            } else if (cp >= 0xFF10 && cp <= 0xFF19) {
                out.push_back(static_cast<char>('0' + (cp - 0xFF10)));  // ０-９
            } else {
                out.append(utf8, i, static_cast<size_t>(len));
            }
        }
        i += static_cast<size_t>(len);
    }
    return out;
}

} // namespace st
