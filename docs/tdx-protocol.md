# 通达信 TDX 协议笔记

直连通达信行情主站（TCP :7709）的私有二进制协议实现记录。
参考：injoyai/tdx (Go)、pytdx (Python)。所有字节偏移均经 `tools_tdx_diag` 实连抓包校准。

## 帧格式

### 请求帧（12 字节头 + 数据）

| 偏移 | 大小 | 字段 | 值 |
|---|---|---|---|
| 0 | 1 | Prefix | `0x0C` |
| 1-4 | 4 | MsgID | 递增 |
| 5 | 1 | Control | `0x01` |
| 6-7 | 2 | Length×2 | data.size()+2 |
| 8-9 | 2 | Length×2（重复） | 同 6-7 |
| 10-11 | 2 | Type (Cmd) | LE |
| 12+ | - | Data | 请求体 |

### 响应帧（16 字节头 + 数据）

| 偏移 | 大小 | 字段 |
|---|---|---|
| 0-3 | 4 | Prefix `B1 CB 74 00`（大端字节序，逐字节比较） |
| 4 | 1 | Control |
| 5 | 1 | MsgID(低) |
| 6-7 | 2 | Unknown |
| 8-9 | 2 | Type (Cmd) |
| 10-11 | 2 | ZipLength |
| 12-13 | 2 | Length |

- `ZipLength == Length`：数据未压缩，直接取 `[16:]`
- 否则：数据 zlib 解压，解压后大小须等于 Length

## 命令 ID

| Cmd | ID | 说明 | 请求体 |
|---|---|---|---|
| Connect | `0x000D` | 登录握手 | `{0x01}` |
| Heart | `0x0004` | 心跳（30s） | 空 |
| Login2 | `0x0FDB` | 备用登录 | - |
| Count | `0x044E` | 证券数量 | `{market, 0x00, 0x75, 0xC7, 0x33, 0x01}` |
| Code | `0x0450` | 证券列表分页 | `{market, 0x00, start_lo, start_hi}` |
| Quote | `0x053E` | 实时报价 | `{0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00, count_u16, (market,code6)*}` |
| Minute | `0x051D` | 当日分时 | `{market, 0x00, code6, 0x00×4}` |
| Kline | `0x052D` | K线 | 见下 |
| Gbbq | `0x000F` | 除权除息 | `{0x01, 0x00, market, code6}` |
| BidOrder | `0x0F82` | 五档盘口 | 预留 |

### K线请求体

```
market(1) 0x00 code(6) category(1) 0x00 0x01 0x00 start(2) count(2) 0x00×10
```

- category：0=5分 1=15分 2=30分 3=60分 7=1分 9=日线 5=周 6=月 8=季 4=年（默认日线 9）
- start 从最新计 0，单次 ≤800

### Count 请求体 `{market, 0x00, 0x75, 0xC7, 0x33, 0x01}`
市场字节在**首位**（`market-first`）。实测市场顺序 = 首位，第二个字节固定 `0x00`，尾部 4 字节固定魔数。注意：**请求体第一个字节是 market（1=沪 0=深），不是保留位**。

## 市场编码

| TDX 字节 | Market |
|---|---|
| 0 | SZ 深市 |
| 1 | SH 沪市 |
| 2 | BJ 北交所 |

## 变长整数（decodeVarInt）

- 第一字节：低 6 位为数据，bit6 为符号位，bit7 为续位
- 后续字节：低 7 位数据，bit7 为续位
- 每字节贡献 `(byte & 0x7F) << (6 + (i-1)*7)`

## 量解码（decodeVolume / getVolume2）

32 位打包的浮点数量：
- `logpoint = v >> 24`，`hleax = (v>>16)&0xFF`，`lheax = (v>>8)&0xFF`，`lleax = v&0xFF`
- `dbl_xmm6 = 2^(logpoint*2 - 0x7F)`，按 hleax 是否 >0x80 分两支，再加 2 个尾数项

## K线记录（decodeKline）

响应 `payload[0:2]` = 条数，随后每条：

| 偏移 | 编码 | 字段 |
|---|---|---|
| 0-3 | 4B 时间 | 日线 = YYYYMMDD 十进制小端（`72 27 35 01`→20260722）；分钟 = 位压缩 ym/hm |
| + | 变长 | 开（相对昨收差分） |
| + | 变长 | 收（相对开差分） |
| + | 变长 | 高（相对开差分） |
| + | 变长 | 低（相对开差分） |
| + | 4B | 成交量（decodeVolume，手→股 ×100） |
| + | 4B | 成交额（元） |

价格单位厘（÷1000 → 元）。差分：`open = openOff + lastClose`；`close = lastClose + openOff + closeOff`；`high/low` 相对 open。
注意：**指数 K线记录比个股多 4 字节**（涨跌家数），`decodeKline(..., isIndex)` 跳过。指数判定：SH `000xxx`（上证指数/沪深300/科创50…）、SZ `399xxx`。

## 报价记录（decodeQuote）

`payload[0:2]` 跳过，`[2:4]` = 条数，随后每条（**必须消费完整记录才能对齐下一条**，含五档等尾部字段）：

