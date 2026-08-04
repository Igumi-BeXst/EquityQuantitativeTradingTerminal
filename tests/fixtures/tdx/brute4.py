# 穷举字段数2/3/4 × header 8..55，报告匹配与末尾偏移
data = open("minute_600519.bin", "rb").read()
N = len(data)

def varint(buf, pos):
    val, i, start = 0, 0, pos
    while pos < len(buf):
        b = buf[pos]
        val += (b & 0x3F) if i == 0 else ((b & 0x7F) << (6 + (i - 1) * 7))
        cont = (b & 0x80) != 0
        pos += 1; i += 1
        if not cont: break
    if pos > start and (buf[start] & 0x40): val = -val
    return val, pos

def score(prices):
    if len(prices) < 240: return False, None
    cum, fen = [], 0.0
    for p in prices:
        fen += p; cum.append(fen / 100.0)
    best = None
    for name, seq in (("cum", cum), ("abs", prices)):
        first, last = seq[0], seq[-1]
        lo, hi = min(seq[:240]), max(seq[:240])
        for order, f0, f1 in (("newest", 1328.36, 1350.06), ("oldest", 1350.06, 1328.36)):
            if abs(first - f0) < 0.4 and abs(last - f1) < 0.4 and abs(lo - 1328.36) < 0.5 and abs(hi - 1350.94) < 0.5:
                best = (name, order)
    return best is not None, best

print(f"total={N}")
for nf in (2, 3, 4):
    for hdr in range(8, 56):
        prices, pos, ok = [], hdr, True
        for i in range(240):
            for f in range(nf):
                v, pos = varint(data, pos)
                if pos > N: ok = False; break
            if not ok: break
            prices.append(v)
            if not ok: break
        if len(prices) != 240: continue
        m, b = score(prices)
        if m:
            print(f"  *** MATCH nf={nf} hdr={hdr} {b} end={pos} used={pos-hdr}")

# 也试记录数=从数据算出的可能值，看最接近 1268 的
print("== 末尾偏移自洽检查 (3字段 hdr=13) ==")
pos = 13; n = 0
while pos < N:
    for f in range(3):
        v, pos = varint(data, pos)
    n += 1
print(f"  3字段 hdr=13: 读完 {n} 条, 末尾 pos={pos} (总 {N})")

print("== 末尾偏移自洽检查 (2字段 hdr=13) ==")
pos = 13; n = 0
while pos < N:
    for f in range(2):
        v, pos = varint(data, pos)
    n += 1
print(f"  2字段 hdr=13: 读完 {n} 条, 末尾 pos={pos} (总 {N})")
