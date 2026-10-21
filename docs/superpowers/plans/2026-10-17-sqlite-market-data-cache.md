# SQLite Market Data Cache Implementation Plan

> **For agentic workers:** Implement this plan task by task in the current workspace. Preserve unrelated existing worktree changes.

**Goal:** Connect the existing SQLite repository to historical market-data reads across providers and pages, with TDX raw-bar and corporate-action based adjustment caching.

**Architecture:** Wrap each concrete provider with a cache-aware `IDataProvider` decorator before composing `MultiProvider`, so fallback responses retain their actual provider identity. Store raw bars, source-adjusted fallback bars, corporate-action snapshots, reference data, coverage ranges, and sync metadata in SQLite. TDX adjusted bars are rebuilt locally from cached raw bars and persisted GBBQ events.

**Tech Stack:** C++17, Qt 6 SQL/SQLite, existing `IDataProvider`, TDX protocol/provider, `AppPaths`.

**Spec:** `docs/superpowers/specs/2026-10-17-unified-sqlite-market-cache-design.md`

## Global Constraints

- Cache Daily, Weekly, Monthly, Quarterly and Yearly bars; bypass Minute1/5/15/30/60, Tick, quotes, depth and transactions.
- Keep the existing 3-calendar-day current-window freshness rule measured from the last successful persisted sync; refresh stale raw tails with overlap, preserve all older rows, and apply a short retry cooldown after failed current-window refreshes.
- Persist source identity and price mode; never mix provider or adjustment-mode rows.
- Refresh TDX adjustment snapshots every 3 calendar days and rebuild adjusted bars locally when their version changes.
- Keep funds-page LHB/margin, fundamental snapshots and news/sentiment out of scope.
- Preserve existing SQLite tables and `.stb` files; `.stb` is a raw daily migration fallback only.
- Keep every Qt SQL connection on its creating thread; do not add or run tests in this change.

## Review Focus

- Fallback provider changes must not reuse bars or corporate actions from the other provider.
- Failed bar/action requests must not advance coverage or replace the last successful snapshot.
- Requests for historical windows must not trigger freshness refreshes beyond their requested end date.
- TDX adjusted bars must be based on exactly the same GBBQ fields and rounding rules as the current implementation.
- SQLite lock/open failures must fall through to the existing provider path without losing the user's cache.

---

### Task 1: Extend the SQLite repository for provider-aware historical data

**Files:**
- Modify: `src/data/data_repository.h`
- Modify: `src/data/data_repository.cpp`

**Interfaces:**
- Add `enum class BarPriceMode { DefaultAdjusted, Raw, Hfq }`.
- Add source-aware `loadBars`, `saveBars`, range-coverage, sync-time, stock-reference and adjustment-event operations.
- Add `DataRepository::forCurrentThread(const std::string& dbPath)` returning a repository whose Qt SQL connection belongs to the calling thread.

- [x] Create versioned generic tables for bars, successful coverage intervals, source-specific stock/reference data, TDX adjustment events and their sync versions; retain existing tables without treating source-unknown rows as valid cache hits.
- [x] Store all five supported historical periods in the generic table, including turnover where present; exclude minute and tick periods from the new cache API.
- [x] Use batched transactions, prepared queries, `busy_timeout`, and WAL; do not move a `QSqlDatabase` across threads.
- [x] Merge overlapping/adjacent successful coverage intervals without marking empty or failed provider results as covered; persist current-window attempt/success timestamps separately from bar dates.
- [x] Preserve old `DataRepository` entry points where needed by current callers.

### Task 2: Expose TDX corporate-action snapshots and reusable adjustment

**Files:**
- Create: `src/data/adjustment_data_provider.h`
- Modify: `src/data/tdx/tdx_provider.h`
- Modify: `src/data/tdx/tdx_provider.cpp`
- Modify: `src/data/tdx/tdx_models.h` only if a shared event representation requires it

**Interfaces:**
- Add `AdjustmentEvent` with ex-date, category and the four GBBQ adjustment fields.
- Add `IAdjustmentDataProvider::getAdjustmentEvents(code, forceRefresh)` and `applyAdjustment(bars, events, mode)`.
- Implement the capability on `TdxProvider`; preserve the existing adjustment arithmetic and rounding.

