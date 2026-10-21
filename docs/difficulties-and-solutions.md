# StockTerminal 项目执行难点与解决方法

> 汇总自 `docs/DEVLOG.md`（2061 行开发日志）、源码实测、历史会话记录（8 个会话，其中 2 个主开发会话共 1 万+ 行记录）。
> 每条难点都标注了真实证据位置。

---

## 一、数据源：从"能拿到数据"到"拿对数据"

### 1.1 数据源三次迁移 —— AKShare → 腾讯 → 通达信直连

| 阶段 | 方案 | 结果 |
|---|---|---|
| P1 | AKShare | 依赖第三方 Python 生态，东财 DNS 返回不可达 IPv6，VPN 干扰 |
| P4 | 腾讯行情 HTTP | 可用但字段少、无盘口/五档/逐笔 |
| P9 | **通达信主站直连 TCP :7709** | 拿到报价/盘口/逐笔/K线/除权全量能力 |

**难点**：这是私有二进制协议，无官方文档。
**方法**：参考 `injoyai/tdx`(Go) + `pytdx`(Python)，用探针工具（`tools_tdx_diag` / `tools_tdx_live`）实连抓包逐字节校准，**把每个字节偏移写进 `docs/tdx-protocol.md`**（171 行协议笔记）。
**关键**：`TdxTransport` 虚接口 + `FakeTdxTransport` 注入，让协议层可脱离网络做单元测试。

### 1.2 协议逆向的四个致命细节

#### ① 报价记录必须完整消费（批量请求只解析对 1/6）

```
旧实现只读前 11 个字段（到 amount）就跳下一条
→ 多代码批量请求记录错位，6 只只解析对 1 只
```
单只请求时前几个字段恰好正确，所以**长期未暴露**；直到市场面板做 129 只分块批量请求才炸。
**修复**：按 pytdx 完整字段序列消费整条记录（含 `s_vol/b_vol`、五档 ×20、`rev4(2B)`、`rev9+active2(4B)`）。
**防复发**：落地 fixture `quote_6codes.bin` + `DecodeQuoteBatchFromFixture` 回归锁定 6 只价格。

#### ② 指数 K 线比个股多 4 字节

上证指数 K 线第二根起日期变成 1899 年垃圾 —— 指数记录多了「涨跌家数」4 字节。
**修复**：`decodeKline(payload, cat, isIndex)` + `isIndexCode`（SH `000xxx` / SZ `399xxx`），fixture `kline_000001_day.bin` 锁定。

#### ③ 五档价格是"相对现价的差分"

实连验证：茅台现价 1306.45 → 买1=1306.45、卖1=1306.46。
**修复**：绝对价 = `priceFen + diff` 再 ÷100。

#### ④ 0x051D 当日分时 = 服务器非标准变体，**逆向失败后换路**

这是全项目最硬的一块骨头：
- 响应头里嵌了 `market+code`（`[4]=01 [5:11]=600519`），而 pytdx/injoyai 的标准解析器记录都从 `[4]` 起
- **对照组实验**：pytdx 发完全相同的请求字节，收到完全相同的 1268B 响应，官方解析器产出同样垃圾（0.01 → 2623.17）→ 证明是**服务器变体**，不是我们解析错了
- 穷举 2/3/4 字段 × 偏移 {4,6,8,13,14} × 累积/绝对 × 正反序 + 240 条滑动窗口 → 全部失败

**最终方案（换路，不是硬啃）**：改用 **0x0FC5 逐笔成交明细分页聚合** —— 1000 笔/页，全日记 ~5 页，按分钟聚合 OHLC + 量额，构建 240 分钟轴。
**代价**：请求数从 1 次变成 5 次；**收益**：数据 100% 正确，且顺带拿到了逐笔明细能力。

### 1.3 单位陷阱（协议原始单位 ≠ 显示单位）

| 数据 | 原始单位 | 转换 |
|---|---|---|
| K线价格 | 厘 | ÷1000 |
| K线量（个股） | 手 | ×100 |
| K线量（**指数**） | **万股** | ×10000 |
| 五档价 | 相对现价差分（分） | `priceFen+diff` 再 ÷100 |
| 指数逐笔 volume | **成交额/100（不是量！）** | 按分钟累计成交额，再用日K量额比折算 |

