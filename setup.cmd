@echo off
REM cmd entry point for setup.ps1 - for shells where "./setup.ps1" is awkward
REM (plain cmd.exe, a double-click, a restrictive execution policy). Clones the
REM third-party dependencies into vendor\ and fetches the PS2 deploy tools.
REM
REM This is a WRAPPER ON PURPOSE. It used to carry its own copy of the
REM dependency list - a column of :clone calls - and drifted behind deps.ps1:
REM it never cloned vendor/ufbx (nor, earlier, stb's headers and miniaudio),
REM and it tried to "git clone" into vendor\tyra, whose engine sources are
REM tracked in this repo, so a fresh clone got "destination path already exists
REM and is not an empty directory" followed by cmake failing on the missing
REM ufbx sources. There is one list (deps.ps1) and one implementation
REM (setup.ps1) - do not reintroduce a second one here.
REM See "Platform parity" in .claude/skills/tyra-editor-dev/SKILL.md.
setlocal
cd /d "%~dp0"

call :findps || exit /b 1
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
exit /b %ERRORLEVEL%

:findps
REM PowerShell 7 if it is installed, else the Windows PowerShell every Windows
REM box ships with. Both run setup.ps1 unchanged.
set "PS="
for %%P in (pwsh.exe) do if not defined PS if exist "%%~$PATH:P" set "PS=%%~$PATH:P"
for %%P in (powershell.exe) do if not defined PS if exist "%%~$PATH:P" set "PS=%%~$PATH:P"
if not defined PS (
    echo Neither pwsh.exe nor powershell.exe was found on PATH.
    echo setup.cmd only forwards to setup.ps1 - run that directly instead.
    exit /b 1
)
exit /b 0
