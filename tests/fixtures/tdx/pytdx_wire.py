# monkeypatch pytdx 客户端 socket，转储 get_minute_time_data 的原始请求/响应
import struct
from pytdx.hq import TdxHq_API

api = TdxHq_API()
captured = {}

def make_wrap(api):
    client = api.client
    orig_send = client.send
    orig_recv = client.recv
    def send(data):
        captured['req'] = bytes(data)
        print(f"[REQ {len(data)}B] {bytes(data).hex()}")
        return orig_send(data)
    def recv(n):
        b = orig_recv(n)
        captured.setdefault('resp_head', b'')
        if len(captured['resp_head']) < 16:
            captured['resp_head'] += b
        captured.setdefault('resp', b'')
        if len(captured['resp']) < 128:
            captured['resp'] += b
        print(f"[RESP recv {n}B got {len(b)}B] {b[:24].hex()}")
        return b
    client.send = send
    client.recv = recv

with api.connect('124.71.187.122', 7709):
    print("connect ok")
    make_wrap(api)
    print("== get_minute_time_data ==")
    minute = api.get_minute_time_data(1, '600519')
    print("parsed len:", len(minute) if minute else None)
    if minute:
        print("first3:", minute[:3], "last3:", minute[-3:])
    import os
    if 'req' in captured:
        open('pytdx_minute_req.bin', 'wb').write(captured['req'])
        print("saved pytdx_minute_req.bin")
    if 'resp_head' in captured and len(captured['resp_head']) >= 16:
        open('pytdx_minute_head.bin', 'wb').write(captured['resp_head'][:16])
        print("resp_head:", captured['resp_head'][:16].hex())
