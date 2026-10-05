param([Parameter(Mandatory=$true)][string]$Stem,[Parameter(Mandatory=$true)][ValidateSet(0,1)][int]$Order,[Parameter(Mandatory=$true)][ValidateSet(10,11)][int]$Kind)
$ErrorActionPreference='Stop'
$taskLab='F:/Projects/tyrax2-lab-20261001'
$taskDeadline=(Get-Date).AddSeconds(300)
$taskLastPhase=-1
while((Get-Date) -lt $taskDeadline){
 $taskText=Get-Content -Raw -LiteralPath "$taskLab/$Stem.log"
 $taskPhases=[regex]::Matches($taskText,'LOG: NIGHTPHASE phase=(\d+) first=')
 if($taskPhases.Count){$taskCurrent=[int]$taskPhases[$taskPhases.Count-1].Groups[1].Value; if($taskCurrent -ne $taskLastPhase){Write-Output "Observed physical phase $taskCurrent";$taskLastPhase=$taskCurrent}}
 if($taskText.Contains("LOG: NIGHTDONE order=$Order valid=1 loops=5400")){
  $taskFinish=if($Kind -eq 11 -and $Order -eq 1){'finish-physical-no-reset.ps1'}else{'finish-physical.ps1'}
  & "$taskLab/player-light-receivers-root-helpers-v2/$taskFinish" -Stem $Stem
  Write-Output 'Physical run accepted and preserved'
  exit 0
 }
 Start-Sleep -Seconds 1
}
throw 'Incomplete physical test after300s; retain exact client/live evidence for diagnosis'