| 编码 | 字段 |
|---|---|
| 1B + 6B + 2B | 市场 + 代码 + 活跃标志 |
| 变长 | price（分 → ÷100 元） |
| 变长 | last 差分（昨收 = price+last） |
| 变长 | open 差分 |
| 变长 | high 差分 |
| 变长 | low 差分 |
| 变长 | reversed_bytes0（服务时间） |
| 变长 | reversed_bytes1（应为 -price） |
| 变长 | 成交量（手 → ×100 股） |
| 变长 | cur_vol 现量 |
| 4B | 成交额（decodeVolume，元） |
| 变长×4 | s_vol / b_vol / reversed2 / reversed3 |
| 变长×20 | 五档：bid_k/ask_k/bid_vol_k/ask_vol_k (k=1..5) |
| 2B | reversed_bytes4 (uint16) |
| 变长×4 | reversed_bytes5-8 |
| 2B + 2B | reversed_bytes9 (int16，涨速) + active2 (uint16) |

**教训**：旧实现只读前 11 字段（到 amount）就跳下一条 → 多代码批量请求记录错位（6 只只解析对 1 只）。单只时前几个字段恰好正确所以未暴露。fixture `quote_6codes.bin` 回归锁定。

## 证券列表记录（decodeCodeList）

响应 `payload[0:2]` = 条数（每页 ≤1000），随后每条 **29 字节**：

| 偏移 | 大小 | 字段 |
|---|---|---|
| 0-5 | 6 | 代码（ASCII） |
| 6-7 | 2 | 常量 `0x0064`（用途不明） |
| 8-15 | 8 | 名称（GBK，UTF-8 转换 + normalize） |
| 16-19 | 4 | skip |
| 20 | 1 | decimal |
| 21-24 | 4 | 最新价（float） |
| 25-28 | 4 | skip |

**重要：记录内不含市场字段**——市场由请求隐含，调用方传入。曾误读记录首字节（代码首字符）为市场导致全部过滤为 0。

## 分时（0x051D）— 服务器非标准变体，未校准（降级）

**实测结论（2026-08-04，fixture `tests/fixtures/tdx/minute_600519.bin` 1268B）**：
- 响应结构：`count(2)=240 + [2:4]2B + market(1B@[4])=01 + code(6B@[5]) + [11:13]2B + 数据[13:]`
- 数据 [13:] 按 3 变长字段 `[price,?,vol]` 恰好读满 **250 条**、精确消耗 1255B 无余量
- 前 3 条（9 个变长字段）是 **quote 前导块**：price=132836(close) / lastDiff=3062(preClose) / openDiff=2170(open) / highDiff=2258(high) / lowDiff=0(low) / rev0 / rev1 / vol=37450(手) / curVol —— 与 decodeQuote 字段序列完全一致，全部命中真实基准
- **真实分时记录布局未破解**：穷举 2/3/4 字段 × 偏移 {4,6,8,13,14} × 累积/绝对 × 正反序 + 240 条滑动窗口，无任何布局同时满足 first=close(1328.36)/last=open(1350.06)/min=low/max=high
- **与 pytdx 同请求同响应对照**：pytdx `get_minute_time_data` 发完全相同的请求字节（`01 00 36 30 30 35 31 39 00 00 00 00`），收到**相同 1268B 响应**，官方解析器产出同样垃圾（0.01→2623.17）。pytdx docstring 的标准响应头是 `f0 00 00 00 a2 08...`（记录从 [4] 起），而**该服务器 [4] 是 market+code** —— 响应头嵌入 market/code，属非标准变体，标准解析器（pytdx/injoyai）全部失配
- injoyai 自注释"todo 解析好像不对"；该服务器报价命令 pytdx `get_security_quotes` 也返回 []（我们的 decodeQuote 反而正确，说明本服务器协议为变体）

**当前降级**：`getIntraday` 返回 nullopt，分时图显示"无数据"。备选方案：0x0FC5 分时成交明细聚合（需先验证该命令在此服务器可用）。fixture 与分析脚本保留在 `tests/fixtures/tdx/`（analyze*.py / brute*.py / pytdx_*.py）供后续排查。

## 前复权（qfq）

- 拉 gbbq（0x000F）除权除息事件，缓存 per-code
- 事件仿射变换：`m = (10 + songZhuanGu + peiGu)/10`，`c = (fenHong - peiGu*peiGuJia)/10`
- 对每根 bar（升序），从最新事件开始累积因子，直到 `event.date <= barDate`：
  `M = M/m`，`A = (A - c)/m`，价格 `P_adj = round((P*M + A)*100)/100`
- 日/周/月/季/年前复权；分钟线不复权

## 服务器列表

默认 8 个公开主站（IP 可能变更，config `data.tdx.servers` 可覆盖）：
`124.71.187.122, 180.153.18.170, 60.12.136.250, 218.75.126.9, 115.238.90.165, 119.147.212.81, 101.227.73.20, 221.231.141.60`（均 :7709）

连接失败自动切换下一个，成功服务器优先重连。已实测首个 124.71.187.122 可达。

## 实测基准（2026-08-04）

- 登录 ✓、日/周/月K前复权 ✓（茅台 2026-08-04 收 1328.36，与报价一致）
- 报价 ✓（SH600519 现价 1328.36 昨收 1358.98 跌 -2.25% 量 3745000 额 50.04 亿）
- SH 列表 27642 只（Count=27642），SZ 列表 23906 只；茅台命中
