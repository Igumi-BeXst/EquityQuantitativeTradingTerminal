# 验证完整 pytdx 报价字段序列能正确解析 6 只报价
import struct

data = open("quote_6codes.bin", "rb").read()
print(f"total={len(data)} head={data[:4].hex()}")

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

pos = 4  # skip 2 + count(2)
expected = ["600519", "601318", "600036", "000001", "300750", "000858"]
for idx in range(6):
    start = pos
    mkt, code, active = struct.unpack_from("<B6sH", data, pos)
    code = code.decode("ascii")
    pos += 9
    price, pos = varint(data, pos)
    lc, pos = varint(data, pos)
    op, pos = varint(data, pos)
    hi, pos = varint(data, pos)
    lo, pos = varint(data, pos)
    rev0, pos = varint(data, pos)
    rev1, pos = varint(data, pos)
    vol, pos = varint(data, pos)
    curvol, pos = varint(data, pos)
    amt_raw = struct.unpack_from("<I", data, pos)[0]; pos += 4
    # 剩余字段全部消费
    for _ in range(2): s, pos = varint(data, pos)   # s_vol, b_vol
    for _ in range(2): s, pos = varint(data, pos)   # rev2, rev3
    for _ in range(5):  # 五档 20 个
        for _ in range(4): s, pos = varint(data, pos)
    pos += 2  # rev4 uint16
    for _ in range(4): s, pos = varint(data, pos)   # rev5-8
    pos += 4  # rev9 int16 + active2 uint16
    price_y = price / 100.0
    preclose_y = (price + lc) / 100.0
    ok = "OK" if code == expected[idx] else "WRONG"
    print(f"[{idx}] {mkt}{code} price={price_y:.2f} preClose={preclose_y:.2f} "
          f"vol={vol} curvol={curvol} 消耗={pos-start}B  {ok}")
print(f"最终 pos={pos}/{len(data)} {'✓' if pos==len(data) else '✗ 不一致'}")
