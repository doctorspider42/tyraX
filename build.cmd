@echo off
REM cmd entry point for build.ps1. Usage:
REM   build.cmd          - configure (if needed) + build
REM   build.cmd run      - build and launch the editor
REM   build.cmd clean    - remove the build directory first
REM
REM This is a WRAPPER ON PURPOSE. It used to carry its own copy of the
REM vendor-dependency guard, checking four hardcoded directories, and drifted
REM behind deps.ps1 - so a fresh clone walked straight into cmake with
REM vendor/ufbx missing and failed with "Cannot find source file:
REM vendor/ufbx/ufbx.c". build.ps1 probes the ONE list in deps.ps1; keep the
REM guard there, not here.
setlocal
cd /d "%~dp0"

set "DO_RUN="
set "DO_CLEAN="
for %%A in (%*) do call :arg "%%~A" || exit /b 2

set "ARGS="
if defined DO_CLEAN set "ARGS=%ARGS% -Clean"
if defined DO_RUN   set "ARGS=%ARGS% -Run"

call :findps || exit /b 1
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"%ARGS%
exit /b %ERRORLEVEL%

:arg
REM %1 = one command-line word, mapped onto the build.ps1 switch it names.
if /i "%~1"=="run"    ( set "DO_RUN=1"   & exit /b 0 )
if /i "%~1"=="-run"   ( set "DO_RUN=1"   & exit /b 0 )
if /i "%~1"=="clean"  ( set "DO_CLEAN=1" & exit /b 0 )
if /i "%~1"=="-clean" ( set "DO_CLEAN=1" & exit /b 0 )
echo Unknown option: %~1 ^(expected "run" and/or "clean"^)
exit /b 2

:findps
REM PowerShell 7 if it is installed, else the Windows PowerShell every Windows
REM box ships with. Both run build.ps1 unchanged.
set "PS="
for %%P in (pwsh.exe) do if not defined PS if exist "%%~$PATH:P" set "PS=%%~$PATH:P"
for %%P in (powershell.exe) do if not defined PS if exist "%%~$PATH:P" set "PS=%%~$PATH:P"
if not defined PS (
    echo Neither pwsh.exe nor powershell.exe was found on PATH.
    echo build.cmd only forwards to build.ps1 - run that directly instead.
    exit /b 1
)
exit /b 0
