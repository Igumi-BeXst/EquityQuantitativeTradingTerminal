@echo off
rem ============================================================================
rem StockTerminal build script
rem   build.bat            -> Debug only (default; preset with-qt, output build\)
rem   build.bat debug      -> same
rem   build.bat release    -> Release only (preset release-qt, output build\release-qt\)
rem   build.bat both       -> Debug then Release
rem
rem Note: Release is NOT built by default. Its test suite currently has a known
rem failure (MultiProviderTest.DefaultProviderUsesTdxWithAkshareFailover), so it
rem is opt-in only until that is resolved.
rem
rem NOTE 1: keep this file ASCII-only. cmd.exe parses .bat using the local code
rem         page; non-ASCII comment bytes can corrupt line endings so REM lines
rem         leak out and run as commands (this already broke the build once).
rem NOTE 2: avoid multi-line "( ... )" blocks around %1-style expansion. A ')'
rem         coming from the expanded value closes the block early, which made
rem         every branch report failure right after a successful configure.
rem ============================================================================
cd /d d:\StockTerminal

set "MODE=%~1"
if "%MODE%"=="" set "MODE=debug"

set VCPKG_ROOT=%CD%\vcpkg

echo === Activating MSVC environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

rem Qt DLLs are needed both to run the app and for ctest to start the test exes
set PATH=D:\Qt\6.11.1\msvc2022_64\bin;%PATH%

rem Point temp files at a project-local dir. Some environments (sandboxes,
rem security policies, restricted accounts) have a non-writable %TEMP%, which
rem breaks the persistence tests (BarDiskCache / CustomIndexStore /
rem TradeJournalStore / PaperTradeState).
set "TEMP=%CD%\build\test_tmp"
set "TMP=%CD%\build\test_tmp"
if not exist "%TEMP%" mkdir "%TEMP%" >nul 2>&1

if /i "%MODE%"=="debug"   goto :mode_debug
if /i "%MODE%"=="release" goto :mode_release
if /i "%MODE%"=="both"    goto :mode_both
echo.
echo Unknown mode: %MODE%
echo Usage: build.bat [debug^|release^|both]
exit /b 2

:mode_debug
call :build_one with-qt build
if errorlevel 1 goto :fail
goto :ok

:mode_release
rem release-qt reuses build\vcpkg_installed, produced by the with-qt preset
if not exist "build\vcpkg_installed" echo === WARN: build\vcpkg_installed missing; run "build.bat debug" once first ===
call :build_one release-qt build\release-qt
if errorlevel 1 goto :fail
goto :ok

:mode_both
call :build_one with-qt build
if errorlevel 1 goto :fail
call :build_one release-qt build\release-qt
if errorlevel 1 goto :fail
goto :ok

rem ---------------------------------------------------------------- subroutine
:build_one
echo.
echo ================================================================
echo  Preset: %~1    Output: %~2
echo ================================================================
echo === Configuring %~1 ===
cmake --preset %~1
if errorlevel 1 goto :one_cfg_fail

echo === Building %~1 ===
cmake --build --preset %~1
if errorlevel 1 goto :one_build_fail

echo === Testing %~1 ===
ctest --test-dir %~2 --output-on-failure
if errorlevel 1 goto :one_test_fail
exit /b 0

:one_cfg_fail
echo === Configure FAILED : %~1 ===
exit /b 1

:one_build_fail
echo === Build FAILED : %~1 ===
exit /b 1

:one_test_fail
echo === Tests FAILED : %~1 === see failing test names above
exit /b 1

rem ------------------------------------------------------------------- results
:fail
echo.
echo === BUILD FAILED ===
pause
exit /b 1

:ok
echo.
echo === Build + Tests SUCCESS ===
echo.
echo   Debug   : build\src\StockTerminal.exe           (run.bat)
echo   Release : build\release-qt\src\StockTerminal.exe (run-release.bat)
pause
exit /b 0
