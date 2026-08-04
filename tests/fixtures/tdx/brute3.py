# 穷举 header 偏移，3 变长字段 [price,rev,vol] 累积布局，校验基准
# 基准(2026-08-04 600519): 开1350.06 高1350.94 低1328.36 收1328.36
data = open("minute_600519.bin", "rb").read()

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

def check(prices, tag):
    if len(prices) < 240: return False
    # 累积
    cum, fen = [], 0.0
    for p in prices:
        fen += p; cum.append(fen / 100.0)
    for name, seq in (("cum", cum), ("abs", prices)):
        if len(seq) < 240: continue
        first, last = seq[0], seq[-1]
        lo, hi = min(seq[:240]), max(seq[:240])
        nf = abs(first - 1328.36) < 0.5 and abs(last - 1350.06) < 0.5
        of = abs(first - 1350.06) < 0.5 and abs(last - 1328.36) < 0.5
        if (nf or of) and abs(lo - 1328.36) < 0.6 and abs(hi - 1350.94) < 0.6:
            print(f"  *** MATCH {tag} {name}: first={first:.2f} last={last:.2f} "
                  f"min={lo:.2f} max={hi:.2f} ({'newest' if nf else 'oldest'})")
            return True
    return False

best = None
for hdr in range(11, 60):
    prices, pos, ok = [], hdr, True
    for i in range(240):
        vals = []
        for f in range(3):
            v, pos = varint(data, pos)
            vals.append(v)
            if pos > len(data): ok = False; break
        if not ok: break
        prices.append(vals[0])
    if len(prices) == 240:
        m = check(prices, f"hdr={hdr}")
        if m and best is None: best = hdr
print("best header offset:", best)

# 另测 2 字段 [price,vol] 布局
print("== 2字段 [price,vol] ==")
for hdr in range(11, 60):
    prices, pos, ok = [], hdr, True
    for i in range(240):
        vals = []
        for f in range(2):
            v, pos = varint(data, pos)
            vals.append(v)
            if pos > len(data): ok = False; break
        if not ok: break
        prices.append(vals[0])
    if len(prices) == 240:
        check(prices, f"hdr={hdr}")