**最典型的一例**：用户对比参照软件发现分时首根量柱 1000.95 万 vs 10.01 万，**差 100 倍**。
诊断实证：0x0FC5 逐笔量原始单位是**手**（全日记合计 37475 手 ≈ 报价 37450 手），数据层 ×100 转股是对的 —— **根因在显示层把量当「万股」显示，而 A 股惯例是「手」**。
**修复**：数据层统一换算成「股」，显示层 `formatVolume()` 统一转「手」并自动缩放（亿手/万手/手）。

### 1.4 数据源被 IP 级封锁

**症状**：板块行情一直拉不到，面板空态。
**排查（curl 独立取证）**：
- 东财 `clist` 全部主机 `Connection closed`，**连 curl 都 HTTP 000 空回复**
- 同主机其他接口 `ulist.np`（实时行情）、`push2his`（K线）→ **HTTP 200 正常**
- 参数/Referer/ut 时间戳/子域/HTTPS 全试遍 → 全 000

**结论**：东财 CDN/WAF 对**本机 IP** 封锁了 clist 整条路径（早前压测触发），非代码 bug、非普通限流。
**修复（工程化容灾）**：
1. 新浪板块行情兜底（行业 49 / 概念 175 条真实数据）
2. `g_eastMoneyStrikes` 原子计数：连续 2 次全败 → **进程内直接降级新浪**，不再浪费时间重试
3. 进程重启自动复测东财，封锁解除即回切富数据源
4. 已知取舍：新浪行业仅 49 个（东财 86+）、无换手率/涨跌平家数（详情页已隐藏该段）

### 1.5 多源回退：整体替换，**绝不拼接**

**设计原则**（`multi_provider.h`）：主源优先，空/失败**整体**回退备源。
**为什么不拼接**：同一逻辑查询在不同源之间价量口径不同 —— 实测 600667 当日外盘各源差 27%（206.6万~263.3万），拼接会在同一序列里混入两套口径，回测/图表出现不可解释跳变。
**能力差异的优雅降级**：`IDataProvider` 给可选接口默认实现（`getMarketDepth→nullopt`、`getQuoteFundamentals→nullopt`、`getTransactions→{}`），不支持的源无需改代码，UI 显示「—」而不是错误的 0。

### 1.6 数据正确性的"非 bug"判定

用户对比同花顺发现外盘/内盘不一致（茅台 我们 1.78/2.49 vs 同花顺 1.55/2.72）。
**没有直接改代码**，而是先做完整验证：
- 五档价格合理（记录对齐对）
- **外盘 + 内盘 = 总成交量（精确）**
- 方向归属验证：茅台 `s_vol/b_vol` 与全天逐笔 `buyorsell` 求和吻合（外盘=主动买）
- 600667 当日外部各源外盘 206.6万~263.3万（差 27%），同花顺主界面口径最偏内盘、新浪最偏外盘，**各源互不一致**

**结论**：我们用的是通达信服务器 `s_vol/b_vol`（通达信口径），与通达信客户端完全一致 → **是行情软件统计口径差异，不是本软件缺陷**。
为此新增 `TdxProvider::getDayTransactions()`（0x0FC5 分页拉全当日逐笔）与 `tools_depth_probe` 聚焦诊断工具，把结论固化。

> **这是贯穿项目的方法论**：用户报"数据不对"时，先把口径验证做完再决定改不改 —— 避免"把对的改成错的"。

---

## 二、回测准确性：与聚宽逐笔对齐的漫长收敛

### 2.1 最刺眼的一次：433% vs 21%

同样单票（600667 太极实业）、同样参数、同样日期：**StockTerminal 回测 433%，聚宽 21%**。
**根因**：`smaAt()` / `MACrossStrategy::sma()` 用 `lookback(1..period)` 计算均线，**不包含当前 bar** → 信号整体滞后一天。聚宽等平台均线默认含当日收盘。
**修复**：`smaAt()` 改为 `offset=0` 含当前 bar。影响双均线、动量离场、均值回归等**全部** SMA 类策略。

### 2.2 未来函数：当天看到当天 K 线，却在当天开盘成交

第一笔买入与聚宽差 3 天。
**根因**：原回测在当天 `onBar` 中能看到**当天完整 K 线**，却在当天开盘价成交 —— 经典未来函数。
**修复**（对齐聚宽时序）：
1. 策略 `onBar` 时历史**不含今日 bar**（只看到昨日及以前）
2. 信号基于昨日收盘
3. 今日开盘价撮合
4. 收盘后再把今日 bar 追加进历史

影响面：回测/参数优化/优化建议/策略对比/压力测试**全部**走同一引擎，统一受益。

### 2.3 前复权 vs 不复权：信号口径与成交口径必须匹配

