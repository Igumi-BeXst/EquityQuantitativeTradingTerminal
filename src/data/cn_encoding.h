#pragma once

#include <string>

namespace st {

/// GBK → UTF-8 转码（腾讯/TDX 行情接口均返回 GBK 名称，Qt6 core 无 QTextCodec）
std::string gbkToUtf8(const std::string& gbk);

/// 规范化股票名称: 去除空格（半角/全角）、全角字母数字转半角。
/// 腾讯返回 "五 粮 液" / "京东方Ａ" 等，影响按名称搜索与展示。
std::string normalizeName(std::string utf8);

} // namespace st
