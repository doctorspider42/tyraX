param([Parameter(Mandatory=$true)][string]$Stem,[Parameter(Mandatory=$true)][ValidateSet(0,1)][int]$Order)
$ErrorActionPreference='Stop'
$taskLab='F:/Projects/tyrax2-lab-20261001'
if(Get-CimInstance Win32_Process -Filter "name='ps2client.exe'"){throw 'A PS2 client already exists; inspect exact ownership before launch'}
& python "$taskLab/spot-result-root-helpers-v1/prepare-physical.py" --stem $Stem --kind 23 --order $Order
if($LASTEXITCODE -ne 0){throw 'Preparation rejected'}
$taskLauncherOut="$taskLab/$Stem-launcher.log"
$taskLauncherErr="$taskLab/$Stem-launcher.err"
if((Test-Path $taskLauncherOut) -or (Test-Path $taskLauncherErr)){throw 'Unique launcher outputs required'}
$taskLauncher=Start-Process -FilePath (Get-Command python).Source -ArgumentList @("$taskLab/spot-result-root-helpers-v1/launch-physical-observed.py",'--stem',$Stem) -WindowStyle Hidden -RedirectStandardOutput $taskLauncherOut -RedirectStandardError $taskLauncherErr -PassThru
$taskLaunch="$taskLab/$Stem-launch"
$taskDeadline=(Get-Date).AddSeconds(15)
while(!(Test-Path "$taskLaunch/observed-launch.json")){
 if((Get-Date) -gt $taskDeadline){throw 'No observed launch; preserve attempt'}
 Start-Sleep -Milliseconds 100
}
$taskObserved=Get-Content -Raw "$taskLaunch/observed-launch.json" | ConvertFrom-Json
$taskClient=Get-CimInstance Win32_Process -Filter "ProcessId=$($taskObserved.pid)"
$taskExe=(Resolve-Path F:/Projects/tyra-editor/tools/ps2client/bin/ps2client.exe).Path
if(!$taskClient -or $taskClient.ExecutablePath -ne $taskExe -or !$taskClient.CommandLine.Contains('execee') -or !$taskClient.CommandLine.Contains('host:vehicle-playground.elf')){throw 'Actual client identity mismatch'}
$taskClient | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json | Set-Content "$taskLaunch/client-process.json"
@{pid=$taskLauncher.Id; clientPid=$taskClient.ProcessId; helper='start-lattice-physical-root-v1.ps1'} | ConvertTo-Json | Set-Content "$taskLaunch/root-launcher-process.json"
Write-Output "Owned physical client $($taskClient.ProcessId), launcher $($taskLauncher.Id)"
