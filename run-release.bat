@echo off
rem 启动 Release 版 StockTerminal（比 Debug 快 10~50 倍，日常使用推荐）
rem 构建: cmake --preset release-qt && cmake --build --preset release-qt
set "QT_BIN=D:\Qt\6.11.1\msvc2022_64\bin"
if exist "%QT_BIN%\Qt6Core.dll" set "PATH=%QT_BIN%;%PATH%"
start "" "%~dp0build\release-qt\src\StockTerminal.exe"
