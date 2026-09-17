# The PICTURE half of the reflection-reuse acceptance: photograph the same
# parked poses in two arms and compare them pixel for pixel.
#
# It needs a `debug` fixture, not the `quiet-debug` one the counts are taken
# on: the game photographs ITSELF through the Live Debugger's command channel
# (docs/devkit.md, "The game's own screenshot"), which quiet-debug switches off.
# That is the right trade here - a capture is a picture question and the live
# tools cost milliseconds, not pixels.
#
# Two rules this obeys, both paid for by earlier rounds:
#   * the fixture's own `bin/district-benchmark.csv` appearing is the "it is
#     safe to write into bin/ now" signal. Nothing is written during sampling.
#   * only the DAY poses are comparable. The night ones have authored lamp
#     flicker and twinkling stars, so three captures of ONE arm differ from
#     each other and no between-arm number can be read from them.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fixture,
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string]$Editor,
    # Fixture phases to photograph. 0 = garage day, 2 = outer day; 1 and 3 are
    # the night poses and are deliberately not in the default.
    [int[]]$Poses = @(0, 2),
    # Repeats per pose. Two is the minimum that can say whether the arm is
    # deterministic at all, which has to be established before any between-arm
    # difference means anything.
    [int]$Repeats = 3,
    [int]$TimeoutSec = 420
)
$ErrorActionPreference = 'Stop'

$elf = (Resolve-Path -LiteralPath (Join-Path $Fixture 'bin/vehicle-playground.elf')).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null
foreach ($f in 'district-benchmark.csv', 'frame-inventory.csv', 'livedbg.cmd',
                'frame.tga', 'livedbg.bin') {
    Remove-Item -LiteralPath (Join-Path $Fixture "bin/$f") -ErrorAction SilentlyContinue
}

$pcsx2 = @("$env:ProgramFiles\PCSX2\pcsx2-qt.exe",
           "${env:ProgramFiles(x86)}\PCSX2\pcsx2-qt.exe") |
         Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $pcsx2) { throw 'pcsx2-qt.exe not found' }
$proc = Start-Process -FilePath $pcsx2 -PassThru -ArgumentList @(
    '-batch', '-nogui', '-logfile', (Join-Path $Out 'emulog.txt'), '-elf', $elf)
Write-Output "PCSX2 pid $($proc.Id) on $elf"
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not (Test-Path -LiteralPath (Join-Path $Fixture 'bin/district-benchmark.csv'))) {
        Start-Sleep -Seconds 5
        if ($proc.HasExited) { throw "PCSX2 exited early ($($proc.ExitCode))" }
        if ((Get-Date) -gt $deadline) { throw 'Timed out before the sampler finished' }
    }
    Start-Sleep -Seconds 3
    foreach ($pose in $Poses) {
        Set-Content -LiteralPath (Join-Path $Fixture 'bin/district-benchmark-pose.txt') `
                    -Value "$pose" -NoNewline
        Start-Sleep -Seconds 6   # the pose file is read every 30 frames
        for ($k = 1; $k -le $Repeats; $k++) {
            Remove-Item -LiteralPath (Join-Path $Fixture 'bin/frame.tga') -ErrorAction SilentlyContinue
            $png = Join-Path $Out ("pose$pose-$k.png")
            & $Editor --capture-frame $Fixture -o $png
            if ($LASTEXITCODE -ne 0) { throw "--capture-frame failed at pose $pose" }
            Start-Sleep -Seconds 2
        }
    }
} finally {
    try { $proc.CloseMainWindow() | Out-Null } catch {}
    Start-Sleep -Seconds 2
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}
foreach ($f in 'log.txt', 'district-benchmark.csv') {
    $src = Join-Path $Fixture "bin/$f"
    if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $Out $f) }
}
if (Test-Path -LiteralPath (Join-Path $Fixture 'ARM.json')) {
    Copy-Item -Force (Join-Path $Fixture 'ARM.json') (Join-Path $Out 'ARM.json')
}
Get-ChildItem -LiteralPath $Out | Select-Object Name, Length
