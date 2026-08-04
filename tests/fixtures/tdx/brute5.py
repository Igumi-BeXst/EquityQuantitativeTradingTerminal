# 3字段布局，滑动窗口 240 条记录累积，找匹配基准的窗口
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

# 解析所有记录（3字段），收集 price 字段
recs = []
pos = 13
while pos < N:
    vals = []
    for f in range(3):
        v, pos = varint(data, pos)
        vals.append(v)
    recs.append(vals)
print(f"总记录数={len(recs)}")

print("前 12 条记录字段:")
for i, r in enumerate(recs[:12]):
    print(f"  R{i}: {r}")

# 窗口扫描
print("== 窗口扫描 240 条 ==")
for s in range(0, len(recs) - 239):
    win = recs[s:s+240]
    cum, fen = [], 0.0
    for r in win:
        fen += r[0]
        cum.append(fen / 100.0)
    first, last = cum[0], cum[-1]
    lo, hi = min(cum), max(cum)
    for order, f0, f1 in (("newest", 1328.36, 1350.06), ("oldest", 1350.06, 1328.36)):
        if abs(first - f0) < 0.4 and abs(last - f1) < 0.4 and abs(lo - 1328.36) < 0.5 and abs(hi - 1350.94) < 0.5:
            print(f"  *** MATCH start={s} {order}: first={first:.2f} last={last:.2f} lo={lo:.2f} hi={hi:.2f}")

# 也试每个记录 price 字段不用累积（绝对）
print("== 绝对价格扫描 ==")
for s in range(0, len(recs) - 239):
    win = [r[0] / 100.0 for r in recs[s:s+240]]
    first, last = win[0], win[-1]
    lo, hi = min(win), max(win)
    for order, f0, f1 in (("newest", 1328.36, 1350.06), ("oldest", 1350.06, 1328.36)):
        if abs(first - f0) < 0.4 and abs(last - f1) < 0.4 and abs(lo - 1328.36) < 0.5 and abs(hi - 1350.94) < 0.5:
            print(f"  *** MATCH start={s} {order}: first={first:.2f} last={last:.2f} lo={lo:.2f} hi={hi:.2f}")
