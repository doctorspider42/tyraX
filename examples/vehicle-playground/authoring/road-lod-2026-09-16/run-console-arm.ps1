# One physical-PS2 measurement arm of the road lateral-budget A/B.
#
# Adapted from ../ee-probes-2026-09-16/run-console-arm.ps1. Two differences.
# The arms here are two EDITOR binaries generating the same example, not two
# engine builds (see build-arm.ps1), so ARM.json records the editor hash as
# well as the ELF's. And the instrumenter runs WITHOUT --attribute, so
# frame-attrib.csv is never written and frame-cost.csv is the file to wait for.
#
# THE CONSOLE IS A SHARED RESOURCE. A listening tcp/18193 does NOT mean free -
# check for a live ps2client first:
#   Get-CimInstance Win32_Process -Filter "name like '%ps2%'"
# and never reset a console another session is running on.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Arm,
    [string]$Label = '',
    [string]$Root = 'D:/tyra-roadlod-0916',
    [int]$PreviousClient = 0,
    [int]$TimeoutSeconds = 240
)
$ErrorActionPreference = 'Stop'
$host_ = '192.168.100.150'
$client = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path 'tools/ps2client/bin/ps2client.exe'
$bin = Join-Path $Root "arms/$Arm/bin"
$runName = if ($Label) { "$Arm-$Label" } else { $Arm }
$out = Join-Path $Root "results/$runName"

if (!(Test-Path -LiteralPath (Join-Path $bin 'vehicle-playground.elf'))) { throw "No ELF for arm $Arm" }
if (Test-Path -LiteralPath $out) { throw "Archive $out before re-running $runName" }

# A leftover CSV from the previous run of THIS arm would be collected as this
# run's result - the control is deliberately run twice on one ELF, so this is
# not a theoretical hazard.
foreach ($stale in 'frame-cost.csv', 'frame-attrib.csv', 'district-benchmark.csv', 'log.txt') {
    Remove-Item -LiteralPath (Join-Path $bin $stale) -ErrorAction SilentlyContinue
}
Set-Content -LiteralPath (Join-Path $bin 'ps2link.run') -Value 'ps2link' -Encoding ascii

if ($PreviousClient -gt 0) {
    $old = Get-CimInstance Win32_Process -Filter "ProcessId=$PreviousClient" -ErrorAction SilentlyContinue
    if ($old) {
        if ($old.Name -ne 'ps2client.exe' -or $old.CommandLine -notmatch $host_) { throw 'Unexpected previous client' }
        Stop-Process -Id $PreviousClient
        Start-Sleep -Seconds 2
    }
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$elfHash = (Get-FileHash -LiteralPath (Join-Path $bin 'vehicle-playground.elf') -Algorithm SHA256).Hash

Push-Location -LiteralPath $bin
try {
    & $client -h $host_ -t 10 reset *> (Join-Path $out 'reset.log')
    if ($LASTEXITCODE -ne 0) { throw 'Reset command failed' }
    Start-Sleep -Seconds 3
    $proc = Start-Process -FilePath $client `
        -ArgumentList '-h', $host_, 'execee', 'host:vehicle-playground.elf', '-ps2link' `
        -WorkingDirectory $bin -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $out 'host.log') -RedirectStandardError (Join-Path $out 'host.err')
    [pscustomobject]@{
        arm = $Arm; label = $Label; pid = $proc.Id
        started = (Get-Date).ToString('o'); elf = $elfHash
        armJson = (Get-Content -LiteralPath (Join-Path $bin '../ARM.json') -Raw | ConvertFrom-Json)
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'run.json')
    Write-Output "Launched $runName, ps2client PID $($proc.Id), ELF $elfHash"

    # The instrumented fixture writes frame-cost.csv at frame 1440 - 120 warm-up
    # plus 240 recorded rows in each of four parked poses. Waiting for the FILE
    # is what makes this a single scripted step.
    # WAIT FOR THE ROW COUNT TO STOP GROWING, not for the file to appear. The
    # CSV is written over host: at ps2link speed, and a fixed sleep after it
    # first exists collects a half-written one (measured in round one: 570 of
    # 960 rows). Without --attribute there is no second file, so frame-cost.csv
    # is both the first and the last.
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $cost = Join-Path $bin 'frame-cost.csv'
    $prev = -1
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        if (!(Test-Path -LiteralPath $cost)) { continue }
        $n = (Get-Content -LiteralPath $cost -ErrorAction SilentlyContinue).Count
        if ($n -gt 0 -and $n -eq $prev) { break }
        $prev = $n
    }
    foreach ($f in 'frame-cost.csv', 'frame-attrib.csv', 'log.txt') {
        $src = Join-Path $bin $f
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src (Join-Path $out $f) }
    }
    if (!(Test-Path -LiteralPath (Join-Path $out 'frame-cost.csv'))) {
        Write-Output "WARNING: no frame-cost.csv after $TimeoutSeconds s - REJECT this run"
    } else {
        $rows = (Get-Content -LiteralPath (Join-Path $out 'frame-cost.csv')).Count - 1
        Write-Output "Collected $rows rows (expect 960)"
    }
    Stop-Process -Id $proc.Id -ErrorAction SilentlyContinue
}
finally { Pop-Location }
