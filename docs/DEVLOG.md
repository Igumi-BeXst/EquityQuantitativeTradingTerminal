# 开发日志 (Development Log)

## 2026-08-05 — 分时图 UI 精修 + 搜索框拼音搜索 & 焦点修复

### 已完成（用户实测迭代，未提交）
1. **分时图面板分隔**：量区/MACD 区像日线图一样**交替底色 + 顶部分隔线**，主图与指标明显分隔
2. **昨收标签重叠**：昨收标签与右下 高/低 标签重叠 → 标签 `clamp(top+32, bottom-46)` 夹在中间空白带
3. **分时主图动态分段轴**：涨/跌**各 6 段**对称等分（`symRange_=max(单侧波动)`，昨收居中虚线 0%），左轴价格、右轴涨跌幅（红涨绿跌）；替换原右上现价/涨跌幅、右下高/低临时标注；新增左轴带宽 56px，xFor/柱宽/十字线改用 mainRect_ 定位
4. **搜索框无拼音**：根因 TDX 股票列表只有代码+名称，`pinyinInitials` 硬编码空串。实现 `foundation/utils/pinyin.h/.cpp`：
   - 内嵌 44435 条 Unicode→拼音表（mozillazg/pinyin-data 生成，`scripts/generate_pinyin.py` 可重生成，去声调/ü→v/取首读音）
   - `pinyinInitials()`（首字母）与 `pinyinFull()`（全拼）
   - **多音字词覆盖**（银行→yh、重庆→cq、西藏→xz、厦门→xm），匹配优先于单字表
   - TdxProvider 填充 `pinyin`/`pinyinInitials`；StockSearchIndex 增加**全拼匹配**通道（第4通道）
5. **搜索框抢焦点**：`Qt::Popup` 弹层显示时抓取全应用鼠标键盘 → 其他控件无法交互。改 `Qt::ToolTip`（不抓取）+ `qApp` 级事件过滤器：点击编辑框/弹层之外关闭弹层**且不拦截事件**，一次点击同时关弹层+交互目标控件

### 测试
- 新增 `tests/test_foundation/test_pinyin.cpp`（7 用例：首字母/全拼/词覆盖/ü→v/ASCII/索引名）
- ctest **174/174** 通过，构建零警告

### 关键决策
- 拼音用**首字母 + 全拼双通道**：首字母（gzmt）为默认习惯，全拼（guzhoumaotai）作补充
- 多音字仅覆盖**股票名称高频且无歧义**的词（银行/重庆/西藏/厦门）；重工/重科等 `重=zhòng` 与 `重庆` 冲突，暂不全局覆盖
- 拼音表 934KB 由脚本生成后**提交生成物**（仓库内置，离线可用），源数据 `scripts/pinyin_src.txt` + 生成脚本一并留存保证可重生成

### 待办
- [ ] GUI 复验：拼音搜索、弹层不抢焦点、分时 6+6 对称轴
- [ ] 多音字覆盖词表按需扩充（如 重 的上下文处理）

## 2026-08-04 — P9 TDX 通达信数据源（协议层 + Provider + 接口重构）