- [x] Add an explicit refresh path that bypasses the provider's in-memory GBBQ cache when the SQLite snapshot is stale.
- [x] Convert decoded GBBQ records to/from the shared event representation and sort deterministically for hashing and persistence.
- [x] Keep a failed refresh distinguishable from a successful empty event list.
- [x] Reuse the current QFQ/HFQ transformation logic for local recomputation.

### Task 3: Add a provider-aware cache decorator and connect factory-created providers

**Files:**
- Create: `src/data/cached_data_provider.h`
- Create: `src/data/cached_data_provider.cpp`
- Modify: `src/data/provider_factory.cpp`
- Modify: `src/data/CMakeLists.txt` or `src/CMakeLists.txt` source list, following the repository's current layout
- Modify: `src/data/multi_provider.cpp` only if forwarding a new metadata operation is required

**Interfaces:**
- `CachedDataProvider(std::unique_ptr<IDataProvider> inner, std::string sourceId, std::string dbPath)`.
- Delegate all non-history live methods unchanged.
- Cache history methods by `(sourceId, code, period, BarPriceMode)`; use raw-plus-event local adjustment when the wrapped provider supports `IAdjustmentDataProvider`.

- [x] Cache only Daily/Weekly/Monthly/Quarterly/Yearly history; bypass all excluded periods and live data.
- [x] On a miss, fetch and persist the requested interval; on historical gaps, fetch only uncovered intervals; honor provider-native 640-bar epoch-sentinel windows while retaining full SQLite history.
- [x] For stale current raw history, refresh the tail with an overlap and upsert; use a 3-day overlap/freshness boundary.
- [x] For TDX adjusted reads, read/update Raw bars, independently refresh stale event snapshots, compare deterministic event versions, and locally rebuild QFQ/HFQ.
- [x] For providers without reliable action versions, persist their adjusted response separately and fully refresh the requested adjusted range when its 3-day cache freshness expires.
- [x] Cache `getStockInfo`, `getStockList` and `getSectorIndices` per provider with sync metadata; refresh stale references and serve last-known data only as a marked fallback after provider failure.
- [x] Wrap each concrete provider before constructing `MultiProvider`, preventing cross-provider cache mixing.
- [x] If SQLite initialization or an operation fails, delegate to the inner provider and log the cache failure.

### Task 4: Route parallel backtest raw downloads through SQLite and retain legacy import

**Files:**
- Modify: `src/data/parallel_bar_fetcher.cpp`
- Modify: `src/data/parallel_bar_fetcher.h` only if a helper signature changes
- Modify: `src/data/bar_disk_cache.cpp` only if migration needs a safe full-cache reader

- [x] In each fetch lane, check the source-specific SQLite raw daily cache before network access.
- [x] If SQLite misses or only partially covers TDX raw daily data, import missing rows from `.stb` without replacing newer SQLite rows, then fetch only missing/stale ranges.
- [x] Persist successfully fetched raw bars and coverage through a repository connection owned by that worker thread.
- [x] Keep `.stb` files unchanged and retain the existing TDX parallel-lane behavior.

### Task 5: Integrate and inspect the complete change

**Files:**
- Review the plan's touched data-layer files and existing dirty diffs.
- Update: `docs/superpowers/specs/2026-10-17-unified-sqlite-market-cache-design.md` only if implementation requires a documented contract adjustment.

- [x] Inspect all `makeDataProvider()` call paths to confirm historical UI/engine consumers receive the decorator.
- [x] Inspect direct `TdxProvider` construction paths and ensure the parallel backtest fetcher is covered.
- [x] Perform a source-level review of each scope exclusion, source/mode key, coverage update, fallback path and Qt SQL thread owner.
- [x] Do not run or add tests in this implementation pass, per the active task instructions.

**Build check:** `cmake --build build --target StockTerminal --parallel 4` completed and linked `src/StockTerminal.exe`.

**Review follow-up:** Added last-success sync state and a 60-second retry cooldown, bounded epoch-sentinel returns to the provider's 640-bar window, filled partial SQLite daily history from `.stb` without overwriting cached rows, and only advance reference-data freshness after SQLite commits successfully. The app target rebuild completed successfully.
