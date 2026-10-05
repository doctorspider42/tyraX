param([Parameter(Mandatory=$true)][ValidatePattern('^night-ablation-ps2-[a-z0-9-]+$')][string]$Stem)
$ErrorActionPreference='Stop'
$taskLab='F:/Projects/tyrax2-lab-20261001'
$taskLaunch="$taskLab/$Stem-launch"
$taskPlan=Get-Content -Raw "$taskLaunch/owned-launch.json" | ConvertFrom-Json
$taskRaw="$taskLab/$Stem.log"
$taskText=Get-Content -Raw -LiteralPath $taskRaw
if(!$taskText.Contains("NIGHTDONE order=$($taskPlan.order) valid=1 loops=5400")){throw 'Physical run incomplete; preserve live inputs'}
$taskOwned=Get-Content -Raw "$taskLaunch/client-process.json" | ConvertFrom-Json
$taskProc=Get-CimInstance Win32_Process -Filter "ProcessId=$($taskOwned.ProcessId)"
$taskExe=(Resolve-Path 'F:/Projects/tyra-editor/tools/ps2client/bin/ps2client.exe').Path
if(!$taskProc -or $taskProc.ExecutablePath -ne $taskExe -or $taskProc.CommandLine -ne $taskOwned.CommandLine){throw 'Exact owned client identity mismatch'}
if(Test-Path "$taskLaunch/owned-stop.json"){throw 'Stop record already exists'}
$taskProc | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json | Set-Content "$taskLaunch/owned-stop.json"
Stop-Process -Id $taskOwned.ProcessId
if(Test-Path "$taskLaunch/observed-launch.json"){
  $taskDeadline=(Get-Date).AddSeconds(5)
  $taskClosed=$false
  while((Get-Date) -lt $taskDeadline){
    $taskLast=Get-Content "$taskLaunch/host-arrivals.jsonl" -Tail 1 | ConvertFrom-Json
    if($taskLast.event -eq 'client-eof'){$taskClosed=$true;break}
    Start-Sleep -Milliseconds 100
  }
  if(!$taskClosed){throw 'Owned host arrival stream not closed; preserve evidence'}
}
& python "$taskLab/wild-pool-table-runtime-tools-v3/normalize-physical.py" $taskRaw "$taskLab/$Stem-utf8.log" --proof "$taskLab/$Stem-normalization.json"
if($LASTEXITCODE -ne 0){throw 'Normalization rejected; raw preserved'}
$taskArtifact="$($taskPlan.fixture)/game/bin/night-ablation.log"
& python "$taskLab/corona24-pricing-root-helpers-v1/freeze-completed.py" --launch "$taskLaunch/owned-launch.json" --stdout "$taskLab/$Stem-utf8.log" --artifact $taskArtifact --environment ps2 --raw-stdout $taskRaw --normalization-proof "$taskLab/$Stem-normalization.json" --out "$taskLab/$Stem-evidence"
if($LASTEXITCODE -ne 0){throw 'Strict runtime rejected; preserve live artifact'}
if((Get-FileHash $taskArtifact).Hash -ne (Get-FileHash "$taskLab/$Stem-evidence/night-ablation.log").Hash){throw 'Artifact archive drift'}
Remove-Item -LiteralPath $taskArtifact
$taskResetStem="$taskLab/$Stem-postrun-reset"
if(Test-Path "$taskResetStem.log"){throw 'Reset log already exists'}
$taskReset=Start-Process -FilePath $taskExe -ArgumentList @('-h','192.168.100.150','reset') -WindowStyle Hidden -RedirectStandardOutput "$taskResetStem.log" -RedirectStandardError "$taskResetStem.err" -PassThru
$taskReset.Id | Set-Content "$taskResetStem.pid"
Start-Sleep -Seconds 10
Write-Output "Physical evidence accepted and archived; reset sent PID $($taskReset.Id)"
