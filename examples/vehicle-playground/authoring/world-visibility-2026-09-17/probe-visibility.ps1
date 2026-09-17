# Photograph the garage-day frame once per PROBE, all from ONE boot.
#
# This is what makes the round cheap. The alternative - one fixture per object,
# each with its own native build and its own PCSX2 boot - is roughly an hour
# per object. Runtime `visible` toggling (the primitive content-sampler.py
# established in the reflection round) collapses that to one build and one boot
# for the whole population.
#
# A probe is a SET of object indices to hide; `-1` is the control. The control
# is photographed FIRST and LAST: an arm whose own control drifts between the
# start and the end of the run cannot support any between-probe number, and
# that has to be established before a single zero is believed.
#
# THE EMULATOR IS A SHARED RESOURCE. This launches PCSX2 itself on this arm's
# ELF and stops only that process; never `--build --run`, which reaps other
# worktrees' emulators.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fixture,
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string]$Editor,
    # Each entry is a probe: a name, then the indices to hide. "ctl" hides
    # nothing. Defaults to the fourteen objects the 2026-09-16 inventory
    # measured as submitting triangles in this pose, plus the controls below.
    [string[]]$Probes = @(
        'ctl=-1',
        'o34=34', 'o36=36', 'o40=40', 'o42=42', 'o1=1', 'o121=121', 'o120=120',
        'o38=38', 'o60=60', 'o61=61', 'o76=76', 'o79=79', 'o35=35', 'o41=41',
        # The OCCLUDER pairs. A lone zero is ambiguous - it cannot tell "the
        # object was invisible" from "the hide never reached that index". These
        # remove the suspected occluder TOO: if the pair changes more pixels
        # than the occluder alone, the second object was both reachable and
        # behind it. That is the positive control every zero needs.
        'p3440=34 40', 'p3642=36 42',
        # The suspected zero set hidden TOGETHER. Two objects can each be
        # individually redundant while their union is not; only this rules
        # that out.
        'z4042=40 42'
    ),
    [int]$Repeats = 2,
    [int]$SettleSec = 8,
    # The game photographs itself over host: fs and that write can tear
    # ("wrote N of M bytes - the host: write did not complete"). It is a
    # transient, not a failure of the probe, so a capture is retried rather
    # than allowed to kill a nineteen-probe single-boot run.
    [int]$CaptureTries = 4,
    [int]$TimeoutSec = 1800
)
$ErrorActionPreference = 'Stop'

$elf = (Resolve-Path -LiteralPath (Join-Path $Fixture 'bin/vehicle-playground.elf')).Path
New-Item -ItemType Directory -Force -Path $Out | Out-Null
foreach ($f in 'district-benchmark.csv', 'frame-inventory.csv', 'livedbg.cmd',
                'frame.tga', 'livedbg.bin', 'district-benchmark-pose.txt') {
    Remove-Item -LiteralPath (Join-Path $Fixture "bin/$f") -ErrorAction SilentlyContinue
}

$pcsx2 = @("$env:ProgramFiles\PCSX2\pcsx2-qt.exe",
           "${env:ProgramFiles(x86)}\PCSX2\pcsx2-qt.exe") |
         Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $pcsx2) { throw 'pcsx2-qt.exe not found' }
$proc = Start-Process -FilePath $pcsx2 -PassThru -ArgumentList @(
    '-batch', '-nogui', '-logfile', (Join-Path $Out 'emulog.txt'), '-elf', $elf)
Write-Output "PCSX2 pid $($proc.Id) on $elf"

# The control is re-photographed at the end under the name `ctl2`, which is the
# run's own drift check.
$plan = @($Probes) + @('ctl2=-1')
$failed = @()
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while (-not (Test-Path -LiteralPath (Join-Path $Fixture 'bin/district-benchmark.csv'))) {
        Start-Sleep -Seconds 5
        if ($proc.HasExited) { throw "PCSX2 exited early ($($proc.ExitCode))" }
        if ((Get-Date) -gt $deadline) { throw 'Timed out before the warm-up finished' }
    }
    Start-Sleep -Seconds 3
    foreach ($spec in $plan) {
        $name = $spec.Split('=')[0]
        $set  = $spec.Split('=')[1] -replace ',', ' '
        Set-Content -LiteralPath (Join-Path $Fixture 'bin/district-benchmark-pose.txt') `
                    -Value $set -NoNewline
        Start-Sleep -Seconds $SettleSec   # the command file is read every 30 frames
        for ($k = 1; $k -le $Repeats; $k++) {
            $png = Join-Path $Out ("$name-$k.png")
            $ok = $false
            for ($try = 1; $try -le $CaptureTries -and -not $ok; $try++) {
                Remove-Item -LiteralPath (Join-Path $Fixture 'bin/frame.tga') -ErrorAction SilentlyContinue
                & $Editor --capture-frame $Fixture -o $png
                if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $png)) { $ok = $true }
                else {
                    Write-Output "  retry $try/$CaptureTries for $name-$k (torn host: write)"
                    if ($proc.HasExited) { throw "PCSX2 died during $name" }
                    Start-Sleep -Seconds 4
                }
            }
            if (-not $ok) { $failed += "$name-$k"; Write-Output "  GAVE UP on $name-$k" }
            Start-Sleep -Seconds 2
        }
        Write-Output "captured $name (hiding: $set)"
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
(($plan -join "`n") + "`nFAILED: " + ($failed -join ' ')) |
    Set-Content -LiteralPath (Join-Path $Out 'PROBES.txt')
if ($failed.Count) { Write-Output "CAPTURES THAT NEVER LANDED: $($failed -join ' ')" }
Get-ChildItem -LiteralPath $Out | Select-Object Name, Length
