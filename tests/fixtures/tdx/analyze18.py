# 按 pytdx 18 字节记录 <HffII> 扫描分时 payload，校验基准
# 基准: 开1350.06 高1350.94 低1328.36 收1328.36
import struct

data = open("minute_600519.bin", "rb").read()
print(f"total={len(data)}")

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

# 先找 quote 块结束（price=132836, last/open/high/low 差分已在前面确认）
# d0 price=132836, 接 lastDiff/openDiff/highDiff/lowDiff/rev0/rev1/vol/curVol, 之后 4B amount
d = data[13:]
pos = 0
fields = []
for _ in range(8):
    v, pos = varint(d, pos)
    fields.append(v)
# amount: decodeVolume(rdU32)
import struct as S
def decode_volume(val):
    ivol = val
    if ivol >= 2**31: ivol -= 2**32
    logpoint = (ivol >> 24) & 0xFF
    hleax = (ivol >> 16) & 0xFF
    lheax = (ivol >> 8) & 0xFF
    lleax = ivol & 0xFF
    dwEcx = logpoint * 2 - 0x7F
    dwEdx = logpoint * 2 - 0x86
    x6 = 2.0 ** dwEcx
    if hleax > 0x80:
        x4 = 2.0 ** (dwEdx + 1) * (64.0 + (hleax & 0x7F))
    else:
        x4 = x6 * hleax / 128.0
    scale = 2.0 if (hleax & 0x80) else 1.0
    x3 = x6 * lheax / 32768.0 * scale
    x1 = x6 * lleax / 8388608.0 * scale
    return x6 + x4 + x3 + x1

if pos + 4 <= len(d):
    amount = decode_volume(S.unpack_from("<I", d, pos)[0])
    pos += 4
print(f"quote块: price={fields[0]/100:.2f} last={fields[1]} open={fields[2]} high={fields[3]} low={fields[4]} vol={fields[6]} curvol={fields[7]} amount={amount:.0f}")
print(f"quote块结束 offset={pos} (d), 剩余={len(d)-pos}")

# 18 字节记录扫描
for hdr_off in range(11, 17):
    rec_start = hdr_off
    prices = []
    ok_all = True
    p = rec_start
    while p + 18 <= len(d):
        raw_time, price, avg, vol, amt = S.unpack_from("<HffII", d, p)
        prices.append((raw_time, price, avg, vol, amt))
        p += 18
    if len(prices) < 5:
        continue
    first_p, last_p = prices[0][1], prices[-1][1]
    lo = min(x[1] for x in prices)
    hi = max(x[1] for x in prices)
    nf = abs(first_p - 1328.36) < 0.5 and abs(last_p - 1350.06) < 0.5
    of = abs(first_p - 1350.06) < 0.5 and abs(last_p - 1328.36) < 0.5
    lowok = abs(lo - 1328.36) < 0.6
    highok = abs(hi - 1350.94) < 0.6
    print(f"hdr_off={hdr_off} recs={len(prices)} first={first_p:.2f} last={last_p:.2f} lo={lo:.2f} hi={hi:.2f} "
          f"{'NEWEST-MATCH' if nf and lowok and highok else ('OLDEST-MATCH' if of and lowok and highok else '')}")
    if nf and lowok and highok:
        print("  === 匹配! 前5:", [(t, f"{p:.2f}", f"{a:.2f}", v) for t, p, a, v, _ in prices[:5]])
