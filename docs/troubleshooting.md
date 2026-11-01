# 常见问题排查

## 编译问题
- Qt not found: 确保 vcpkg 安装 qt6 且 CMAKE_TOOLCHAIN_FILE 正确
- spdlog not found: vcpkg install spdlog

## 运行时问题
- DLL missing: 将 build/vcpkg_installed/*/bin 加入 PATH
- SQLite driver error in `DataRepository` tests: 确认 Qt SQLite driver 可用；当前应用行情流程尚未接入该仓储

## 全市场选股中途崩溃（0xC0000005 / 堆损坏）
- 症状：全市场勾选因子开始选股，数据抓取阶段结束后 UI 线程崩溃；WER 显示 `StockTerminal.exe` 0xC0000005，dump 栈顶为 `std::string::_Tidy_deallocate` / `ScreenerPanel::onAllDataFetched` 析构快照 vector。
- 原因（2026-11-01 已修）：`AKShareProvider::fetch()` 使用 UI 线程创建的成员 `QNetworkAccessManager`，选股 IO 线程回退到 AKShare（TDX 缺数据）时跨线程使用 QNAM，造成堆损坏；崩溃只是稍后在 UI 线程析构 string 时暴露。
- 修复：AKShareProvider 全部改用 `threadLocalHttp()` 线程本地 QNAM；ScreenerPanel IO 进度回调增加空指针检查；表达式引擎增加矩阵尺寸越界校验。
- 排查要点：若 `%LOCALAPPDATA%\CrashDumps\StockTerminal.exe.*.dmp` 存在，用 minidump 解析线程栈；同时检查日志是否有 `QObject: Cannot create children for a parent that is in a different thread`——出现即说明仍有跨线程 QObject/QNAM 使用。