聚宽 `use_real_price=True` 的策略**信号基于前复权 K 线，成交/盯市用不复权真实价**。
我们此前全部用前复权价成交 → 第一笔成交价约 7.64（前复权）而非真实价 7.91。
**修复链**：
- 数据层：`IDataProvider` 新增 `getRawBars()`（不复权）/`getHfqBars()`（后复权）
- 配置层：4 个 Config 结构增加 `rawBars` 字段
- 引擎层：信号用前复权历史，撮合用当日不复权真实开盘价，收盘盯市用真实收盘价
- UI 层：5 个量化面板 IO 阶段额外拉取不复权 K 线

### 2.4 warm-up：回测第一天就能看到历史

聚宽在回测前就能拿到历史数据算指标，我们只加载 `startDate~endDate` → 信号起点不同。
**修复**：`kBacktestWarmupCalendarDays = 400`，从 `startDate - 400 天` 开始拉取，回测循环前用缓存预填 `seriesByCode`；**交易/净值仍只从 `startDate` 开始**。

### 2.5 1 跳滑点 + 100 股整手

- 聚宽每笔市价单成交价基本是「开盘价 ± 0.01 元」→ 新增 `slippagePerShare` 配置，5 个面板默认 0.01
- 按金额下单原用 `amount/10` 占位 → 买入 900 股而非 1100 股。**修复**：`Order.targetAmount` 记录目标金额，撮合时用今日开盘价换算股数并**向下取整到 100 股整数倍**，再受可用资金约束

### 2.6 收敛结果

| 阶段 | 总收益 |
|---|---|
| 修复前 | 44~48% |
| SMA + 时序修复后 | ~44% |
| 真实价成交 + warm-up + 滑点后 | **25.99%** |
| 聚宽基准 | 21.26% |

成交明细在 2024-09/10、2024-11、2025-02/03、2025-05 等关键段与聚宽**逐笔数量/价格基本一致**。

### 2.7 收尾时的"假 bug"

用户报「最后一笔交易记录不一致」，追查发现：结束日期设为 `2026-08-18` 时最后一天那笔不出现，设为 `2026-08-19` 才出现且总收益与聚宽一致 —— 是**日期区间语义**（信号在 T 日、成交在 T+1 日）导致的正常表现，不是 bug。
> **经验**：对齐外部平台时，"差一天"类问题先确认是口径差异还是真 bug，比直接改代码省几轮返工。

---

## 三、并发与生命周期：全项目最凶险的坑

### 3.1 关闭时堆损坏（HEAP CORRUPTION）—— 连续四轮排查

**症状**：`HEAP CORRUPTION DETECTED: after Normal block(#12337)`，**非确定性**，有时启动即崩、有时关闭时崩。事件日志 `APPCRASH P7=c0000374`。

#### 根因（层层叠加，每修一层浮出下一层）

| 轮次 | 根因 |
|---|---|
| 第 1 轮 | **ThreadPool 异步 lambda 捕获裸 `this`**：盘口 2.5s / 关键数据 5s / 市场面板 30s 轮询在 IO 线程读 `this->provider_`；用户关窗后 widget 先销毁，IO 线程后执行 → 读已释放内存得到垃圾指针 → 经垃圾指针调方法 → **随机堆写**覆盖相邻堆块哨兵 |
| 第 2 轮 | **`TdxProvider::disconnect()` 末尾把 `stopThreads_` 重置为 false** → 关闭窗口间隙面板定时器触发异步任务 → `ensureConnected()` 派生**不受管理的 detached 线程** → provider 析构后该线程访问已释放的 this |
| 第 3 轮 | **PatternPanel 漏改** —— `startDetect` 的 IO lambda 仍捕获指向成员 `unique_ptr<DataCache>` 的裸 `cache_.get()` → 关量化工作台（形态识别在飞）时写入已释放 cache。这就是「关量化工作台必崩」的直接原因 |
| 第 4 轮 | **QuantWindow 关闭竞态**：面板在飞任务持有 QPointer 守卫 + shared_ptr cache，但关窗瞬间 worker 跨线程读 QPointer（与主线程销毁并发）存在 use-after-free 窗口 |

#### 解决方法

