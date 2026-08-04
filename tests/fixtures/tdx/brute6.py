import struct
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

# 打印 [13] 全解析首尾
pos = 13; recs = []
while pos < N:
    v = []
    for f in range(3):
        x, pos = varint(data, pos)
        v.append(x)
    recs.append(v)
print(f"[13] 3字段 记录数={len(recs)}")
cum, fen = [], 0.0
for r in recs:
    fen += r[0]; cum.append(fen)
print(f"  cum[0]={cum[0]/100:.2f} cum[-1]={cum[-1]/100:.2f} min={min(cum)/100:.2f} max={max(cum)/100:.2f}")
print(f"  abs[0]={recs[0][0]/100:.2f} abs[-1]={recs[-1][0]/100:.2f}")

def check_seq(seq, tag):
    if len(seq) < 240: return
    for name, s in (("cum", seq), ("abs", seq)):
        first, last = s[0], s[-1]
        lo, hi = min(s), max(s)
        for order, f0, f1 in (("newest", 1328.36, 1350.06), ("oldest", 1350.06, 1328.36)):
            if abs(first - f0) < 0.4 and abs(last - f1) < 0.4 and abs(lo - 1328.36) < 0.5 and abs(hi - 1350.94) < 0.5:
                print(f"  *** MATCH {tag} {name}/{order}: first={first:.2f} last={last:.2f} lo={lo:.2f} hi={hi:.2f}")

# 综合穷举
print("== 综合穷举 ==")
for hdr in (6, 8, 13, 14):
    for nf in (2, 3, 4):
        for price_idx in (0, 1):
            recs, pos, ok = [], hdr, True
            while pos < N:
                v = []
                for f in range(nf):
                    x, pos = varint(data, pos)
                    if pos > N: ok = False; break
                    v.append(x)
                if not ok: break
                recs.append(v)
            if len(recs) < 241: continue
            prices = [r[price_idx] / 100.0 for r in recs]
            # 累积版
            c = [0.0]*len(prices); acc = 0.0
            for i, p in enumerate(prices): acc += p; c[i] = acc
            for s in range(0, len(recs) - 239):
                for name, seq in (("cum", c[s:s+240]), ("abs", prices[s:s+240])):
                    first, last = seq[0], seq[-1]
                    lo, hi = min(seq), max(seq)
                    for order, f0, f1 in (("newest", 1328.36, 1350.06), ("oldest", 1350.06, 1328.36)):
                        if abs(first - f0) < 0.4 and abs(last - f1) < 0.4 and abs(lo - 1328.36) < 0.5 and abs(hi - 1350.94) < 0.5:
                            print(f"  *** MATCH hdr={hdr} nf={nf} pidx={price_idx} start={s} {name}/{order}")
