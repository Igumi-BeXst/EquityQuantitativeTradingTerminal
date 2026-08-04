@echo off
REM 运行 StockTerminal（需 Qt DLL 在 PATH）
cd /d d:\StockTerminal
set PATH=D:\Qt\6.11.1\msvc2022_64\bin;%PATH%
start "" build\src\StockTerminal.exe
