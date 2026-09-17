param([Parameter(Mandatory=$true)][string]$Arm, [int]$PreviousClient = 0, [int]$TraceStart = 1500, [int]$TraceFrames = 4, [switch]$NoStates)
$ErrorActionPreference = 'Stop'
$traceRoot = 'D:/tyra-hw-profiler-0914'
$traceSlug = $Arm.Replace('/','-')
$traceBin = Join-Path (Join-Path $traceRoot $Arm) 'bin'
if (!(Test-Path -LiteralPath (Join-Path $traceBin 'vehicle-playground.elf'))) { throw 'No ELF' }
if (Test-Path -LiteralPath (Join-Path $traceBin 'frame-cost.csv')) { throw 'Archive old CSV before a new measurement' }
Set-Content -LiteralPath (Join-Path $traceBin 'ps2link.run') -Value 'ps2link' -Encoding ascii
if ((Get-Content -LiteralPath (Join-Path $traceBin 'ps2link.run') -Raw).Trim() -ne 'ps2link') { throw 'Invalid marker' }
if ($TraceFrames -gt 0) {
  $traceStateFlag = if ($NoStates) { 0 } else { 1 }
  Set-Content -LiteralPath (Join-Path $traceBin 'hardware-trace.cfg') -Value "$TraceStart $TraceFrames $traceStateFlag" -Encoding ascii
}
if ($PreviousClient -gt 0) {
  $traceOld = Get-CimInstance Win32_Process -Filter "ProcessId=$PreviousClient"
  if ($traceOld.Name -ne 'ps2client.exe' -or $traceOld.CommandLine -notmatch '192.168.100.150') { throw 'Unexpected previous client' }
  Stop-Process -Id $PreviousClient
}
Push-Location -LiteralPath $traceBin
try {
  & D:/tyra-editor/tools/ps2client/bin/ps2client.exe -h 192.168.100.150 -t 10 reset *> (Join-Path $traceRoot "reset-$traceSlug.log")
  if ($LASTEXITCODE -ne 0) { throw 'Reset command failed' }
  Start-Sleep -Seconds 3
  $traceProc = Start-Process -FilePath D:/tyra-editor/tools/ps2client/bin/ps2client.exe -ArgumentList '-h','192.168.100.150','execee','host:vehicle-playground.elf','-ps2link' -WorkingDirectory $traceBin -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $traceRoot "host-$traceSlug.log") -RedirectStandardError (Join-Path $traceRoot "host-$traceSlug.err")
  [pscustomobject]@{arm=$Arm; pid=$traceProc.Id; started=(Get-Date).ToString('o'); elf=(Get-FileHash -LiteralPath (Join-Path $traceBin 'vehicle-playground.elf')).Hash} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $traceRoot "run-$traceSlug.json")
  Write-Output "Launched $Arm, ps2client PID $($traceProc.Id)"
} finally { Pop-Location }
