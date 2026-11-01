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
