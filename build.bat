@echo off
rem =====================================================================
rem  Mayous build script  (MinGW-w64 / GCC)
rem
rem    build.bat          release build -> build\mayous.exe
rem    build.bat debug    debug build (console + trace log, no optimize)
rem
rem  NOTE: this file is deliberately ASCII-only. A .bat containing
rem  Shift-JIS text breaks apart under a non-932 console codepage,
rem  because trail bytes can be 0x5C ("\") or 0x7C ("|").
rem  Japanese documentation lives in README.md instead.
rem =====================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "OUT=%ROOT%build"
set "MODE=%~1"

where gcc >nul 2>&1
if errorlevel 1 (
    echo [ERROR] gcc not found in PATH. Install MinGW-w64:
    echo         winget install BrechtSanders.WinLibs.POSIX.UCRT
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

rem --- icons ------------------------------------------------------------
if not exist "%ROOT%res\mayous.ico" (
    echo [1/3] generating icons...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\make_icons.ps1"
    if errorlevel 1 exit /b 1
) else (
    echo [1/3] icons already present.
)

rem --- resources --------------------------------------------------------
rem  MinGW always links default-manifest.o for non-shared builds, so
rem  putting our own manifest in the .rc makes ld fail with
rem  "multiple non-default manifests". We therefore compile our manifest
rem  AS default-manifest.o and put it first on the search path with -B,
rem  shadowing the toolchain's copy.
echo [2/3] compiling resources...
windres -I "%ROOT%res" "%ROOT%res\mayous.rc" -O coff -o "%OUT%\mayous.res"
if errorlevel 1 exit /b 1
windres -I "%ROOT%res" "%ROOT%res\manifest_only.rc" -O coff -o "%OUT%\default-manifest.o"
if errorlevel 1 exit /b 1

rem --- program ----------------------------------------------------------
echo [3/3] compiling and linking...

set "CFLAGS=-municode -DUNICODE -D_UNICODE -Wall -Wextra -std=gnu11"
set "LIBS=-luser32 -lgdi32 -lshell32 -ladvapi32 -lcomctl32 -ldwmapi -luxtheme"

if /i "%MODE%"=="debug" (
    set "CFLAGS=!CFLAGS! -g -O0 -DMAYOUS_DEBUG"
    set "LINKFLAGS=-mwindows"
    set "EXE=%OUT%\mayous-debug.exe"
) else (
    rem -static: no libgcc / libwinpthread DLL needed on the target machine
    set "CFLAGS=!CFLAGS! -O2 -fno-ident"
    set "LINKFLAGS=-mwindows -static -s"
    set "EXE=%OUT%\mayous.exe"
)

gcc -B "%OUT%/" !CFLAGS! !LINKFLAGS! ^
    "%ROOT%src\main.c" "%ROOT%src\chord.c" "%ROOT%src\config.c" ^
    "%ROOT%src\agent.c" "%ROOT%src\settings.c" ^
    "%ROOT%src\capture.c" "%ROOT%src\legacy.c" "%ROOT%src\theme.c" ^
    "%ROOT%src\overlay.c" ^
    "%OUT%\mayous.res" -o "!EXE!" !LIBS!
if errorlevel 1 exit /b 1

echo.
echo done: !EXE!
for %%F in ("!EXE!") do echo size: %%~zF bytes
endlocal