**① 统一「安全异步模式」**（全项目 16+ 处）：
```cpp
// IO 线程按值捕获 provider（不读 this->provider_）+ QPointer 守卫
QPointer<Widget> guard(this);
auto provider = provider_;              // shared_ptr 按值捕获，保活
ThreadPool::submitIO([guard, provider, code] {
    auto data = provider->getBars(code);          // 不碰 this
    QMetaObject::invokeMethod(guard, [guard, data] {
        if (!guard) return;                        // 对象已销毁 → 安全跳过
        guard->updateUi(data);
    });
});
```
- `cache_` 由 `unique_ptr<DataCache>` 改 `shared_ptr<DataCache>`（lambda 按值捕获保活）
- 结果投递用 `QPointer` 守卫，widget 销毁后自动变 null

**② 关闭顺序串行化**（消除竞态窗口）：
```cpp
// QuantWindow::closeEvent —— 先排空在途任务，再销毁面板
ThreadPool::ioPool()->waitForDone();
ThreadPool::workerPool()->waitForDone();
// 然后才销毁面板
```
正常关闭无在途任务时立即返回不卡顿。
叠加：`MainWindow::~MainWindow` 排空 + `provider_->disconnect()` 放在排空**之后**（事件循环已停止，不会提交新任务，无死锁）。

**③ `stopThreads_` 语义修正**：`disconnect()` 不再重置（保持 true 使线程获取 `mutex_` 后立即退出），`connect()` 启动时置 false（保留重连能力），`ensureConnected()`/`refreshQuotes()` 入口加 `if (stopThreads_.load()) return;` 阻止关闭后触发重连。

**④ 用堆压力测试排除法缩小范围**：`tools_heap_stress` 无头跑 15 轮真实 2015-2026 数据（网格/压力/蒙特卡洛/形态识别），每步 `_CrtCheckMemory()` → **引擎全部堆完整** → 排除计算越界写，锁定 UI 层竞态。

### 3.2 线程风暴（量化工作台打开即崩）

**症状**：Release 版点「量化工作台」菜单直接崩溃（`0xC0000374`）。诡异的是 **Debug 不崩**（时序不同）。
**复现**：`tools_quant_repro` 循环构造 QuantWindow —— 「连接未就绪 + 无主线程延迟」稳定崩溃；等连接就绪 / 加任何主线程打点延迟就不崩（**竞态窗口微秒级**）。
**根因**：`TdxProvider::ensureConnected` 在 `state_ != Connecting` 时**每个 executeCommand 都 spawn 一个 detached doConnect 线程**。量化工作台打开 = 8+ 组件（6 个 `StockPoolPicker` + 2 个 `StockSearchBar`）同时 `submitIO` → 连接失败/慢时**线程风暴**：多个 doConnect 并发执行 `transport_ = std::move(transport)` 竞争 → use-after-free / 双重释放。
> 挖出一个信号：`lastConnectAttempt_` 字段（冷却预留）**声明了但从未使用** —— 说明设计时有考虑，实现时漏了。

**修复**：
1. 连接失败路径加 **2s 冷却**（`lastConnectAttempt_` 生效）→ 冷却期内并发 executeCommand 只等待现有尝试，不再风暴式 spawn
2. `StockPoolPicker` / `StockSearchBar` 的 IO 加载任务**先等 provider 连接就绪**（≤15s 轮询）再拉列表
3. 保留 `tools_quant_repro` / `tools_concurrent_list` 复现工具
**验证**：原始崩溃配置 × 20 轮全部通过；release 版启动后 2s 内 Alt+Q 打开量化工作台 × 5 全部存活。

### 3.3 函数级 static 的隐藏竞态

**症状**：参数优化面板崩溃（WER: `ucrtbased.dll` + `0x80000003` = Debug 断言断点）。
**根因**：`BacktestEngine::getPortfolio` 用**函数级 `static Portfolio`** —— GridSearch 并行跑多个 BacktestEngine 时多线程同时写同一个 static → 数据竞争/堆损坏 → 策略遍历 positions 时撞上对方释放。
**为什么回测面板从未触发**：回测面板只跑单引擎。**并行网格搜索一上就炸。**
**修复**：`cachedPortfolio_` 实例成员替代函数级 static（每引擎独立）。
**防复发**：新增回归测试 `ConcurrentEnginesPortfolioSafe`（4 线程并发引擎 + 高频 getPortfolio）。

### 3.4 排查方法论：堆损坏的真正根因可能不是代码

有一轮「关闭时必现堆损坏」，走完整套二分排查后，**根因是增量构建混用新旧目标文件**：
- `git stash` 恢复时，KLineChart 等类**布局不一致（ABI 错位）** → 成员写入越过堆缓冲区
- 非确定性块号（#8014/#8016/#8018）正是源于此
- **全量 `--clean-first` rebuild → 直接修复**

