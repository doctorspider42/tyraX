$ErrorActionPreference='Stop'
$traceRoot='D:/tyra-hw-profiler-0914'
$tracePython='C:/Users/papaj/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$traceArms=@('control','no-host','16bpp','no-extra-passes')
for ($traceIndex=0; $traceIndex -lt $traceArms.Count; $traceIndex++) {
 $traceArm=$traceArms[$traceIndex]
 $traceStarted=Get-Date
 $traceCsv=Join-Path $traceRoot "$traceArm/bin/hardware-trace.csv"
 do {
  if (((Get-Date)-$traceStarted).TotalSeconds -gt 240) { throw "Timed out: $traceArm" }
  Start-Sleep -Seconds 3
  $traceReady=(Test-Path -LiteralPath $traceCsv) -and ((Get-Content -LiteralPath $traceCsv -Tail 1) -match '^END,')
 } until ($traceReady)
 & $tracePython D:/tyra-editor/tools/hardware-trace.py export $traceCsv -o (Join-Path $traceRoot "$traceArm-timeline")
 if ($LASTEXITCODE -ne 0) { throw "Invalid trace: $traceArm" }
 $traceRows=Import-Csv -LiteralPath (Join-Path $traceRoot "$traceArm/bin/frame-cost.csv")
 if ($traceRows.Count -ne 960) { throw "Incomplete timing: $traceArm" }
 if ($traceArm -ne 'no-host') {
  & D:/tyra-editor/build/tyrax-editor.exe --capture-frame (Join-Path $traceRoot $traceArm) -o (Join-Path $traceRoot "$traceArm-garage.png")
  if ($LASTEXITCODE -ne 0) { throw "Capture failed: $traceArm" }
 }
 Write-Output "Completed physical PS2: $traceArm (960 timing rows, full trace)"
 if ($traceIndex+1 -lt $traceArms.Count) {
  $traceClient=(Get-Content -LiteralPath (Join-Path $traceRoot "run-$traceArm.json") -Raw | ConvertFrom-Json).pid
  & (Join-Path $traceRoot 'run-arm.ps1') -Arm $traceArms[$traceIndex+1] -PreviousClient $traceClient
 }
}
