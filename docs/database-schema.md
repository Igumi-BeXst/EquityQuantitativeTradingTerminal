# SQLite 数据仓储

`DataRepository` 是项目的**行情持久化层**，基于 SQLite（Qt6::Sql 的 `QSQLITE` 驱动）实现。它已接入生产路径：所有历史 K 线、股票列表、板块指数、除权除息事件都经由它落盘。

> **文档状态说明**：本文描述的 schema（11 张表、索引、PRAGMA）来自 `src/data/data_repository.cpp` 的 `createSchema()` 与 `init()`，属于代码事实。
> **运行时行为未经验证** —— 截至 2026-10-21，作者本机未生成过 `market_data.sqlite`（详见文末"验证状态"）。

## 一、接入方式

历史的读取链路已从"直连数据源"改为"数据源 + SQLite 缓存"：

```
UI / 回测 / 选股
      ↓  IDataProvider 接口
CachedDataProvider          ← 装饰器：查缓存 → 拉网络 → 写回
      ↓  惰性转发
TdxProvider / TencentProvider / AKShareProvider …
      ↓
DataRepository  →  %LOCALAPPDATA%\StockTerminal\data\market_data.sqlite
```

装配位置：`src/data/provider_factory.cpp`

```cpp
std::unique_ptr<IDataProvider> makeCachedProvider(const std::string& name) {
    return std::make_unique<CachedDataProvider>(makeDataProviderByName(name), name,
        AppPaths::dataDir() + "/market_data.sqlite");
}
```

- 默认配置 `data.provider = "multi"`，主源 `tdx`、备源 `akshare`
- **每个数据源各自包一层缓存**：`MultiProvider(Cached(tdx), Cached(akshare))`，靠 `source_id` 隔离缓存空间
- 缓存的只写历史与参考数据；`batchQuote` / `getIntraday` / `getMarketDepth` / `getTransactions` 等实时接口**纯转发不缓存**

数据库文件路径不再可配置（`config/default.json` 中的 `data.dbPath` 已移除），固定为 `AppPaths::dataDir()`。

