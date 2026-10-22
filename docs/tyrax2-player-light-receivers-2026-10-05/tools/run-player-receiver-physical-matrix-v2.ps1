$ErrorActionPreference='Stop'
$taskLab='F:/Projects/tyrax2-lab-20261001'
foreach($taskKind in @(10,11)){
 foreach($taskOrder in @(0,1)){
  $taskStem="night-ablation-ps2-player-receivers-kind$taskKind-order$taskOrder-20261005"
  & "$taskLab/start-player-receivers-physical-root-v2.ps1" -Stem $taskStem -Order $taskOrder -Kind $taskKind
  $taskStartupDeadline=(Get-Date).AddSeconds(25)
  while(!(Get-Content -Raw "$taskLab/$taskStem.log").Contains("LOG: NIGHTPLAN schema=1 kind=$taskKind order=$taskOrder ")){
   if((Get-Date) -gt $taskStartupDeadline){throw 'Fresh ELF startup absent; preserve exact attempt for diagnosis'}
   Start-Sleep -Milliseconds 200
  }
  & "$taskLab/wait-player-receivers-physical-root-v2.ps1" -Stem $taskStem -Order $taskOrder -Kind $taskKind
 }
}
Write-Output 'Completed both receiver contrasts in both physical orders'
