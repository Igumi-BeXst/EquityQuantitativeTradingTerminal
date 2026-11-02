# 常见问题排查

## 编译问题
- Qt not found: 确保 vcpkg 安装 qt6 且 CMAKE_TOOLCHAIN_FILE 正确
- spdlog not found: vcpkg install spdlog

## 运行时问题
- DLL missing: 将 build/vcpkg_installed/*/bin 加入 PATH
- SQLite driver error in `DataRepository` tests: 确认 Qt SQLite driver 可用；当前应用行情流程尚未接入该仓储

## 全市场选股中途崩溃（0xC0000005 / 增量构建 ABI 不一致）
- 症状：全市场勾选因子开始选股，数据抓取结束（进度约 50%）时 UI 线程崩溃；WER 显示 `StockTerminal.exe` 0xC0000005，dump 栈顶为 `std::string::_Tidy_deallocate` / `ScreenerPanel::onAllDataFetched` → `vector<QuoteFundamentals>::_Tidy`。
- 根因（2026-11-01 已修）：release **增量构建**残留了旧 `market_panel.cpp.obj`，其 `vector<QuoteFundamentals>::_Tidy` 模板实例按旧 112-byte 布局析构，而当前 `QuoteFundamentals` 已是 184-byte，导致元素错位、string 指针为 0 而崩溃。
- 修复：clean rebuild Debug 与 Release（删除对象/库后重新配置编译）；当前 exe 反汇编 `_Tidy` 步长为 `add rbx, 0xB8`（184）。
- 预防：修改 `QuoteFundamentals`/`Bar`/`StockCode` 等 data 层结构体后必须 clean rebuild，别依赖增量构建。
- 排查要点：若 `%LOCALAPPDATA%\CrashDumps\StockTerminal.exe.*.dmp` 存在，可算 `(vector.end - vector.begin) / N` 得到元素大小，并与当前 `sizeof` 对比；若崩溃 string 地址落在 `begin + i*旧sizeof + 8`，基本可判定为旧对象文件 ABI 不一致。跨线程 QNAM 警告是另一类独立隐患（已用线程本地 QNAM 修复），不要与 ABI 混淆。

## 选股换手率/基本面数据全为中性（原始值 --）
- 症状：选股结果中「换手率」等依赖 `QuoteFundamentals` 的因子全部 50 分、原始值 `--`；但主窗口个股关键数据能看到换手率。
- 原因：TDX 日线不含换手率，选股快照原先只走 TDX→AKShare（东财 ulist）；东财接口不可用时快照为空，而主窗口换手率走腾讯行情，所以显示正常。
- 修复（2026-11-03）：`MultiProvider` 增加基本面第三备源 `quoteFallback`，multi 模式注入 `TencentProvider`；主/备源无有效快照时自动回退腾讯（不影响 K线/行情源）。
- 注意：腾讯行情只提供换手率/市盈 TTM/市值/股本等字段；PB/ROE/营收/净利/毛利率/负债率仍需东财，东财不可用时相关基本面因子仍会中性。
- 排查：`fundamental_calib <code>` 可验证东财通路；若东财失败但腾讯正常，选股应自动使用腾讯换手率。
