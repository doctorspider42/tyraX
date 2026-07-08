@echo off
REM Builds the editor (cmd version of build.ps1). Usage:
REM   build.cmd          - configure (if needed) + build
REM   build.cmd run      - build and launch the editor
REM   build.cmd clean    - remove the build directory first
setlocal
cd /d "%~dp0"

set "DO_RUN="
set "DO_CLEAN="
for %%A in (%*) do (
    if /i "%%~A"=="run"   set "DO_RUN=1"
    if /i "%%~A"=="clean" set "DO_CLEAN=1"
)

REM Dependencies in vendor\
if not exist "vendor\imgui" goto :setup
if not exist "vendor\glfw" goto :setup
if not exist "vendor\imguizmo" goto :setup
if not exist "vendor\imnodes" goto :setup
goto :toolchain

:setup
echo == Cloning dependencies (setup.cmd) ==
call "%~dp0setup.cmd" || goto :error

:toolchain
REM g++ from scoop's mingw is often not on PATH in fresh shells
where g++ >nul 2>&1
if %errorlevel%==0 goto :clean
set "MINGW_BIN=%USERPROFILE%\scoop\apps\mingw\current\bin"
if exist "%MINGW_BIN%\g++.exe" (
    set "PATH=%MINGW_BIN%;%PATH%"
) else (
    echo g++ not found. Install the toolchain first: scoop install mingw cmake ninja
    goto :error
)

:clean
if defined DO_CLEAN if exist "build" (
    echo == Cleaning build directory ==
    rmdir /s /q "build"
)

if not exist "build\build.ninja" (
    echo == Configuring (cmake) ==
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || (echo cmake configure failed & goto :error)
)

echo == Building ==
cmake --build build || (echo build failed & goto :error)

echo OK: build\tyra-editor.exe

if defined DO_RUN start "" "%~dp0build\tyra-editor.exe"

endlocal
exit /b 0

:error
endlocal
exit /b 1
