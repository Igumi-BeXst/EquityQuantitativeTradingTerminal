#pragma once

#include <string>

namespace st {
namespace utils {

/// 提取中文名称的拼音首字母（小写），如 "贵州茅台" → "gzmt"。
/// ASCII 字母保留（小写），非汉字/字母字符忽略。
std::string pinyinInitials(const std::string& utf8Name);

/// 提取中文名称的全拼（小写、去声调、ü→v），如 "贵州茅台" → "guzhoumaotai"。
/// 多音字取常用读音（见 generated pinyin.cpp 数据表）。
std::string pinyinFull(const std::string& utf8Name);

}  // namespace utils
}  // namespace st
