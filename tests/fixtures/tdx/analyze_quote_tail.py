# 解码 600519 报价完整记录所有字段，找换手率（茅台今日换手率 ≈ 0.3%）
import struct

data = open("quote_6codes.bin", "rb").read()

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

pos = 4  # skip + count
mkt, code, active = struct.unpack_from("<B6sH", data, pos)
pos += 9
print(f"record: {mkt}{code.decode()}")

fields = []
price, pos = varint(data, pos)
fields.append(("price", price))
for name in ("last_diff", "open_diff", "high_diff", "low_diff"):
    v, pos = varint(data, pos); fields.append((name, v))
for name in ("rev0(时间)", "rev1", "vol", "cur_vol"):
    v, pos = varint(data, pos); fields.append((name, v))
amt_raw = struct.unpack_from("<I", data, pos)[0]; pos += 4
fields.append(("amount_raw", amt_raw))
for name in ("s_vol", "b_vol", "rev2", "rev3"):
    v, pos = varint(data, pos); fields.append((name, v))
for k in range(1, 6):
    for name in (f"bid{k}", f"ask{k}", f"bidvol{k}", f"askvol{k}"):
        v, pos = varint(data, pos); fields.append((name, v))
rev4 = struct.unpack_from("<H", data, pos)[0]; pos += 2
fields.append(("rev4", rev4))
for name in ("rev5", "rev6", "rev7", "rev8"):
    v, pos = varint(data, pos); fields.append((name, v))
rev9, active2 = struct.unpack_from("<hH", data, pos); pos += 4
fields.append(("rev9(涨速)", rev9)); fields.append(("active2", active2))

for name, v in fields:
    print(f"  {name:12s} = {v}")
print(f"end pos={pos}")

# 换手率 0.298% = 0.298 或 29.8 或 2980 等
print("\n== 可能的换手率候选（0.1~10 或 ~30 量级）==")
for name, v in fields:
    fv = abs(v)
    if (0.05 <= fv <= 50) or (v and (name.startswith("rev") or name.startswith("s_vol") or name.startswith("b_vol"))):
        print(f"  {name} = {v}")
