@echo off
REM Clones third-party dependencies into vendor\ and fetches the PS2 deploy
REM tools, from cmd.exe.
REM
REM A thin wrapper on setup.ps1, on purpose: the dependency list lives in
REM deps.ps1 and nowhere else. This file used to duplicate it as a column of
REM :clone calls and silently fell three dependencies behind (stb's headers,
REM ufbx, miniaudio). See "Platform parity" in
REM .claude/skills/tyra-editor-dev/SKILL.md.
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1"
endlocal & exit /b %errorlevel%