### 背景
用户要求参考 [injoyai/tdx](https://github.com/injoyai/tdx)（Go 通达信协议客户端）把数据源改为**直连通达信主站**（TCP :7709），完全替换腾讯数据源。已确认 C++ 直连二进制协议、覆盖全部 UI 消费（日/周/月K线+分时+实时盘口+股票列表）。

### 已完成（提交 c4d48a8，+2005 行）
- [x] **协议层** `src/data/tdx/`：TdxSocket（WinSock2 同步 TCP + TdxTransport 虚接口供测试注入）、tdx_protocol（帧编解码：请求 0x0C 12B 头 / 响应 0xB1CB7400 16B 头 + zlib 解压、变长整数、量解码 getVolume2、命令构造、tdxMarket/klineCategory）、tdx_models（K线差分价格/报价分单位/分时/除权除息/股票列表解码）
- [x] **TdxProvider**：单连接 mutex 串行 + 断线重连 + 8 服务器 failover（已更新为可连通 IP）；getBars 日/周/月**前复权**（gbbq 缓存 + 仿射变换 P_adj=(P-c)/m）；batchQuote 分块 60；getStockList 分页 1000；subscribeQuote pollThread + EventBus
- [x] **接口重构**：IDataProvider 加 batchQuote/getIntraday/refreshQuotes/providerName 纯虚；12 个 UI 类 `TencentProvider*`→`IDataProvider*`；main_window `makeDataProvider()`；provider_factory 读 config `data.provider`（默认 tdx）；AKShare 补桩；cn_encoding 抽取 GBK 工具；vcpkg 加 zlib
- [x] **实连验证**（tools_tdx_live）：登录 ✓、日/周/月K线前复权 ✓（茅台 2026-08-04 收 1328.36 与报价一致）、报价 ✓（量 3745000/额 50 亿）、列表解析 ✓（每页 1000）

### Step 9c 集成问题修复（用户实测反馈，本日）
用户启动软件实测发现 4 个问题，全部定位根因并修复：
1. **涨幅/跌幅榜 + 市场宽度错误**：根因 `decodeQuote` 只读前 11 个字段（到 amount）就跳下一条 → 多代码批量请求（MarketPanel 精选池 129 只分 3 块）记录错位，6 只只解析对 1 只。修复：按 pytdx 完整字段序列消费整条记录（s_vol/b_vol/rev2-3/**五档×20**/rev4(2B)/rev5-8/rev9+active2(4B)）。实连验证 6 只（茅台/平安/招行/平安银行/宁德/五粮液）价格全对。fixture `quote_6codes.bin` + `DecodeQuoteBatchFromFixture` 回归锁定
2. **指数K线错乱**：根因 指数 K线记录比个股**多 4 字节（涨跌家数）**，decodeKline 不跳 → 第二根起错位（日期 1899 垃圾）。修复：`decodeKline(payload, cat, isIndex)` + `isIndexCode`（SH `000xxx` / SZ `399xxx`），fetchBarsRaw 传入。实连验证上证指数 5 根（2026-07-29~08-04 开~3823/收~3828 合理）。fixture `kline_000001_day.bin` + `DecodeKlineIndexFromFixture`/`IsIndexCode` 回归
3. **搜索框每输入一个字符卡住（需切屏恢复）**：根因 `Qt::Popup` 弹层显示时**抢占键盘焦点** → 下个字符输入被弹层吞掉（QListWidget type-ahead 导航，表现为"往下选择"）；切屏（弹层关闭）才恢复。修复：弹层加 `WA_ShowWithoutActivating`（第一版）+ **popup 上装事件过滤器，导航键本地处理、可打印字符转发给 edit 继续输入**（第二版，`QCoreApplication::sendEvent(edit_, key)`）。GUI 需人工复验

### Step 9c 第二轮修复（用户复测反馈，本日）
1. **搜索框仍卡**（输入"000001"第二个0会往下选择）：WA_ShowWithoutActivating 不足以阻止 Qt::Popup 抢键盘 → 在 popup 装事件过滤器转发可打印字符给 edit
2. **涨幅/跌幅榜股票数量少**：原用精选 129 只。改为**全 A 股池**（getStockList 过滤可交易前缀 SH 600/601/603/605/688、SZ 000/001/002/003/300/301，~5000 只），异步加载完成后立即刷新；刷新间隔 10s→30s（全池拉取较重）
3. **换手率无数据**：实测 TDX 0x053E 报价记录**不含换手率**（解码尾部字段 rev4=2006/rev5-8=0/rev9=-3/active2=4197 无候选）；需流通股本（另一命令逐股贵）→ 换手率列显示 "—" 而非误导的 0.00%
4. **服务器报价批上限 = 80 只**（实测 200 只请求返回 80）→ quoteChunk_ 60→80
- 全量构建零警告；ctest **165/165**；tdx_live 回归正常

### Step 9c 第三轮修复（用户复测，跌幅榜 -100% 股）
- **症状**：跌幅榜出现一批 -100% 股票
- **根因**（`tools_tdx_market_probe` 全 A 股池 5209 只实测）：TDX 对**停牌股返回 price=0.00**（6 只：SZ000838/002214/300246/300311/300333/300862），`change=(0-preClose)/preClose` 误算成 -100%
- **修复**：MarketPanel 排名与市场宽度**排除 `lastPrice<=0 || preClose<=0`**（停牌/无行情不参与排序与涨跌家数统计）
- 新增 `tools_tdx_market_probe` 市场异常探测工具；ctest 165/165

### Step 10 单测 + 收尾（本日）
- **分时 0x051D 判定为服务器非标准变体，降级保留**：穷举 2/3/4 字段 × 偏移 × 累积/绝对全失败；pytdx `get_minute_time_data` 发相同请求收相同 1268B 响应，官方解析器同样产出垃圾（0.01→2623）——该服务器响应头嵌入 market+code（`[4]=01 [5:11]=600519`），标准解析器全部失配。quote 前导块已识别（price/last/open/high/low 差分全命中基准）。fixture `minute_600519.bin` + 分析脚本保留 `tests/fixtures/tdx/`，findings 写入 docs/tdx-protocol.md
- **transportFactory_ 注入**：doConnect 改用工厂创建传输（默认 TdxSocket），FakeTdxTransport 全链路可测
- **线程可中断**：pollLoop/heartbeatLoop 固定 sleep → 条件变量 wait_for，disconnect 立即唤醒（修复生产 5s/30s 关闭延迟 + 测试挂死）
- **真 bug 修复**：
  1. `loadStockList` `uint16_t start` + `maxStocks=100000`（> uint16 上限）→ `start<maxStocks` 恒真 + `start+=1000` 溢出回绕 → **无限循环**。改 uint32 + `start<=65535` 防回绕
  2. `decodeCodeList` 名称字段固定 8B，短名含 null 填充 → 未修剪导致名称尾部空字符。加尾部 null 修剪
- **单测**：`test_tdx_protocol.cpp`（编码/请求构造/响应压缩与未压缩/变长往返/时间日与分钟/量/Count/CodeList 市场传入+GBK+normalize/K线合成差分/报价合成 + fixture 驱动）与 `test_tdx_provider.cpp`（FakeTdxTransport：getBars 快乐路径/断线重连/batchQuote 分块 61→2 次/getStockList 分页 2500→3 页/qfqAdjust 10送10 前复权 ÷2/connect failover/订阅退订去重+poll 请求体观察）共 **26 用例**
- 全量构建零警告；ctest **162/162**（136 + 26）；tdx_live 回归正常（分时降级属预期）

### Step 9a 集成排查：getStockList 列表 bug（本日修复）
- **症状**：`tools_tdx_live` 显示 SH 列表 = 0 只 → 搜索栏/索引依赖 getStockList 会全空
- **根因**：`decodeCodeList` 把记录**首字节**（代码首字符，如 '9'=0x39）当作市场 → `marketFromByte(57)=Unknown` → 全部被 `isValid()` 过滤。实测记录内**不含市场字段**（市场由请求隐含），布局 `[0:6]代码 | [6:8]=0x0064 | [8:16]名称GBK(8B) | [16:29]其他`
- **修复**：`decodeCodeList(payload, Market)` 增加市场参数由调用方传入；`loadStockList` 传请求市场；列表上限 20000 硬截断改为按 Count 真实总数（安全上限 100000）
- **验证**：tdx_diag 实连对比 count mkt=1→27642 / mkt=0→23906；code mkt=1→"999999 上证指数"、mkt=0→"395001 主板A股"；tdx_live SH 列表 **27642 只 + 找到茅台: 贵州茅台**
- 新增 `docs/tdx-protocol.md` 协议笔记（帧格式/命令/变长/量解码/K线/报价/列表布局/复权/服务器列表，全部实连抓包校准）

### Step 8 构建验证（本日收尾）
- 脚本替换后首次编译，修复 2 处遗漏：
  1. `main_window.cpp` `std::makeDataProvider()` — 脚本保留 `std::` 前缀，改为 `makeDataProvider()`（namespace st 自由函数）
  2. `tools_chart_render.cpp` 用旧签名构造 `KLineChart(TencentProvider*)`/`TimelineChart(TencentProvider*)` — 改 `makeDataProvider()` + `provider.get()`
- 状态栏/About 数据源文案 `"腾讯行情"` → `provider_->providerName()` 动态显示
- `cmake --build --preset with-qt` 零错误零警告；`ctest` 136/136 全绿（含 test_tencent_provider 回归基线）

### 已知降级（Step 10 待办）
- **分时 0x051D 格式待校准**：实测数据起点 [13]（非参考实现 [6]），差分累积前 2 条对后续错位 → `getIntraday` 返回 nullopt（分时图显示"无数据"，不显示错误数据）。备选：0x0FC5 分时成交明细聚合
- 股票列表全量 28 页（27642 只）拉取较慢，需优化

### 测试
- 136/136（无新增单测，协议层靠 tools_tdx_live 实连验证；test_tdx_protocol/test_tdx_provider 留 Step 10）

## 2026-08-03 — P7 优化崩溃修复 + 回测性能优化

### 背景
用户测试参数优化面板时应用崩溃（WER: `ucrtbased.dll` + `0x80000003` = Debug CRT/STL 断言断点）。

### 根因（两个叠加问题）
1. **BacktestEngine::getPortfolio 用函数级 static Portfolio**：GridSearch 并行跑多个 BacktestEngine 时，多线程同时写同一个 static → 数据竞争/堆损坏 → 策略遍历 positions 时撞上对方释放 → STL 断言崩溃。回测面板只跑单引擎所以从未触发。
2. **BacktestEngine 每 bar 整段拷贝历史**（`BarSeries series(hist)` O(n²)）：网格搜索 90 组合 × 6 股从 38s（单线程）到 123s（8 线程 Debug 堆锁竞争）——优化"假死"。

### 修复
- [x] `cachedPortfolio_` 实例成员替代函数级 static（每引擎独立，线程隔离）
- [x] **BarSeries 重构**：内部改 `shared_ptr<vector<Bar>>` 存储 + 新增 `append()`；backtest/paper_trade 热循环从"每 bar 整段拷贝"变 O(1) 追加
- [x] backtest_engine：删除死代码 `filteredBarsByCode`/`currentIndexByCode`，改 `map<StockCode, BarSeries> seriesByCode` 增量构建
- [x] paper_trade：`history_` 改 `map<string, BarSeries>` + `seedHistory` 按值移动
- [x] 优化面板：Debug 下 `parallelLanes=2`（实测 Debug CRT 堆全局锁使 8 线程 = 单线程 2.3 倍耗时），Release 用 maxThreadCount

### 性能对比（6 股 × 90 组合）
| lanes | 修复前 | 修复后 |
|---|---|---|
| 1 | 38s | 6.0s |
| 2 | - | 5.6s |
| 8 | 123s | 14.0s |

### 排查过程
- WER 拿到崩溃签名（ucrtbased.dll + 0x80000003 = Debug 断言断点）
- 无头复现工具发现单线程通过、并行卡死 → 定位 BarSeries O(n²) 拷贝
- gtest 全崩但独立 exe 通过 → 二分测试文件定位为**陈旧对象 ABI 不匹配**（bar.h 变更后部分测试对象未重编），强制重编后 135 全绿
- 新增回归测试 `ConcurrentEnginesPortfolioSafe`（4 线程并发引擎 + 高频 getPortfolio）
- 测试 135 → 136

## 2026-08-03 — P7 UI 量化面板完成

### 背景
P6 完成（117 tests）后搭建量化面板体系。探索确认：StockScreener(11因子)/PaperTradeEngine/BacktestEngine 已就绪，`optimizer/`、`analyzer/` 目录为空，`screener_panel` 是空桩。发现 **Performance 的 winRate/profitFactor/totalTrades/winningTrades/totalPnl 字段从未被填充**（性能展示和优化目标函数都依赖它）。

### 已完成
- [x] **Engine 修复与新增**
  - 修复 `PerformanceCalculator::computeTradeStats`（FIFO 逐股配对，买成本=amount+totalFee、卖回款=amount-totalFee）→ BacktestEngine::run 填充 winRate/profitFactor/totalTrades/winningTrades/totalPnl
  - `GridSearchOptimizer`（engine/optimizer/）：参数笛卡尔积 + std::async 并行回测（共享只读 DataCache 不重复 IO）+ 5 目标函数（总收益/夏普/最大回撤/卡玛/盈亏比）排序
  - `StrategyComparator`（engine/analyzer/）：多策略同数据范围同时回测，按总收益降序
  - `StressTest`（engine/analyzer/）：5 预设极端窗口（2015股灾/2016熔断/2018熊市/2020疫情/2024微盘股）+ 全期基线对比
  - `MonteCarlo`（engine/analyzer/）：日收益有放回重采样 N 次 → P5/P50/P95 + 亏损概率
  - 修复 `PaperTradeEngine`：onQuote 现在累积 per-code 历史并注入 ctx.history（此前趋势策略因 `!ctx.history` 永不交易）；新增 `seedHistory` 播种历史；修复 `placeOrder` 忽略 amount 导致 `buyByAmount` 不成交
- [x] **UI 面板**（一个"量化"QDockWidget 内嵌 5 标签页，tabify 到右侧回测 dock）
  - **选股面板**：11 因子勾选 + 精选129池 + topN/回看/截止 → StockScreener → 结果表（排名/代码/名称/总分/因子明细），双击开图
  - **模拟交易面板**：单股票 + 策略参数 + 资金/滑点 → QTimer 3s 轮询 batchQuote 驱动 PaperTradeEngine → 现金/市值/总资产/总盈亏/持仓 + 成交表 + 日志
  - **参数优化面板**：两参数 from/to/step + 目标函数 → GridSearch → 结果表按目标排序，单击行应用参数到回测面板
  - **策略对比面板**：6 策略预设同时回测 + 多序列净值叠加（EquityCurveWidget 扩展）+ 蒙特卡洛置信区间
  - **压力测试面板**：5 压力场景 + 基线对比 + 当前窗口净值曲线/指标（最大回撤红色高亮）
- [x] **UI 基础**：EquityCurveWidget 多序列（setSeries + setData 兼容）、ScreenResultModel/GridSearchTableModel/ComparisonTableModel
- [x] **MainWindow**：quantDock(5 tab) + 选股双击开图 + 优化点行应用到回测

### 测试
- **Total: 135 tests, 0 failed**（+18）
  - test_performance +4: computeTradeStats 单赢/赢亏/空/FIFO部分
  - test_backtest_engine +1: TradeStatsFilled（买入卖出→交易统计填充）
  - test_grid_search +6: 组合生成/排序/目标映射/缓存复用/并行确定性
  - test_analyzer +6: StressTest窗口+基线/Comparator降序/MonteCarlo恒定+分布+空
  - test_paper_trade +1: TrendStrategyTradesWithSeededHistory（播种历史+金叉买入）

### 排查记录
- **PaperTradeEngine 趋势策略永不交易**：onQuote 未设置 ctx.history，MACross/Turtle 首行 `!ctx.history` return → 累积历史 + seedHistory
- **buyByAmount 不成交**：placeOrder lambda 忽略 amount 参数 → 按 lastPrice_ 换算股数
- **test_engine.exe 0xc0000135**（DLL 找不到）：构建 shell 缺 Qt bin PATH（build.bat 有 `set PATH=D:\Qt\...\bin`）
- **C4099 预置警告**：backtest_panel.h `struct StockCode;` 前向声明与定义 `class StockCode` 冲突 → 改 class
- **C4996 预置警告**：main_window.cpp QMenu::addAction 弃用重载 → 换 text,shortcut,obj,slot 顺序

## 2026-08-03 — P6 UI 图表核心完成

### 背景
P5 完成（103 tests）后搭建完整图表体系。实测锁定腾讯三个数据接口格式（日/周/月 fqkline、分钟 mkline、分时 minute/query），发现 getBars 周/月是坏的（返回日线）。

### 已完成
- [x] **Data 层**
  - indicators 自研（foundation/utils，纯函数可单测）：sma/ema/macd/boll/rsi（Wilder）
  - `parseMinuteTime` 解析 "yyyyMMddHHmm"
  - K线通用化：`fetchKlineBars`/`parseFqKline`（日/周/月参数化 + qfq{day|week|month} 键），修复周/月返回日线 bug
  - 分钟 K 线：`fetchMinuteBars`/`parseMinuteKline`（m5/m15/m30/m60，域 ifzq）
  - 分时：`getIntraday`/`parseIntraday`（IntradayData{date,preClose,points}，量手→股）
  - `Quote::turnover` 换手率（时间戳锚定 t+8）
  - getBars 分发: 日/周/月/季/年 + 分钟周期；start==epoch 拉最近 640 根
- [x] **KLineChart**（QPainter 自绘核心）
  - 布局: 主图55% + 量15% + MACD15% + RSI15% + 时间轴
  - 蜡烛红涨绿跌 + 量柱 + MA5/10/20/60 + BOLL + MACD(DIF/DEA/柱) + RSI(6/12/24) 副图
  - 十字光标 + OHLC 浮框（时间/高低开收/涨跌幅/量/MA）、滚轮缩放（20-800 锚定光标）、左键拖拽平移
  - 异步加载 loadGen_ 竞态守卫；周期切换（分时/日/周/月）
- [x] **TimelineChart 分时图**：240 分钟 X 轴（09:30-11:30/13:00-15:00 午休虚线）、昨收线、价格线+均价线（cumAmt/cumVol）、量柱红涨绿跌
- [x] **CentralChartWidget**：分时↔K线 QStackedWidget + 周期栏；MainWindow 中央区 welcome↔图表，搜索/指数/榜单双击→开图
- [x] **BacktestPanel**：选股(精选129多选)/策略/参数/日期/资金 → IO池拉数→Worker池回测 → 绩效指标网格(12项红涨绿跌)+净值曲线+成交明细表
- [x] **StrategyPanel**：策略模板库(双均线/海龟)+参数编辑 → 应用到回测
- [x] **MarketPanel**：涨幅榜/跌幅榜/市场宽度（batchQuote 实时 129 只，QTimer 10s 刷新，双击开图）

### 测试
- **Total: 117 tests, 0 failed**（+14: indicators 9 + tencent 解析 5）
  - test_foundation +9: SMA/EMA/MACD/RSI(全涨100全跌0/Wilder经典)/BOLL/parseMinuteTime
  - test_data +5: parseFqKline 周/月键、parseMinuteKline 时间列序手→股、parseIntraday 量额换算+qt昨收、turnover

### 实测验证
- `kline_test`：日线640/周线640(跨12年)/月线299(跨25年)/5分钟320/60分钟320/分时267点，周月聚合正确
- `chart_render`：离屏渲染 K线图+分时图为 PNG + 像素分析（红蜡烛/绿蜡烛/MA黄线/均价橙线/价格蓝线全部命中）
- 应用运行 10s 无崩溃，市场面板实时刷新正常

### 排查记录
- **Bar 无 preClose**：涨跌幅改用前一根收盘计算
- **ema 前导 NaN**（MACD dea=ema(dif)）：定位首个连续有限窗口作种子
- **parseIntraday date 嵌套**：date 在 stock["data"]["date"] 非 stock["date"]
- **QPointF 花括号窄化**：MSVC /permissive- 拒绝 int→qreal
- **QToolButton::setFlat 移除**（Qt6）→ setAutoRaise
- **周/月线修复验证**：周线跨12年、月线跨25年（非日线）

### 下一步 → P7 UI 量化面板
选股面板、模拟交易面板、参数优化面板、策略对比面板、压力测试面板

## 2026-08-03 — P5 UI 框架完成

### 背景
P4 完成后（87 tests 零失败）搭建 Qt UI 框架。探索确认 UI 层 13 个类全为空桩，且发现 **4 个必须前置修复的底层缺陷**：`LogManager::logMessage` 从未 emit、st_ui AUTOMOC 陈旧（12 类无 moc）、腾讯行情返回 GBK 编码、`toPinyinInitials` 对中文是占位符。

### 已完成
- [x] **Data 层增强（P5 前置）**
  - `fetch()` 改 thread_local QNetworkAccessManager → 任意线程安全调用（IO 池加载股票列表不卡 10s）
  - `parseQuoteRecord` 完整解析行情（实测字段索引 + GBK→UTF-8 转码 + 名称规范化去全角/空格）
  - **时间戳锚定解析**：股票/指数字段布局不同（股票时间@30，指数@32），用 14 位时间戳字段定位后相对取值
  - `batchQuote` / `parseQuotes` / `parseQuoteName` / `toTencentCode` 公共接口
  - `curated_stocks.h` 内置精选股票池 **129 只**（沪66+深63，代码/名称/拼音硬编码），腾讯实时名称覆盖（离线回退）
    - 已用 live 数据校验：0 错误代码；更新 601211 国泰海通（2025 国泰君安+海通合并）
  - **QuotePoller** 实时轮询：主线程异步 QNAM + QTimer（5s）+ `pollInFlight_` 防堆积 + 注入式测试
  - `subscribeQuote`/`unsubscribeQuote`/`refreshQuotes` 真实实现（原为空）
- [x] **UI 类名重命名**（9 类统一 PascalCase）+ 全 .cpp 加 `#include "moc_X.cpp"`（moc 1→26 个）
- [x] **主题系统**：ThemeManager + dark/light.qss（.qrc 内嵌）+ ConfigManager 持久化 `ui.theme`
- [x] **日志面板**：LogManager `log()` 补 emit `logMessage`（fmt::format + 主线程 Queued 投递）；LogPanel 级别过滤/自动滚动/清空/有界缓冲
- [x] **顶部指数条**：上证/深证/创业板/科创50 实时行情，红涨绿跌，可点击
- [x] **全局搜索栏**：代码/名称/拼音首字母搜索，自定义 QListWidget 弹层 + 防抖 + 键盘导航，IO 池异步加载 129 只
- [x] **快捷键系统**：ShortcutManager + ConfigManager 持久化映射（Ctrl+Space/Ctrl+L/F5/Ctrl+,）+ 偏好设置对话框
- [x] **MainWindow 组装**：QDockWidget 布局（左市场/右策略+回测/底日志）+ 菜单 + 工具栏 + 状态栏 + QSettings 窗口/布局持久化 + 主题切换

### 测试
- **Total: 103 tests, 0 failed**（+16 新测试）
  - test_data +13: TencentQuoteTest 8（字段解析/GBK/规范化/批量/指数市场前缀）、CuratedStocksTest 2、QuotePollerTest 3
  - test_core +3: LogManagerTest（logMessage 信号投递）

### 实测验证
- `curated_check`：129 只精选股票池对比腾讯实时名称，0 错误代码
- `quote_test`：QuotePoller 真实异步链路，4 大指数全部正确（上证 3832.26 +0.72% / 深成 13578.9 +2.21% / 创业 3343.96 +3.06% / 科创 1635.96 +2.99%）
- 应用启动 8s 无崩溃；`%APPDATA%\StockTerminal\config\default.json` 生成 `ui.theme`/`ui.shortcuts.*`；ini 持久化窗口几何+dock 布局

### 排查记录
- **QToolButton::setFlat 移除**（Qt6）→ 改用 setAutoRaise(true)
- **`class QWidget;` 误放 namespace st 内** → 遮蔽全局 ::QWidget，导致基类解析错乱 → 移到全局作用域
- **指数/股票字段布局不同**：绝对索引硬编码解析指数会错 → 时间戳锚定相对取值
- **指数市场识别错误**：parseQuoteBatch 用代码自动检测市场，sh000001 被误判 SZ → 从 `v_sh000001` 前缀解析市场
- **start() 立即刷新只拉到 1 只**：订阅逐个调用 → singleShot(0) 延迟到本轮订阅完成

### 下一步 → P6 UI 图表核心
K线图（9 层渲染系统）+ 分时图 + 回测面板 + 策略面板 + 市场全景面板

## 2026-08-02 — P4 验证闭环 + 腾讯数据源完成

### 背景
P4 目标用真实 A 股数据验证回测闭环。发现**东财数据源（AKShare）在此机器网络不稳定**（DNS 返回不可达 IPv6，VPN 干扰）。经实测腾讯行情接口稳定（连续 3 次 200）。

### 已完成
- [x] **TencentProvider** — 腾讯数据源（主用，稳定）
  - 日K线（前复权）：web.ifzq.gtimg.cn 接口
  - 实时行情/批量行情：qt.gtimg.cn
  - 独立 QNetworkAccessManager + NoProxy（不影响全局 VPN）
  - 自动重试（3次）+ 超时 10s
  - 腾讯 K线格式解析：`[日期,开,收,高,低,量]`（与东财字段顺序不同）
- [x] **AKShareProvider 保留**（东财，备选）
- [x] **策略模板库**（engine/strategy/templates/）
  - ma_cross_strategy: 双均线（MA5/20 金叉死叉）
  - turtle_strategy: 海龟（唐奇安通道突破）
- [x] **端到端回测验证**（真实茅台数据 484 根 2023-2024）

### 端到端验证结果
| 策略 | 总收益 | 最大回撤 | 夏普 | 交易数 |
|------|--------|---------|------|--------|
| 双均线 MA5/20 | -15.8% | 33.9% | -0.31 | 34 |
| 海龟策略 | **+45.1%** | 15.3% | 0.88 | 57 |

回测引擎验证通过：数据拉取 → 缓存 → 策略驱动 → 撮合 → 绩效计算全链路正确。

### 排查记录
- **东财数据源不稳定**：DNS 返回 IPv6 不可达，curl/httplib 时通时不通 → 改腾讯数据源
- **腾讯 K线价格为字符串**：nlohmann 解析需转 double
- **BacktestEngine ctx.history 未设置**：策略无法计算指标 → 修复注入历史序列
- **前视偏差**：history 需为"到当前为止"的序列，不能含未来 bar → 累计注入
- **BarSeries::lookback(0) 越界**：MA 策略 sma 用 lookback(0)，修复为 lookback(1..n)
- **trade.time 为 1970**：下单时间未设置 → currentTime_ 追踪

### 下一步 → P5 UI 框架
主窗口 QDockWidget 布局 + 全局搜索 + 顶部指数条 + 日志面板

## 2026-08-02 — P3 Engine 扩展引擎完成

### 已完成
- [x] **TA-Lib 接入** — vcpkg 安装 talib 0.7.1（一次通过）
  - CMake 链接 `talib::talib`，头文件 `ta-lib/ta_libc.h`
- [x] **StockScreener 选股引擎**
  - IFactor 因子抽象接口（name/category/calculate/toScore）
  - 核心因子集 11 个：ROC/RSI/MACD/波动率/ATR/最大回撤/均线排列/ADX/量比/换手/OBV
  - ConditionFilter 条件筛选
  - Ranker 加权排名（总分归一化）
  - StockScreener 主类：加载日线 → 算因子 → 筛选 → 排名
- [x] **MarketEngine 基础版**
  - MarketScanner 涨幅榜/跌幅榜/换手率
  - MarketBreadth 市场宽度（涨跌家数/ADL/新高新低）
  - MarketEngine 聚合主类
- [x] **Fundamental 预留**
  - FinancialReport / CompanyProfile 数据结构
  - IFundamentalProvider 抽象接口（独立于行情）

### 测试
- **Total: 87 tests, 0 failed**（+19 新测试）
  - FactorTest: 6 tests（指标正确性/数据不足/分数映射）
  - ScreenerTest: 6 tests（排名/筛选/选股流程）
  - MarketScannerTest: 4 tests（涨跌幅/排序/TopN）
  - MarketBreadthTest: 2 tests

### 排查记录
- **BarSeries::lookback bug**：`lookback(1)` 返回当前 bar 而非前一根。修复为 `bars_[size - n - 1]`（n=1 前一根）。

### 下一步 → P4 验证闭环
双均线+海龟策略，回测跑通 + 模拟交易验证

## 2026-08-02 — P2 Engine 核心引擎完成

### 已完成
- [x] **Strategy 基类** — IStrategy 完整接口
  - 生命周期: initialize/onStart/onStop
  - 核心入口: onBar, 可选 onTrade
  - 下单 API: buy/sell/buyByAmount/sellAll (引擎注入交易回调)
  - 查询 API: portfolio/currentCode
- [x] **FeeCalculator** — 全参数可配置费用计算
  - 佣金(含最低)、印花税、过户费、经手费、证管费、自定义规则
  - 内置模板: A股/低佣金/ETF/港股
  - FeeBreakdown 费用明细
- [x] **OrderMatcher** — 订单撮合器
  - 市价单/限价单, 资金/持仓限制
  - 部分成交/全部成交/未触发
- [x] **Performance** — 绩效统计器
  - 核心: 总收益/年化/最大回撤/夏普/胜率
  - 进阶: 卡玛/波动率/索提诺/Alpha/Beta
- [x] **BacktestEngine** — 事件驱动回测引擎
  - 全局时间轴对齐 → 逐Bar驱动策略 → 下一Bar开盘价撮合
  - 内部 Account 管理资金/持仓/费用
  - 净值曲线快照 → 绩效计算
- [x] **PaperTradeEngine** — 模拟交易引擎
  - 实时行情驱动, 即时成交+滑点
  - 与回测共用 IStrategy 接口

### 测试
- **Total: 68 tests, 0 failed**（+18 新测试）
  - FeeCalculatorTest: 7 tests
  - PerformanceTest: 6 tests
  - BacktestEngineTest: 2 tests (买入持有上涨+无数据报错)
  - PaperTradeEngineTest: 3 tests

### 排查记录
- **空指针崩溃**：StrategyContext.portfolio 未设置导致策略解引用空指针（SEH）
- **getCurrentCode 静态空码**：下单 code 为空导致撮合不匹配，改由 currentCode_ 成员追踪
- **手续费断言**：测试预期未含全部费用项，修正为完整费用

### 下一步 → P3 Engine 扩展引擎
StockScreener + MarketEngine + Fundamental

## 2026-08-02 — 安全设计：敏感凭证保护

### 背景
软件将开源公开 + 纯本地单机分享给他人使用，需保护数据源 token、券商凭证、AI key、数据库口令。

### 已完成
- [x] **CredentialStore** — Windows DPAPI 加密凭证存储
  - CryptProtectData/CryptUnprotectData 加密整个 secret JSON → Base64 → 落盘
  - 绑定当前用户，跨用户/磁盘窃取无法解密
  - 非 Windows 预留明文占位（跨平台）
- [x] **AppPaths** — 用户目录隔离
  - %APPDATA%\StockTerminal\{config,data,secrets,logs}
  - 程序目录保持只读，多用户凭证隔离
- [x] **docs/security.md** — 威胁模型、零遥测声明、token 轮换实践
- [x] ConfigManager 保持非敏感配置，敏感字段走 CredentialStore

### 测试
- **Total: 50 tests, 0 failed**（+11 新测试）
  - CredentialStoreTest: 7 tests（含密文检查）
  - AppPathsTest: 4 tests

### 关键决策
- **DPAPI 而非主密码**：零打扰、绑定用户、无需自存密钥；对木马场景靠 token 轮换补足
- **Base64 存储**：DPAPI 输出二进制，JSON 要求 UTF-8，先 Base64 再存

### 下一步 → P2 Engine 核心引擎
Strategy 基类 + BacktestEngine + PaperTradeEngine + FeeCalculator + Performance

## 2026-08-02 — P1 Data 层完成

### 已完成
- [x] Foundation 补充 `StockInfo` 类型（股票基本信息）
- [x] `IDataProvider` 抽象接口完善（connect/getStockInfo/getStockList/getBars/订阅行情）
- [x] **DataRepository** — SQLite 存储 (Qt6::Sql)
  - stocks 表：股票信息
  - daily_bars 表：日线 K 线（OHLCV + turnover）
  - minute_bars 表：分钟 K 线
  - data_sync_log 表：同步状态
- [x] **AKShareProvider** — 东财 HTTP API 接入 (cpp-httplib)
  - 股票列表、日线 K 线（JSON 解析）
- [x] **DataCache** — 内存缓存（code+period 分组，线程安全）
- [x] **StockSearchIndex** — 股票搜索索引（代码/名称/拼音首字母）
- [x] Foundation 补充 `bar.cpp`（BarSeries 实现）

### 测试结果
- **Total: 39 tests, 0 failed**（+14 新测试）
  - DataRepositoryTest: 4 tests（schema/股票保存加载/K线保存加载/同步日志）
  - DataCacheTest: 4 tests
  - StockSearchIndexTest: 6 tests

### 排查记录
- **Qt SQL 崩溃根因**：test_data 的 main 缺少 `QCoreApplication`，导致 SQL 驱动插件加载崩溃（SEH 0xc0000005）。加入 QCoreApplication 后修复。
- **StockCode 解析 bug**：loadStockInfos 用完整 fullCode（SH600519）构造 code，改用 StockCode(fullCode) 解析构造函数修复。

### 下一步 → P2 Engine 核心引擎
Strategy 基类 + BacktestEngine + PaperTradeEngine + FeeCalculator + Performance
- [x] 需求讨论：综合性股票交易工作站，30+功能
- [x] 架构设计：分层服务架构（7层），UI→Intelligence→Engine→Core→Data→Foundation
- [x] 技术栈确定：C++17 + Qt 6.5+ + CMake + Ninja + MSVC
- [x] 功能清单：P0-P10 分阶段实施计划
- [x] 项目文档结构搭建 (docs/) + CLAUDE.md

## 2026-08-02 — P0 项目骨架搭建完成

### 已完成
- [x] Foundation 层完整实现
  - [x] enums.h: Market, Direction, BarPeriod, OrderType, OrderStatus 等10+枚举
  - [x] types.h: Price, Volume, Amount, DateTime 等核心类型别名
  - [x] stock_code.h/cpp: 市场自动检测（SH600519/600519 两种格式）
  - [x] bar.h: OHLCV K线 + BarSeries（历史序列+技术指标辅助）
  - [x] tick.h: Tick、MarketDepth、Quote 实时行情结构
  - [x] order.h: Order/Trade/OrderUpdate 订单成交体系
  - [x] portfolio.h: Position/Portfolio 持仓管理
  - [x] utils/datetime.h/cpp: 日期解析、交易日计算、周末检测
  - [x] utils/string_utils.h/cpp: trim/split/toLower/startsWith/endsWith/拼音
  - [x] utils/json_utils.h/cpp: JSON 文件读写 + 嵌套路径读写
- [x] CMake 构建系统
  - [x] FetchContent: nlohmann-json, spdlog, cpp-httplib, GoogleTest
  - [x] Qt 可选开关 (BUILD_WITH_QT=OFF/ON)
  - [x] CMakePresets.json: default / with-qt / release
  - [x] build.bat: 一键构建+测试脚本
- [x] MSVC 编译环境验证通过（MSVC 14.51 + Ninja + Windows SDK 10）
- [x] 其他层桩文件 (stubs) 创建
- [x] 示例策略: ma_cross / momentum / grid_trading

### 测试结果
- [x] Foundation 层单元测试：**18 tests passed, 0 failed**
  - StockCodeTest: 8 tests
  - DateTimeTest: 3 tests
  - StringUtilsTest: 5 tests
  - JsonUtilsTest: 2 tests

## 2026-08-02 — P0 完成：Core 层实现

### 已完成
- [x] Qt 6.11.1 安装 + 集成（MSVC 2022 64-bit @ D:\Qt\6.11.1\msvc2022_64）
- [x] CMake 集成 Qt：AUTOMOC、BUILD_WITH_QT 开关
- [x] Core 层完整实现：
  - [x] **EventBus** — Qt 信号槽 + lambda 双模式，线程安全，跨模块通信
  - [x] **ThreadPool** — QThreadPool 封装，worker pool (CPU) + IO pool 分离
  - [x] **ConfigManager** — JSON 配置管理，支持嵌套路径 "a.b.c"
  - [x] **LogManager** — spdlog 封装，console + file dual sinks
  - [x] **NotificationService** — 统一通知通道，EventBus 集成，历史记录
- [x] Core 层单元测试：7 tests passed
- [x] Foundation 层单元测试：18 tests passed

### 测试汇总
- **Total: 25 tests, 0 failed**

### 代码统计 (估算)
| 层 | 文件数 | 代码行数 |
|----|--------|---------|
| Foundation | 10 | ~500 |
| Core | 10 | ~600 |
| Data | 8 (stubs) | ~100 |
| Engine | 12 (stubs) | ~150 |
| UI | 12 (stubs) | ~150 |
| Tests | 10 | ~250 |

### 关键决策
- **vcpkg + 系统 Qt**：vcpkg 管理 C++ 库（nlohmann-json/spdlog/cpp-httplib/gtest），Qt 6.11.1 单独安装由 CMAKE_PREFIX_PATH 指定
- **C++17 替代 C++20**：`operator<=>` 不可用，改用手动比较运算符
- **AUTOMOC 修复**：静态库中 Q_OBJECT 需 `#include "moc_xxx.cpp"` 显式触发
- **Qt DLL PATH**：ctest 需 Qt bin 在 PATH 中

### 下一步 → P1 Data 层
DataProvider 抽象接口、AKShare HTTP 接入、SQLite 存储实现

---

## 模板 — 每日记录格式

```
## YYYY-MM-DD — 简短描述

### 已完成
- [x] 事项1
- [x] 事项2

### 待办
- [ ] 待办1
- [ ] 待办2

### 关键决策
- 决策1：原因
- 决策2：原因

### 遇到的问题
- 问题1：解决方案
```
