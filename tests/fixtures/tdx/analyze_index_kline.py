# 指数K线记录边界逆向：要求 5 根时间有效且连续
import struct

data = open("kline_000001_day.bin", "rb").read()
print(f"total={len(data)} head={data[:2].hex()}")

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

def fmt_date(v):
    y, m, d = v // 10000, (v % 10000) // 100, v % 100
    return f"{y}-{m:02d}-{d:02d}"

# 尝试不同 extra 模式（在 amount 之后）
def try_parse(extra_mode, label):
    pos = 2
    recs = []
    for i in range(5):
        if pos + 4 > len(data): return None
        tm = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        vals = []
        for _ in range(4):
            v, pos = varint(data, pos)
            vals.append(v)
            if pos > len(data): return None
        if pos + 8 > len(data): return None
        vol = data[pos:pos+4]; amount = data[pos+4:pos+8]
        pos += 8
        # extra
        if extra_mode == "skip2": pos += 2
        elif extra_mode == "skip4": pos += 4
        elif extra_mode == "skip6": pos += 6
        elif extra_mode == "skip8": pos += 8
        elif extra_mode == "2varint":
            for _ in range(2):
                v, pos = varint(data, pos)
        if pos > len(data): return None
        recs.append((tm, vals, pos))
    return recs

# 校验时间有效性（2024~2028，月 1-12，日 1-31）
def check(recs, label):
    if not recs: return
    ok = all(2020 <= r[0]//10000 <= 2030 and 1 <= (r[0]%10000)//100 <= 12 and 1 <= r[0]%100 <= 31 for r in recs)
    times = [r[0] for r in recs]
    desc = " / ".join(fmt_date(r[0]) for r in recs)
    if ok:
        print(f"  *** {label}: 全部有效日期 {desc}")
        # 打印价格（厘→元）
        for r in recs:
            o = r[1][0] / 1000.0
            print(f"    {fmt_date(r[0])} open={o:.2f} end_pos={r[2]}")

for m in ("skip2", "skip4", "skip6", "skip8", "2varint"):
    r = try_parse(m, m)
    if r: check(r, m)
