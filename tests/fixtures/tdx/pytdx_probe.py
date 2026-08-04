# pytdx 直连对照：拉 600519 分时 + 报价，对照我们的 fixture
from pytdx.hq import TdxHq_API

api = TdxHq_API()
with api.connect('124.71.187.122', 7709):
    print("server count:", api.get_security_count(1))
    try:
        q = api.get_security_quotes([(1, '600519')])
        print("quote:", q)
    except Exception as e:
        print("quote ERR:", e)
    minute = api.get_minute_time_data(1, '600519')
    print("minute len:", len(minute) if minute else None)
    if minute:
        print("first3:", minute[:3])
        print("last3:", minute[-3:])
        prices = [r['price'] for r in minute]
        print(f"min={min(prices):.2f} max={max(prices):.2f} first={prices[0]:.2f} last={prices[-1]:.2f}")
        vols = [r.get('vol', 0) for r in minute]
        print("total vol:", sum(vols))