**沉淀的三条工具/经验**：
1. `_CrtSetReportMode(MODE_FILE)` 把 CRT 报告重定向 stderr → **无人值守静默复现**
2. `_CrtCheckMemory()` 加检查点 → 定位损坏发生的**时机**（当时定位到 `new CentralChartWidget` 构造段）
3. **类布局变更后必须 clean rebuild** —— 日志里因此记录过 3 次"假崩溃"（0xc0000409 假崩、ABI 不匹配等）

### 3.5 跨线程 QNAM 析构

**根因**：`StockKeyDataWidget` 的 `fundProvider_`（`shared_ptr<AKShareProvider>`）在 **IO 线程析构** → `~AKShareProvider` → `http_->clearAccessCache()` **跨线程访问主线程 QNAM**。
**修复**：`shared_ptr` 移入主线程回调再释放（IO 线程用完、主线程析构）；`AKShareProvider::disconnect()` 不再跨线程调 QNAM `clearAccessCache`（清缓存非必需，直接跳过）。
**根治**：所有数据源 HTTP 改用 **thread_local QNAM** —— 各线程首次使用时在当前线程创建，解决 QNAM 线程亲和问题：
```cpp
/// 线程本地 QNetworkAccessManager（NoProxy）— 任意线程可安全调用。
/// 基本面接口从 ThreadPool IO 线程调用，不能用主线程亲和的成员 QNAM。
```

---

## 四、性能与内存：全市场规模下的硬账

### 4.1 瓶颈定位：不是计算，是 IO

`tools_mem_bench` / `tools_tdx_market_probe` 实测取证：
- 回测**纯计算** 76s（5213 只 × 876 天 = 440 万 bar）
- 但**在线拉取** 400 天预热 + 3 天原始价 → 约 **2 万次 TDX 往返**，单次 0.4~1.6s → **10+ 分钟**
- 市场面板 30s 刷新一次，而刷新耗时 **68.6s** → 批量报价**近乎常驻占用 TDX 连接**

### 4.2 方案 A：多连接并行下载

**为什么不能简单加线程**：`TdxProvider::executeCommand()` 持 `mutex_` 直到 `recvFrame()` 完成 → 多线程共享同一 provider **完整串行**，加线程无效。
**设计**（`data/parallel_bar_fetcher.{h,cpp}`）：
- 识别 TDX 主源后创建最多 **4 个独立 `TdxProvider` 连接**（各自独立 `transport_`）
- 股票池按 lane **round-robin 分片**（`for (i = lane; i < total; i += n)`）
- 拉取结果统一写入线程安全 `DataCache`（`rawMutex` 串行化）
- 并行连接失败 → 回退主数据源
- 并发数上限 4（避免触发 TDX 服务器限流）+ 保持 `InteractiveGuard` 交互优先级语义

**关键收益（省掉一整轮全市场拉取）**：
从已拉取的完整区间 `[warmStart, end]` 中**同时**收集真实价 `rawBars`（`filterRawBars`），**去掉原来的第二次全市场拉取**。

### 4.3 方案 B：磁盘缓存

**设计**（`data/bar_disk_cache.{h,cpp}`）：
- 按 code 保存不复权日线到 `%APPDATA%/StockTerminal/data/bar_cache/raw/<code>.daily.stb`
- 二进制存储，含 `magic("STB1") / version / period / count` 校验
- 覆盖判定**允许最多 3 个自然日边界误差**（应对周末，避免长期使用陈旧缓存）
- 首次下载后落盘，后续回测直接命中缓存；非 TDX 源**不写磁盘**（避免跨数据源混用）

> 用户当时问过「缓存放 SQLite 会不会更好」 —— 结论是**继续用文件缓存**：DataRepository 的 `saveBars/loadBars` 虽已存在但未接入 UI 流程，而文件缓存的格式/版本校验 + 覆盖判定语义更可控。

### 4.4 内存：1.6GB 的构成与削减

**实测构成**（全市场 5213 只 × 876 天）：
- 拉数据阶段峰值 **830MB** —— DataCache 本身，`Bar≈112B × 440 万 = 数据固有成本`
- 回测阶段 **1630MB**：
  - timeline 每组合**拷贝 440 万 Bar**（~420MB）
  - `equitySnapshots` 900 份**完整 Portfolio 快照**（含持仓向量）
  - 多 lane 并行**翻倍**

