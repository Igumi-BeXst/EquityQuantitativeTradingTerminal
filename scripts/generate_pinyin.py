# -*- coding: utf-8 -*-
"""Generate src/foundation/utils/pinyin.cpp from mozillazg/pinyin-data pinyin.txt.

Usage: python scripts/generate_pinyin.py   (reads scripts/pinyin_src.txt, writes src/foundation/utils/pinyin.cpp)
"""
import re

SRC = r'd:/StockTerminal/scripts/pinyin_src.txt'
OUT = r'd:/StockTerminal/src/foundation/utils/pinyin.cpp'

TONE_MAP = {
    'ā': 'a', 'á': 'a', 'ǎ': 'a', 'à': 'a',
    'ē': 'e', 'é': 'e', 'ě': 'e', 'è': 'e',
    'ī': 'i', 'í': 'i', 'ǐ': 'i', 'ì': 'i',
    'ō': 'o', 'ó': 'o', 'ǒ': 'o', 'ò': 'o',
    'ū': 'u', 'ú': 'u', 'ǔ': 'u', 'ù': 'u',
    'ǖ': 'v', 'ǘ': 'v', 'ǚ': 'v', 'ǜ': 'v', 'ü': 'v',
    'ń': 'n', 'ň': 'n', 'ǹ': 'n', 'ḿ': 'm', 'm̀': 'm',
}


def strip_tone(syl):
    out = []
    for ch in syl.lower():
        if ch in TONE_MAP:
            out.append(TONE_MAP[ch])
        elif ch.isalpha():
            out.append(ch)
    return ''.join(out)


entries = []
pat = re.compile(r'^U\+([0-9A-Fa-f]{4,5}):\s*([^#]+)')
with open(SRC, encoding='utf-8') as f:
    for line in f:
        m = pat.match(line.strip())
        if not m:
            continue
        cp = int(m.group(1), 16)
        primary = m.group(2).split(',')[0].strip()
        py = strip_tone(primary)
        if py:
            entries.append((cp, py))

entries.sort(key=lambda e: e[0])
uniq = []
seen = set()
for cp, py in entries:
    if cp in seen:
        continue
    seen.add(cp)
    uniq.append((cp, py))

lines = []
lines.append('// 本文件由 scripts/generate_pinyin.py 自动生成 — 请勿手改')
lines.append('// 数据源: mozillazg/pinyin-data pinyin.txt (MIT License)')
lines.append('// 每项: Unicode 码点 → 拼音全拼（小写、去声调、ü→v，取首读音）')
lines.append('#include "foundation/utils/pinyin.h"')
lines.append('#include <algorithm>')
lines.append('#include <cstdint>')
lines.append('#include <cstring>')
lines.append('')
lines.append('namespace st {')
lines.append('namespace utils {')
lines.append('namespace {')
lines.append('')
lines.append('struct PinyinEntry { uint32_t cp; const char* py; };')
lines.append('')
lines.append('static const PinyinEntry kTable[] = {')
for cp, py in uniq:
    lines.append('    {0x%04X, "%s"},' % (cp, py))
