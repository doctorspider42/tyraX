# Builds the editor. Usage:
#   ./build.ps1          - configure (if needed) + build
#   ./build.ps1 -Run     - build and launch the editor
#   ./build.ps1 -Clean   - remove the build directory first
#   ./build.ps1 -Dev     - fast iteration build (-O1) into build-dev/
param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$Dev
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# Dependencies in vendor/. The list comes from deps.ps1 so this guard can never
# drift behind setup.ps1 again: every dependency CMake compiles is probed by a
# real source file, and a missing one is fixed here instead of surfacing later
# as "Cannot find source file: vendor/..." from cmake. This fires on a fresh
# clone, and just as often in an older worktree after merging a branch that
# added a dependency.
. ./deps.ps1
$missingDeps = @($VendorDeps | Where-Object { $_.Build -and -not (Test-Path $_.Probe) })
if ($missingDeps.Count -gt 0) {
    $names = ($missingDeps | ForEach-Object { $_.Dir }) -join ', '
    Write-Host "== Missing dependencies ($names) - running setup.ps1 ==" -ForegroundColor Cyan
    ./setup.ps1
    $missingDeps = @($VendorDeps | Where-Object { $_.Build -and -not (Test-Path $_.Probe) })
    if ($missingDeps.Count -gt 0) {
        $probes = ($missingDeps | ForEach-Object { $_.Probe }) -join ', '
        throw "Still missing after setup.ps1: $probes. Delete those vendor directories and run ./setup.ps1 again."
    }
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

# Dev gets its OWN build directory on purpose. Flipping CMAKE_BUILD_TYPE in one
# directory invalidates every object, so sharing it would mean a full rebuild on
# every switch - which is the exact cost the mode exists to avoid.
$buildDir = if ($Dev) { 'build-dev' } else { 'build' }
$buildType = if ($Dev) { 'Dev' } else { 'Release' }

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "== Cleaning $buildDir ==" -ForegroundColor Cyan
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path "$buildDir/build.ninja")) {
    Write-Host "== Configuring (cmake, $buildType) ==" -ForegroundColor Cyan
    cmake -S . -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$buildType" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
}

Write-Host '== Building ==' -ForegroundColor Cyan
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host "OK: $buildDir\tyrax-editor.exe" -ForegroundColor Green
if ($Dev) {
    Write-Host 'NOTE: Dev build (-O1) - fast to compile, slow at the host bakes. Not for release or benchmarks.' -ForegroundColor Yellow
}

if ($Run) {
    Start-Process -FilePath "$PSScriptRoot\$buildDir\tyrax-editor.exe"
}
