@echo off
cd /d d:\StockTerminal
echo === Activating MSVC environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

REM Add Qt DLLs to PATH for test execution
set PATH=D:\Qt\6.11.1\msvc2022_64\bin;%PATH%

echo.
echo === Configuring CMake (with Qt) ===
cmake --preset with-qt

if %ERRORLEVEL% NEQ 0 (
    echo === CMake configure FAILED ===
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo === Building ===
cmake --build --preset with-qt

if %ERRORLEVEL% NEQ 0 (
    echo === Build FAILED ===
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo === Running tests ===
ctest --preset default

echo.
echo === Build + Tests SUCCESS ===
echo.
echo Run: build\src\StockTerminal.exe
pause
