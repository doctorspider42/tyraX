# One measurement arm of the FlushCache-cost probe.
#
# The ELF is IDENTICAL in every arm; only bin/perf-probe.cfg differs, so nothing
# but the number of extra FlushCache(0) calls per static VIF1 submission can
# explain a difference. Adapted from
# examples/vehicle-playground/authoring/submission-batching-2026-09-14/run-arm.ps1.
param(
    [Parameter(Mandatory = $true)][string]$Arm,
    [Parameter(Mandatory = $true)][int]$ExtraFlushes,
    [int]$Suppress = 0,
    [int]$PreviousClient = 0
)
$ErrorActionPreference = 'Stop'
$root = 'D:/tyra-flush-0915'
$bin = Join-Path $root 'probe/bin'
$out = Join-Path $root "results/$Arm"
if (!(Test-Path -LiteralPath (Join-Path $bin 'vehicle-playground.elf'))) { throw 'No ELF' }
if (Test-Path -LiteralPath $out) { throw "Archive $out before re-running this arm" }

# A leftover CSV from the previous arm would be collected as this arm's result.
foreach ($stale in 'frame-cost.csv', 'district-benchmark.csv', 'hardware-trace.csv', 'livedbg.cmd') {
    Remove-Item -LiteralPath (Join-Path $bin $stale) -ErrorAction SilentlyContinue
}
Set-Content -LiteralPath (Join-Path $bin 'perf-probe.cfg') -Value "$ExtraFlushes $Suppress" -Encoding ascii
Set-Content -LiteralPath (Join-Path $bin 'ps2link.run') -Value 'ps2link' -Encoding ascii
if ((Get-Content -LiteralPath (Join-Path $bin 'ps2link.run') -Raw).Trim() -ne 'ps2link') { throw 'Invalid marker' }
if ((Get-Content -LiteralPath (Join-Path $bin 'perf-probe.cfg') -Raw).Trim() -ne "$ExtraFlushes $Suppress") { throw 'Invalid probe cfg' }

if ($PreviousClient -gt 0) {
    $old = Get-CimInstance Win32_Process -Filter "ProcessId=$PreviousClient" -ErrorAction SilentlyContinue
    if ($old) {
        if ($old.Name -ne 'ps2client.exe' -or $old.CommandLine -notmatch '192.168.100.150') { throw 'Unexpected previous client' }
        Stop-Process -Id $PreviousClient
        Start-Sleep -Seconds 2
    }
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location -LiteralPath $bin
try {
    & D:/tyra-editor/tools/ps2client/bin/ps2client.exe -h 192.168.100.150 -t 10 reset *> (Join-Path $out 'reset.log')
    if ($LASTEXITCODE -ne 0) { throw 'Reset command failed' }
    Start-Sleep -Seconds 3
    $proc = Start-Process -FilePath D:/tyra-editor/tools/ps2client/bin/ps2client.exe `
        -ArgumentList '-h', '192.168.100.150', 'execee', 'host:vehicle-playground.elf', '-ps2link' `
        -WorkingDirectory $bin -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $out 'host.log') -RedirectStandardError (Join-Path $out 'host.err')
    [pscustomobject]@{
        arm = $Arm; extraFlushes = $ExtraFlushes; suppressFlush = $Suppress; pid = $proc.Id
        started = (Get-Date).ToString('o')
        elf = (Get-FileHash -LiteralPath (Join-Path $bin 'vehicle-playground.elf')).Hash
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'run.json')
    Write-Output "Launched $Arm (extraFlushes=$ExtraFlushes), ps2client PID $($proc.Id)"
}
finally { Pop-Location }
