param([Parameter(Mandatory=$true)][string]$RunPath)
$ErrorActionPreference='Stop'
$owned=Get-Content -LiteralPath (Join-Path $RunPath 'owned-launch.json') | ConvertFrom-Json
$proc=Get-Process -Id $owned.pid -ErrorAction SilentlyContinue
if($proc){if($proc.Path -ne $owned.executable -or $proc.StartTime.ToUniversalTime().Ticks -ne ([datetime]$owned.started).ToUniversalTime().Ticks){throw 'Ownership mismatch'};Stop-Process -Id $owned.pid}
if(@(Get-CimInstance Win32_Process -Filter "name='ps2client.exe'").Count){throw 'Foreign client active'}
$log=Join-Path $RunPath 'postrun-reset.stdout';if(Test-Path $log){throw 'Refuse overwrite'}
$reset=Start-Process -FilePath $owned.executable -ArgumentList @('-h','192.168.100.150','-t','10','reset') -WorkingDirectory $owned.bin -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError (Join-Path $RunPath 'postrun-reset.stderr') -PassThru
$reset.WaitForExit(12000) | Out-Null
if(!$reset.HasExited){if($reset.Path -eq $owned.executable){Stop-Process -Id $reset.Id};throw 'Reset client did not finish'}
@{previousClient=$owned.pid;resetClient=$reset.Id;resetExitCode=$reset.ExitCode;noReadinessClaim=$true} | ConvertTo-Json | Set-Content (Join-Path $RunPath 'owned-stop-reset.json') -Encoding utf8
if((Get-Content $log -Raw) -match 'DMA Busy'){throw 'DMA Busy during reset'}
Write-Output 'Reset request finished; fresh game start still required'
