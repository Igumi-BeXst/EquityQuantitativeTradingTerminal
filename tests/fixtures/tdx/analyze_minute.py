# 分时 0x051D 布局逆向 — 用真实基准校验
# 基准(2026-08-04 600519): 开1350.06 高1350.94 低1328.36 收1328.36 昨收1358.98
import struct

def read_varint(buf, pos):
    data = 0
    i = 0
    start = pos
    while pos < len(buf):
        b = buf[pos]
        if i == 0:
            data += b & 0x3F
        else:
            data += (b & 0x7F) << (6 + (i - 1) * 7)
        cont = (b & 0x80) != 0
        pos += 1
        i += 1
        if not cont:
            break
    if pos > start and (buf[start] & 0x40):
        data = -data
    return data, pos

data = open("minute_600519.bin", "rb").read()
print(f"total={len(data)} header0-13={data[:13].hex()}")

HDR = 13
d = data[HDR:]

def try_layout(nfields, header_skip=0, label=""):
    """nfields 个变长字段/记录，取第0个字段为价格。返回价格序列（直接值，非累积）"""
    prices = []
    pos = header_skip
    recs = 0
    while recs < 240 and pos < len(d):
        vals = []
        for f in range(nfields):
            v, pos = read_varint(d, pos)
            vals.append(v)
            if pos > len(d):
                return None
        prices.append(vals[0])
        recs += 1
    return prices

def check(prices, tag):
    if prices is None or len(prices) < 240:
        return
    # 累计差分 vs 直接绝对：两种都看
    fen = 0.0
    cum = []
    for p in prices:
        fen += p
        cum.append(fen / 100.0)
    absv = [p / 100.0 for p in prices]
    for name, seq in (("cum", cum), ("abs", absv)):
        first, last = seq[0], seq[-1]
        lo, hi = min(seq), max(seq)
        # newest-first: first=close, last=open
        nf = abs(first - 1328.36) < 0.5 and abs(last - 1350.06) < 0.5
        # oldest-first: first=open, last=close
        of = abs(first - 1350.06) < 0.5 and abs(last - 1328.36) < 0.5
        lowok = abs(lo - 1328.36) < 0.6
        highok = abs(hi - 1350.94) < 0.6
        if (nf or of) and lowok and highok:
            print(f"  *** MATCH {tag} {name}: first={first:.2f} last={last:.2f} "
                  f"min={lo:.2f} max={hi:.2f} ({'newest' if nf else 'oldest'})")

print("== 2字段 (price,vol) ==")
try_layout(2)
print("== 3字段 (price,skip,vol) ==")
try_layout(3)
print("== 2字段 跳过1B头 ==")
try_layout(2, 1)
print("== 3字段 跳过1B头 ==")
try_layout(3, 1)

# 穷举字段数 2-6 + 头跳过 0-4
print("== 穷举 fields 2..6 x hdr 0..4 ==")
for nf in range(2, 7):
    for hs in range(0, 5):
        p = try_layout(nf, hs, f"nf{nf}hs{hs}")
        if p and len(p) >= 240:
            check(p, f"nf{nf}hs{hs}")

# 另测固定 5 字节记录
print("== 固定 5B/记录 (int16 price + int16 vol + 1B) ==")
if len(d) % 5 == 0:
    for order in ("newest", "oldest"):
        prices = []
        for i in range(0, len(d), 5):
            pr, _ = struct.unpack_from("<hH", d, i)
            prices.append(pr / 100.0)
        if order == "oldest":
            prices = prices[::-1]
        if len(prices) >= 240:
            check(prices, f"fixed5_{order}")