> **目录位置**：缓存与日志放在 **`%LOCALAPPDATA%\StockTerminal`**（Local，不参与域漫游），
> 配置与凭证放在 **`%APPDATA%\StockTerminal`**（Roaming，跟随账户备份）。
> 这样划分是因为行情缓存会增长到几百 MB 甚至 GB 级，放进 Roaming 会让域用户每次登录/注销
> 同步大量可重建数据。详见 [AppPaths 说明](#一接入方式) 与 `src/core/app_paths.h` 的注释。

## 二、连接与 PRAGMA

每个线程持有**独立连接**（`QSqlDatabase` 不可跨线程）：

```cpp
DataRepository& DataRepository::forCurrentThread(const std::string& dbPath);
```

内部用 `thread_local unordered_map<绝对路径, unique_ptr<DataRepository>>`，同一数据库每线程只建一个连接。连接名格式 `stock_<线程ID>_<自增ID>`（自增 ID 防止线程 ID 复用导致撞名）。

初始化时设置三项 PRAGMA：

| PRAGMA | 值 | 目的 |
|---|---|---|
| `busy_timeout` | 5000 | 锁冲突时重试等待 5 秒，避免立刻返回 `SQLITE_BUSY` |
| `journal_mode` | WAL | 读写并发，IO 线程写缓存时不阻塞 UI 线程读 |
| `synchronous` | NORMAL | 缓存可重新下载，断电最多丢最后几条，换取写入速度 |

Schema 版本通过 `PRAGMA user_version = 3` 标记。

## 三、表结构

共 **11 张表**，分两组：

### A. 历史遗留表（legacy，保留兼容）

早期版本的 4 张表，**未被删除**，`saveBars` / `loadBars` / `saveStockInfos` / `loadStockInfos` / `updateSyncLog` / `getLastSyncTime` 仍在用它们，`tests/test_data/test_repository.cpp` 也依赖。

| 表 | 主键 | 说明 |
|---|---|---|
| `stocks` | `code` | 股票基本信息 |
| `daily_bars` | `(code, date)` | 日线 |
| `minute_bars` | `(code, datetime, period)` | 分钟线 |
| `data_sync_log` | `code` | 同步状态 |

**缺陷**：无 `source_id`、无 `price_mode` —— 不同数据源、不同复权口径会互相覆盖。

### B. 新表（`market_*`，当前主用）

解决 legacy 表的口径隔离问题。

#### `market_bars` — K 线主表 ★

```sql
PRIMARY KEY (source_id, code, period, price_mode, datetime)
```

| 列 | 类型 | 说明 |
|---|---|---|
| `source_id` | TEXT | 数据源标识：`tdx` / `tencent` / `akshare` |
| `code` | TEXT | 完整代码，如 `SH600519` |
| `period` | INTEGER | `BarPeriod` 枚举值 |
| `price_mode` | INTEGER | `BarPriceMode`：0=前复权 1=不复权 2=后复权 |
| `datetime` | TEXT | ISO 格式时间字符串 |
| `open/high/low/close` | REAL | 价格 |
| `volume` | INTEGER | 成交量（股） |
| `amount` | REAL | 成交额 |
| `turnover` | REAL | 换手率 |

索引：`idx_market_bars_lookup (source_id, code, period, price_mode, datetime)`

> **待核实**：该索引列与复合主键完全相同。主键已自动建索引，显式索引可能冗余（白占磁盘、拖慢写入）。需用 `EXPLAIN QUERY PLAN` 验证优化器是否真的需要它，不宜直接删除。

#### `market_bar_coverage` — 已缓存区间

```sql
PRIMARY KEY (source_id, code, period, price_mode, start_time, end_time)
```

记录"**哪段时间被成功拉取过**"。不能靠 `market_bars` 有无行来判断覆盖——行情数据本身有空档（周末、停牌），"没行"既可能是"没数据"也可能是"没拉过"。

更新策略为**全删重写**：读出现有区间 → 加入新区间 → 排序 → 合并（1 秒容差）→ 删除该 key 全部旧行 → 插入合并结果。同一 key 的区间数量通常只有 1~3 段。

#### `market_bar_sync_state` — 同步状态

```sql
PRIMARY KEY (source_id, code, period, price_mode)
```

| 列 | 用途 |
|---|---|
| `last_current_success_at` | 上次成功拉到最新数据的时间 → 判断新鲜度 |
| `last_current_attempt_at` | 上次尝试时间（无论成败）→ 实现 60 秒失败冷却 |
| `last_current_attempt_success` | 上次是否成功（INTEGER 0/1，SQLite 无 bool） |

分开记"成功时间"和"尝试时间"，是为了区分两种策略：新鲜度看前者，退避看后者。

#### `market_adjustment_events` + `market_adjustment_sync` — 除权除息

```sql
market_adjustment_events: PRIMARY KEY (source_id, code, ordinal)
market_adjustment_sync:   PRIMARY KEY (source_id, code)
```

`market_adjustment_events` 存除权除息事件明细（除权日、类别、现金分红、配股价、送转比例、配股比例）；`market_adjustment_sync` 存该股票事件快照的 `version` 与 `fetched_at`。

`version` 是事件内容的 **FNV-1a 64 位哈希**（先按多字段排序、以 17 位精度拼接成规范字符串，再哈希），用于判断"事件是否变化"——内容相同则哈希相同，避免无谓重算复权。

#### `market_reference` + `market_reference_sync` — 参考数据

```sql
market_reference:      PRIMARY KEY (source_id, kind, code)
market_reference_sync: PRIMARY KEY (source_id, kind, market)
```

用 `kind` 字段区分数据种类，一张表存多种同构数据：

| `kind` 取值 | 内容 |
|---|---|
| `stock_list` | 全市场股票列表 |
| `stock_info_<code>` | 单只股票信息 |
| `sector_indices` | 板块指数列表 |

## 四、缓存失效策略

`CachedDataProvider` 采用 **stale-while-revalidate**：

| 常量 | 值 | 含义 |
|---|---|---|
| `kFreshness` | 3 天 | 参考数据/最新 K 线的新鲜期 |
| `kOverlap` | 3 天 | 增量拉取时多取的重叠窗口，防边界遗漏 |
| `kRetryCooldown` | 60 秒 | 失败后的重试冷却 |
| `kRecentHistoryWindow` | 640 根 | TDX/腾讯原生历史窗口，超过则视为"全历史"哨兵 |

行为：

1. **命中且新鲜** → 直接返回，不打网络
2. **未命中或过期** → 调 inner 拉取 → 写库 → 更新 `coverage` 与 `sync_state`
3. **拉取失败但有旧数据** → 返回旧数据并 `qWarning` 告警（保证可用性，同时留下可观测痕迹）
4. **失败冷却期内** → 只跳过"当前窗口"请求，历史缺口仍然补拉
5. **复权口径过期**（`price_mode != Raw`）→ 整段重拉（除权事件会使全部历史前复权价失效）；不复权数据只做增量

## 五、迁移状态

- legacy 4 表与 `market_*` 7 表**并存**，属"扩展而非破坏"（Expand, don't break）的渐进迁移
- 旧的 TDX 二进制缓存 `bar_cache\raw\*.daily.stb` 在首次访问时被**一次性迁入 SQLite**（仅 `source_id == "tdx"`，且只补 SQLite 中不存在的日期，不覆盖较新行）
- **缓存目录迁移**：旧版把 `.stb` 放在 `%APPDATA%\StockTerminal\data`（Roaming），现统一到 `%LOCALAPPDATA%\StockTerminal\data`（Local）。启动时 `AppPaths::migrateLegacyData()` 一次性搬运：同卷 `rename`（原子、瞬间），跨卷退化为复制且仅在完全成功后删除原件；`readLegacyRawDaily` 对旧位置保留只读回退。
- 待新路径稳定后，可移除 legacy 表及其测试

## 六、测试覆盖

现有测试仅覆盖 legacy API：

```
DataRepositoryTest.InitCreatesSchema
DataRepositoryTest.SaveAndLoadStockInfos
DataRepositoryTest.SaveAndLoadDailyBars
DataRepositoryTest.SyncLog
```

**未覆盖**：`forCurrentThread`（线程连接池）、`saveCachedBars` / `loadCachedBars`、`addBarCoverage` / `loadBarCoverage`（区间合并）、`missingRanges` 算法、`recordBarSyncAttempt` 冷却逻辑、复权快照存取、参考数据存取，以及 `CachedDataProvider` 整个类。

优先级建议：先补 `missingRanges` 与区间合并的单测（纯算法、零依赖、边界多），再补数据库读写测试。

## 七、已知问题

| 级别 | 问题 |
|---|---|
| 🔴 | **时间戳用本地时间序列化**：`utils::toDateTimeString` 使用 `localtime`，跨时区时时间语义错乱。正确做法是存 UTC、显示时转本地 |
| 🔴 | **新缓存层零测试覆盖**（见第六节） |
| 🟡 | `market_bars` 显式索引与复合主键列重复，可能冗余（需 `EXPLAIN QUERY PLAN` 验证） |
| 🟡 | `INSERT OR REPLACE` 实为 DELETE+INSERT；可改用 `ON CONFLICT(...) DO UPDATE`（SQLite 3.24+）以获得真正原地更新。当前场景功能正确（整行替换、无外键），属可选优化 |
| 🟡 | `data.dbPath` 配置已移除，用户无法将数据库移到其他磁盘 |

## 八、验证状态

**代码层面**：schema 定义、PRAGMA、索引、装配路径均已按源码核对。

**运行时层面**：截至 2026-10-21，本机**尚未生成** `market_data.sqlite`，因此以下均未经实测：

- 建表是否全部成功、`user_version` 是否写入
- 缓存命中/失效/冷却的实际行为
- 区间合并与 `missingRanges` 的边界正确性
- legacy `.stb` 迁移是否按预期执行
- 数据库体积增长速率

**原因**：`build/release-qt/src/StockTerminal.exe`（构建于 2026-10-17 05:01）早于缓存层源码修改时间（同日 06:25），其二进制中不含 `market_data.sqlite` / `CachedDataProvider` 等字符串；含新代码的是 Debug 版 `build/src/StockTerminal.exe`（06:27），但该版本亦无近期运行记录。

**要开始验证**：

```bash
# 重建 Release（日常用版本）
cmake --build --preset release-qt --target StockTerminal

# 或直接用含新代码的 Debug 版
build\src\StockTerminal.exe
```

启动后跑一次回测，检查 `%LOCALAPPDATA%\StockTerminal\data\market_data.sqlite` 是否出现，并用 `PRAGMA user_version;` 与 `SELECT COUNT(*) FROM market_bars;` 确认。

## 相关文档

- [模块 11：SQLite 持久化缓存层](tutorial/模块11-SQLite持久化缓存.md) —— 完整教学（SQL 基础、WAL、事务、索引、区间算法、缓存策略、动手练习）
- [模块 06：数据层与 TCP 协议](tutorial/模块06-数据层与TCP协议.md) —— 数据从网络如何进入系统
- [安全设计](security.md) —— 凭证加密与用户目录隔离
