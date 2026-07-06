# Builds the editor. Usage:
#   ./build.ps1          - configure (if needed) + build
#   ./build.ps1 -Run     - build and launch the editor
#   ./build.ps1 -Clean   - remove the build directory first
param(
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# Dependencies in vendor/
if (-not (Test-Path 'vendor/imgui') -or -not (Test-Path 'vendor/glfw') -or -not (Test-Path 'vendor/imguizmo')) {
    Write-Host '== Cloning dependencies (setup.ps1) ==' -ForegroundColor Cyan
    ./setup.ps1
}

# g++ from scoop's mingw is often not on PATH in fresh shells
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $mingwBin = "$env:USERPROFILE\scoop\apps\mingw\current\bin"
    if (Test-Path "$mingwBin\g++.exe") {
        $env:PATH = "$mingwBin;$env:PATH"
    }
    else {
        throw "g++ not found. Install the toolchain first: scoop install mingw cmake ninja"
    }
}

if ($Clean -and (Test-Path 'build')) {
    Write-Host '== Cleaning build directory ==' -ForegroundColor Cyan
    Remove-Item -Recurse -Force 'build'
}

if (-not (Test-Path 'build/build.ninja')) {
    Write-Host '== Configuring (cmake) ==' -ForegroundColor Cyan
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
}

Write-Host '== Building ==' -ForegroundColor Cyan
cmake --build build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host "OK: build\tyra-editor.exe" -ForegroundColor Green

if ($Run) {
    Start-Process -FilePath "$PSScriptRoot\build\tyra-editor.exe"
}
