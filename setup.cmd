@echo off
REM Clones third-party dependencies into vendor\ (cmd version of setup.ps1)
setlocal
cd /d "%~dp0"

call :clone "https://github.com/ocornut/imgui.git"          docking vendor\imgui
call :clone "https://github.com/glfw/glfw.git"              3.4     vendor\glfw
call :clone "https://github.com/CedricGuillemet/ImGuizmo.git" master vendor\imguizmo
call :clone "https://github.com/Nelarius/imnodes.git"       master  vendor\imnodes
call :clone "https://github.com/nothings/stb.git"           master  vendor\stb
call :clone "https://github.com/h4570/tyra.git"             master  vendor\tyra

REM Ensure the stb single-headers we #include are present, even when vendor\stb
REM is a stale/partial directory that predates the full clone above (no .git,
REM so the clone step skips it). Back-fill any missing header directly.
call :stbhdr stb_image.h
call :stbhdr stb_truetype.h
call :stbhdr stb_image_write.h

endlocal
exit /b 0

:clone
REM %1 = url  %2 = branch  %3 = dir
if exist "%~3\.git" (
    echo OK: %~3 already present
    exit /b 0
)
git clone --depth 1 --branch %2 %1 "%~3"
exit /b 0

:stbhdr
REM %1 = header filename
if exist "vendor\stb\%~1" exit /b 0
echo Fetching %~1
curl -sSL -o "vendor\stb\%~1" "https://raw.githubusercontent.com/nothings/stb/master/%~1"
exit /b 0
