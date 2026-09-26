$ErrorActionPreference='Stop'
$traceRoot='D:/tyra-hw-profiler-0914'
$tracePython='C:/Users/papaj/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$tracePrevious=29932
foreach ($traceArm in @('scissor/game','unlit/game','control-repeat')) {
 $traceSlug=$traceArm.Replace('/','-')
 if ($traceArm -eq 'control-repeat') {
  & "$traceRoot/run-arm.ps1" -Arm $traceArm -PreviousClient $tracePrevious -TraceFrames 12 -NoStates
 } else {
  & "$traceRoot/run-arm.ps1" -Arm $traceArm -PreviousClient $tracePrevious
 }
 $traceStarted=Get-Date
 $traceCsv=Join-Path $traceRoot "$traceArm/bin/hardware-trace.csv"
 do {
  if (((Get-Date)-$traceStarted).TotalSeconds -gt 240) { throw "Timed out: $traceArm" }
  Start-Sleep -Seconds 3
  $traceReady=(Test-Path -LiteralPath $traceCsv) -and ((Get-Content -LiteralPath $traceCsv -Tail 1) -match '^END,')
 } until ($traceReady)
 & $tracePython D:/tyra-editor/tools/hardware-trace.py export $traceCsv -o (Join-Path $traceRoot "$traceSlug-timeline")
 if ($LASTEXITCODE -ne 0) { throw "Invalid trace: $traceArm" }
 $traceRows=Import-Csv -LiteralPath (Join-Path $traceRoot "$traceArm/bin/frame-cost.csv")
 if ($traceRows.Count -ne 960) { throw "Incomplete timing: $traceArm" }
 & D:/tyra-editor/build/tyrax-editor.exe --capture-frame (Join-Path $traceRoot $traceArm) -o (Join-Path $traceRoot "$traceSlug-garage.png")
 if ($LASTEXITCODE -ne 0) { throw "Capture failed: $traceArm" }
 Write-Output "Completed physical PS2: $traceArm (960 timing rows, full trace)"
 $tracePrevious=(Get-Content -LiteralPath (Join-Path $traceRoot "run-$traceSlug.json") -Raw | ConvertFrom-Json).pid
}