lines.append('};')
lines.append('static const size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);')
lines.append('')
lines.append('// 股票名称高频多音字词覆盖（字符级取首读音会读错的多音词，手工维护）')
lines.append('// word 为 UTF-8 词，full=全拼，initials=首字母；匹配时优先于单字表')
lines.append('struct PolyEntry { const char* word; const char* full; const char* initials; size_t wordLen; };')
lines.append('#define POLY(w, f, i) { w, f, i, sizeof(w) - 1 }')
lines.append('static const PolyEntry kPoly[] = {')
lines.append('    POLY("银行",   "yinhang",   "yh"),')
lines.append('    POLY("重庆",   "chongqing", "cq"),')
lines.append('    POLY("西藏",   "xizang",    "xz"),')
lines.append('    POLY("厦门",   "xiamen",    "xm"),')
lines.append('};')
lines.append('#undef POLY')
lines.append('')
lines.append('}  // namespace')
lines.append('')
lines.append('static const char* lookup(uint32_t cp) {')
lines.append('    auto it = std::lower_bound(kTable, kTable + kTableSize, cp,')
lines.append('                              [](const PinyinEntry& e, uint32_t v) { return e.cp < v; });')
lines.append('    if (it != kTable + kTableSize && it->cp == cp) return it->py;')
lines.append('    return nullptr;')
lines.append('}')
lines.append('')
lines.append('// 解码一个 UTF-8 码点，p 前进到下一字符；返回 0 表示跳过该字节')
lines.append('static uint32_t decodeCp(const unsigned char*& p, const unsigned char* end) {')
lines.append('    const uint8_t c = *p;')
lines.append('    if (c < 0x80) { uint32_t cp = c; ++p; return cp; }')
lines.append('    if ((c & 0xE0) == 0xC0 && end - p >= 2) {')
lines.append('        uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return cp;')
lines.append('    }')
lines.append('    if ((c & 0xF0) == 0xE0 && end - p >= 3) {')
lines.append('        uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);')
lines.append('        p += 3; return cp;')
lines.append('    }')
lines.append('    if ((c & 0xF8) == 0xF0 && end - p >= 4) {')
lines.append('        uint32_t cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);')
lines.append('        p += 4; return cp;')
lines.append('    }')
lines.append('    ++p; return 0;  // 非法字节，跳过')
lines.append('}')
lines.append('')
lines.append('std::string pinyinInitials(const std::string& utf8Name) {')
lines.append('    std::string out;')
lines.append('    out.reserve(utf8Name.size());')
lines.append('    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8Name.data());')
lines.append('    const unsigned char* end = p + utf8Name.size();')
lines.append('    while (p < end) {')
lines.append('        // 多音字词覆盖优先匹配')
lines.append('        bool matched = false;')
lines.append('        for (const auto& e : kPoly) {')
lines.append('            if (static_cast<size_t>(end - p) >= e.wordLen && std::memcmp(p, e.word, e.wordLen) == 0) {')
lines.append('                out += e.initials;')
lines.append('                p += e.wordLen;')
lines.append('                matched = true;')
lines.append('                break;')
lines.append('            }')
lines.append('        }')
lines.append('        if (matched) continue;')
lines.append('        const uint32_t cp = decodeCp(p, end);')
lines.append('        if (cp >= \'a\' && cp <= \'z\') { out.push_back(static_cast<char>(cp)); continue; }')
lines.append('        if (cp >= \'A\' && cp <= \'Z\') { out.push_back(static_cast<char>(cp) + 32); continue; }')
lines.append('        const char* py = lookup(cp);')
lines.append('        if (py) out.push_back(*py);')
lines.append('    }')
lines.append('    return out;')
lines.append('}')
lines.append('')
lines.append('std::string pinyinFull(const std::string& utf8Name) {')
lines.append('    std::string out;')
lines.append('    out.reserve(utf8Name.size());')
lines.append('    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8Name.data());')
lines.append('    const unsigned char* end = p + utf8Name.size();')
lines.append('    while (p < end) {')
lines.append('        // 多音字词覆盖优先匹配')
lines.append('        bool matched = false;')
lines.append('        for (const auto& e : kPoly) {')
lines.append('            if (static_cast<size_t>(end - p) >= e.wordLen && std::memcmp(p, e.word, e.wordLen) == 0) {')
lines.append('                out += e.full;')
lines.append('                p += e.wordLen;')
lines.append('                matched = true;')
lines.append('                break;')
lines.append('            }')
lines.append('        }')
lines.append('        if (matched) continue;')
lines.append('        const uint32_t cp = decodeCp(p, end);')
lines.append('        if (cp >= \'a\' && cp <= \'z\') { out.push_back(static_cast<char>(cp)); continue; }')
lines.append('        if (cp >= \'A\' && cp <= \'Z\') { out.push_back(static_cast<char>(cp) + 32); continue; }')
lines.append('        const char* py = lookup(cp);')
lines.append('        if (py) out += py;')
lines.append('    }')
lines.append('    return out;')
lines.append('}')
lines.append('')
lines.append('}  // namespace utils')
lines.append('}  // namespace st')
lines.append('')

with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines) + '\n')

print('entries: %d' % len(uniq))
print('file size: %.1f KB' % (len('\n'.join(lines)) / 1024))
print('sample:', uniq[:5])
