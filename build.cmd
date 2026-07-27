@echo off
REM Builds the editor from cmd.exe. Usage:
REM   build.cmd          - configure (if needed) + build
REM   build.cmd run      - build and launch the editor
REM   build.cmd clean    - remove the build directory first
REM
REM A THIN WRAPPER on build.ps1, on purpose. This used to be a full cmd
REM translation carrying its own hardcoded list of vendor/ dependencies, which
REM froze at four entries while deps.ps1 grew to seven. That matters more than
REM it looks: a bare `build` in PowerShell resolves to build.CMD before
REM build.PS1 (PATHEXT order), so the stale twin is what actually ran, never
REM fetched vendor/miniaudio, and cmake failed with "miniaudio.h: No such file
REM or directory" on a tree that built fine on Linux. There is now exactly one
REM Windows build script - do not reintroduce build logic here.
REM See "Platform parity" in .claude/skills/tyra-editor-dev/SKILL.md.
setlocal
cd /d "%~dp0"

set "PS_ARGS="
for %%A in (%*) do (
    if /i "%%~A"=="run"   set "PS_ARGS=%PS_ARGS% -Run"
    if /i "%%~A"=="clean" set "PS_ARGS=%PS_ARGS% -Clean"
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"%PS_ARGS%
endlocal & exit /b %errorlevel%