**削减**：
1. **timeline 改存 `const Bar*`**（指向 DataCache 的 `shared_ptr<BarSeries>`）→ 消除每组合 440 万 Bar 值拷贝
2. **`BacktestConfig.keepEquitySnapshots`（默认 true）**：网格搜索/回测/对比/压力测试设 false，净值曲线由内部 `equityCurve_` 累积（新增 `Account::netValue()` 零拷贝取净值）—— 核实全项目**无任何消费者读 `equitySnapshots`**
3. **并行度上限 4**：原 `parallelLanes = 全部核数` → 16 lane × 全市场 = 内存 ×16

实测结果：回测 76s / 峰值内存 1628MB（快照开关对比 1630 vs 1643MB 无差异 → **大头是数据本身，已无可省**）。

### 4.5 Debug 下并行反而更慢

实测发现：**Debug CRT 堆全局锁使 8 线程 = 单线程 2.3 倍耗时**。

| lanes | 修复前 | 修复后 |
|---|---|---|
| 1 | 38s | 6.0s |
| 2 | — | 5.6s |
| 8 | **123s** | 14.0s |

**修复**：
- `BarSeries` 重构为内部 `shared_ptr<vector<Bar>>` + `append()` —— 热循环从**每 bar 整段拷贝 O(n²)** 变 **O(1) 追加**
- Debug 下 `parallelLanes=2`，Release 用 `maxThreadCount`

### 4.6 Release 构建：8.4 倍

**背景**：用户反馈「全市场优化进度条 78% 停滞」，诊断结论是**不是卡死，是计算量爆炸 + 进度粒度太粗**。
**新增 `release-qt` preset**（MSVC Release + Qt，`binaryDir build/release-qt`，**复用 `build/vcpkg_installed` 避免重新下载库**，显式 `CMAKE_CXX_COMPILER=cl` 防 PATH 里 mingw64 cmake 误选 GCC）+ `run-release.bat` 一键启动。
**实测基准** `tools_opt_bench.cpp`（300 只 × 12 组合 MACross）：**Debug 454.8s vs Release 54.2s，快 8.4 倍**，最优目标值一致（43.67）。

### 4.7 进度与 ETA：用户体验也是性能问题

三类真实反馈，三种根因：

| 用户描述 | 根因 | 修复 |
|---|---|---|
| 「进度条 78% 不动了」 | GridSearch 只统计**第一个未完成组合**的子进度，并行 lane 的其他组合不计入 → 停滞 + ETA 虚高 | 所有在跑组合**子进度加权平均** + **单调保护**（compare_exchange，只前进不倒退）；组合内接入 BacktestEngine 子进度回调（全市场 ~900 点/组合） |
| 「进度满了 + ETA 0s + 无结果」 | 优化建议是**两阶段任务**（网格 50~100% → 压力测试 90~100%）：网格完成时进度已 100%，压力测试阶段**跳回 90%** 重爬，且 stress 回调**不更新 ETA** | 重新映射两阶段进度（网格 0-90% + 压力 90-100%，全程单调）+ stress 更新 ETA |
| 「预计完成时间一直在增长」 | 同上 + 内存压力 | 见 4.4；运行开始时提前显示「共 N 组合 × M 只（全市场大池预计较久）」 |

新增共享 `ui/utils/progress_eta.h`（已用/剩余时间格式化），5 个面板（优化/建议/回测/对比/压力）接入「已用 X · 预计剩余 Y」实时标签。

> **坦诚的已知限制**：全市场 × 网格搜索绝对耗时仍长（每组合 ~76s × 90 组合 ≈ 1.9 小时，Release 4 lane）—— 这是计算量数学必然。目标不是"消除等待"，而是**让等待可见、可预期、不误判为卡死**。

---

## 五、贯穿项目的方法论

### 5.1 探针工具驱动（tools_* 诊断工具是主力）

项目沉淀了一批无头诊断工具，每个都对应一次真实排查：

| 工具 | 用途 |
|---|---|
| `tools_tdx_diag` / `tools_tdx_live` | 协议实连抓包校准 |
| `tools_depth_probe` | 单票盘口 + 五档 + 全天逐笔方向对照 |
| `tools_tdx_market_probe` | 全 A 股池异常探测（发现停牌股 price=0） |
| `tools_heap_stress` | 15 轮真实数据堆压力测试 + `_CrtCheckMemory` |
| `tools_quant_repro` | QuantWindow 循环构造复现崩溃 |
| `tools_concurrent_list` | 并发 getStockList 压力测试 |
| `tools_mem_bench` | 内存构成实测 |
| `tools_opt_bench` | Debug/Release 性能基准 |
| `tools_stock_list_probe` | 全市场列表耗时/数量实测 |

