# Build the shared Motor District fixture against ONE engine tree and measure it
# on the physical console. Arms differ only by the engine they were built from,
# so the fixture, its assets and the generated game stay byte-identical across
# a round.
#
# Usage:
#   measure-round.ps1 -Arm r1-candidate -Engine D:/path/to/worktree/vendor/tyra
param(
    [Parameter(Mandatory = $true)][string]$Arm,
    [Parameter(Mandatory = $true)][string]$Engine,
    [string]$Cache = "$env:LOCALAPPDATA\tyra-editor\native-build\flush-probe-0915",
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = 'D:/tyra-flush-0915'
$project = Join-Path $root 'probe'
$bin = Join-Path $project 'bin'
$out = Join-Path $root "results/$Arm"
if (Test-Path -LiteralPath $out) { throw "Archive $out before re-running this arm" }

if (-not $SkipBuild) {
    if (!(Test-Path -LiteralPath (Join-Path $Engine 'engine/Makefile'))) {
        if (!(Test-Path -LiteralPath (Join-Path $Engine 'engine'))) { throw "Not an engine tree: $Engine" }
    }
    Write-Output "== building $Arm from $Engine"
    # The native build writes ordinary progress and make warnings to stderr,
    # which PowerShell turns into terminating NativeCommandError records under
    # ErrorActionPreference Stop. Judge the build by its output instead.
    $ErrorActionPreference = 'Continue'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:/tyra-editor/tools/toolchain/native-build.ps1 `
        -Project $project -Engine $Engine -Cache $Cache `
        -Toolchain "$env:LOCALAPPDATA\tyra-editor\toolchain\ps2dev" 2>&1 |
        Select-String -Pattern 'rebuilding|Native build complete|error:|Error' | ForEach-Object { $_.Line }
    $ErrorActionPreference = 'Stop'
    $elf = Join-Path $bin 'vehicle-playground.elf'
    if (!(Test-Path -LiteralPath $elf)) { throw 'Build produced no ELF' }
    $age = (Get-Date) - (Get-Item -LiteralPath $elf).LastWriteTime
    if ($age.TotalMinutes -gt 10) { throw "ELF is $([int]$age.TotalMinutes) min old - the build did not relink" }
}

# The full-asset gate: a deployment missing baked resources measures something
# else entirely (docs/performance-hardware-recheck.md).
$gate = & python D:/tyra-editor/examples/vehicle-playground/authoring/vu-cost-dma-cache-2026-09-15/check_assets.py `
    $bin D:/tyra-editor/examples/vehicle-playground/authoring/vu-cost-dma-cache-2026-09-15/asset-manifest.json
$gate | ForEach-Object { Write-Output $_ }
if ($LASTEXITCODE -ne 0) { throw 'Full-asset gate failed - do not measure this deployment' }

Get-CimInstance Win32_Process -Filter "Name='ps2client.exe'" | ForEach-Object {
    if ($_.CommandLine -match '192\.168\.100\.150') {
        Write-Output "stopping previous file server $($_.ProcessId)"
        Stop-Process -Id $_.ProcessId -Force
    }
}
Start-Sleep -Seconds 2
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'run-arm.ps1') `
    -Arm $Arm -ExtraFlushes 0 -Suppress 0
Write-Output "== waiting for bin/frame-cost.csv to finish (written a line at a time over host:)"
$csv = Join-Path $bin 'frame-cost.csv'
$prev = -1
for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep -Seconds 5
    $size = if (Test-Path -LiteralPath $csv) { (Get-Item -LiteralPath $csv).Length } else { 0 }
    if ($size -gt 120000 -and $size -eq $prev) {
        Copy-Item -LiteralPath $csv -Destination $out
        Copy-Item -LiteralPath (Join-Path $bin 'district-benchmark.csv') -Destination $out -ErrorAction SilentlyContinue
        Write-Output "== $Arm done: $size bytes"
        exit 0
    }
    $prev = $size
}
throw "No complete frame-cost.csv after 450 s - check $out\host.log"
