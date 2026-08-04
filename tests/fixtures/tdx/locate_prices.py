# 在原始分时字节里定位 132836/135094/132894/135006 (分) 的变长编码位置
data = open("minute_600519.bin", "rb").read()
HDR = 13
d = data[HDR:]

def varint(buf, pos):
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

# 扫描所有 varint 位置和值
targets = {132836: "close", 135094: "high", 132894: "low", 135006: "open"}
found = {v: [] for v in targets}
rows = []
pos = 0
while pos < len(d):
    v, npos = varint(d, pos)
    rows.append((pos, v, npos - pos))
    if v in targets:
        found[v].append(pos)
    pos = npos

print(f"总字节={len(d)} 记录数={len(rows)}")
print("目标 varint 位置:")
for v, name in targets.items():
    print(f"  {name} {v}: {found[v]}")

# 打印前 20 个 varint
print("前 24 个 varint (offset, value, len):")
for r in rows[:24]:
    print(f"  {r[0]:4d} {r[1]:9d} {r[2]}B")

# 看 close(0) 和 open 之间的间距
if found[132836]:
    c0 = found[132836][0]
    print(f"close 首个在 {c0}, 后续 close 位置: {found[132836]}")