配合 **fixture 回归**（`tests/fixtures/tdx/*.bin` + `quote_6codes.bin` / `kline_000001_day.bin` / `minute_600519.bin`），把"实连才发现的问题"固化成可重复的单元测试。

### 5.2 用户实测反馈闭环

开发日志几乎每条都以「**待用户 run.bat 复测**」结尾，下一轮的起点往往是用户的一个具体观察：
- 「关量化工作台崩溃」→ 4 轮排查
- 「78% 不动了」→ 并行下载 + 磁盘缓存 + Release 构建 + 进度细化
- 「量柱差 100 倍」→ 单位口径全线梳理
- 「和聚宽差距大」→ 7 轮对齐收敛
- 「一打开就崩溃」→ 线程风暴

> 这些不是"报 bug"，而是**验收标准的来源**。用户看得见的现象，是最可靠的验收信号。

### 5.3 口径先对齐，再写代码

对比外部平台（聚宽）时，先确认**口径**：信号价用复权还是不复权、成交价用哪个、滑点几跳、手续费怎么算、日期区间是闭区间还是信号/成交分离。
**这些不定死就写代码，返工成本极高** —— 项目在聚宽对齐上花了 7 轮（433% → 44% → 26% vs 21%），前几轮主要就是在补口径。

### 5.4 测试与构建纪律

- **473+ 测试全绿**（Foundation 56 / Core 21 / Data 112 / Engine 207 / Intelligence 73+）；最新 Release 构建实测 **476 个用例 0 失败**
- **零警告**（MSVC `/W4`）作为每次修改的硬门槛
- **类布局变更后必须 clean rebuild**（本项目 3 次"假崩溃"的根因）
- Debug 与 Release **双配置都要构建**（Release 会暴露 Debug 掩盖的问题：`GridSearchTest.SortsByObjectiveDescending` 的 `results[i]` 赋值丢失就是 Release 测试先暴露的）

### 5.5 分层架构的实际价值

`UI → Intelligence → Engine → Core → Data → Foundation` 严格自上而下 + 纯虚接口耦合，在实战里的具体作用：
- **依赖方向约束救过一次设计**：`composeSignal` 设计文档写放 `engine/analyzer`，但入参含 `pattern::PatternSignal` / `sentiment::SentimentScore`（Intelligence 类型），而 `st_engine` 不链接 `st_intelligence` → 放 `intelligence/signal/`（intelligence 依赖 engine 方向合法，**避免循环依赖**）
- **同一套策略代码跑两种场景**：`StrategyEngine` 被 `BacktestEngine` 和 `PaperTradeEngine` 共用 → 从根本上保证回测与模拟交易的一致性（这也是论文选题里提出的核心关键问题）
- **数据源可换**：`IDataProvider` 抽象让 AKShare→腾讯→TDX→MultiProvider 的迁移只影响装配层，12 个 UI 类从 `TencentProvider*` 改为 `IDataProvider*` 即完成

---

## 六、残余风险清单（诚实登记，未修复）

来自本次代码实读，供后续跟进：

1. **`ensureGbbq` 失败也缓存空事件** → 一次网络抖动会让该 code **永久返回不复权 bars（静默错价）**（`tdx_provider.cpp:463-467`）
2. **`diskCacheCovers` 3 天容差对上边界同样生效** → 当天/近 3 日 bar 可能不补拉（`parallel_bar_fetcher.cpp:56-59`）；`disk.save` 覆盖式写入可能截短历史
3. **停牌股 `change=-100%` 仍由数据层产出**，仅在 UI 三处各自过滤（`tdx_provider.cpp:713-714`；`market_panel.cpp:257-259` 等）→ 其他 `batchQuote` 消费者仍有风险
4. **`IDataProvider::getRawBars` 默认回退前复权** → 能力缺失的源会**静默**把前复权当不复权返回；`AKShareProvider` 正是如此（URL 固定 `fqt=1`）
5. **`config/default.json` 的 `data.tdx.*` 运行时从未被读取**（TdxProvider 不读 ConfigManager），头注释与实现不符
6. **协议文档偏移错位**（`tdx-protocol.md:27-30` vs `tdx_protocol.cpp:79-81`）与 K线 category 表过时
7. **死代码/死命令**：`kMsgId`、`kResponsePrefix`、`buildMinuteReq`/`decodeMinute`（0x051D）、`Cmd::Login2`、`Cmd::HistoryMinute`、`KlineDay2`
8. **MultiProvider 只判空不判完整性**（TDX 返回部分 chunk 也算成功，不会用备源补齐）；`MultiProvider::connect()` 因 `TdxProvider::connect()` 恒返回 true 而**永不启动备源**（靠备源自身 `fetch()` 内部自连接兜底）
9. **`AKShareProvider` 成员 QNAM 路径与 thread_local 路径并存** → 跨线程风险仍在（`fetch()` 走成员 QNAM，`httpGet()` 走 thread_local）
10. **腾讯 provider 复制了一份 `gbkToUtf8`/`normalizeName`** → 两处逻辑漂移风险
11. **磁盘缓存文件名恒为 `.daily.stb` 而头部才记 period** → 非 Daily 调用会写同名文件、被下次 Daily 读取判为损坏
12. **`tdx_provider.h` 的 `data.tdx.servers` 注释声称读 ConfigManager，实际用 `kDefaultServers` 硬编码** → 用户改了 config 不生效（文档/实现不符）
13. **`InteractiveGuard`（`tdx_provider.h:74-83`）未禁止拷贝/移动** → 一旦被意外拷贝，`interactiveWaiters_` 计数会失衡变负，导致 `yieldToInteractive()` 的谓词 `== 0` 永不成立，批量报价每个 chunk 白等 500ms（全市场 68s 会显著劣化）。修法：补四个 `= delete`
14. **`ThreadPool::workerPool()/ioPool()`（`thread_pool.cpp:9-28`）用非原子裸指针做惰性初始化** → 两个线程同时首次调用会创建两个池（内存泄漏 + 线程数限制失效）。当前靠"首次调用都发生在主线程启动阶段"侥幸安全；建议改用函数内 `static` 或 `std::call_once`（项目内 `EventBus::instance()` 已是正确写法，属风格不一致）
15. **`StrategyContext::history` 注释与实现相反**（`istrategy.h:19` 写"含当前 bar"，但 `backtest_engine.cpp:284-293` 传的是"不含今日 bar"以消除前视偏差）→ 写策略者若信任注释会误用当日 high/low/close，写出未来函数导致回测虚高。修法：同步注释为"截至昨日的累计历史（不含当前 bar，防前视）"
16. **`IStrategy` 生命周期钩子全为纯虚**（`initialize`/`onStart`/`onStop`，而 `onTrade` 有默认空实现）→ 每个策略被迫写三个空函数，与项目自身在 `IDataProvider` 上遵循的原则（"必须实现的用纯虚，可选的给默认实现"）不一致。建议给默认空实现，只保留 `name()` 与 `onBar()` 为纯虚

---

## 附：一页速览（面试/答辩用）

| # | 难点 | 解决 |
|---|---|---|
| 1 | 通达信私有二进制协议无文档 | 实连抓包校准 + 协议笔记 + `TdxTransport` 可注入测试 |
| 2 | 0x051D 分时是服务器非标准变体，穷举失败 | **换路**：0x0FC5 逐笔聚合 |
| 3 | 报价记录必须完整消费，否则批量解析错位 | 全字段消费 + fixture 回归锁定 |
| 4 | 单位口径混乱（厘/手/万股/差分/成交额） | 数据层统一成「股」，显示层统一转「手」 |
| 5 | 东财 clist 被 IP 级封锁 | 新浪兜底 + 进程内熔断 + 重启自动复测 |
| 6 | 回测 433% vs 聚宽 21% | SMA 含当前 bar + 去掉未来函数 + warm-up + 真实价成交 + 1 跳滑点 → 26% |
| 7 | 关闭/打开时堆损坏（非确定性） | 安全异步模式（shared_ptr + QPointer）+ 关闭排空 + 冷却防线程风暴 + clean rebuild |
| 8 | 并行网格搜索崩溃 | 函数级 static Portfolio → 实例成员 |
| 9 | 全市场拉数 10+ 分钟 | 4 连接并行 + 磁盘缓存 + 省掉第二轮拉取 |
| 10 | 回测内存 1.6GB | timeline 零拷贝指针 + 关快照 + lane 上限 4 |
| 11 | Debug 并行反而慢 2.3 倍 | BarSeries O(n²)→O(1)；Debug 限 2 lane |
| 12 | 全市场优化不可用 | Release 构建（8.4 倍）+ 进度加权平均与单调保护 |
