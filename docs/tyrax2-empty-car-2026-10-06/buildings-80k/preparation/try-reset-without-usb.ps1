$ErrorActionPreference='Stop'
$resetRun='F:/Projects/tyrax2-lab-20261001/tyrax2-reset-without-usb-v1'
if(Test-Path -LiteralPath $resetRun){throw 'Refuse reset evidence overwrite'}
$priorOwner=Get-Content F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-ps2-two-cars-light-buildings-v1/owned-launch.json | ConvertFrom-Json
$priorProc=Get-Process -Id $priorOwner.pid -ErrorAction Stop
if($priorProc.Path -ne $priorOwner.executable -or $priorProc.StartTime.ToUniversalTime().Ticks -ne ([datetime]$priorOwner.started).ToUniversalTime().Ticks){throw 'Prior client identity differs'}
$allClients=@(Get-CimInstance Win32_Process -Filter "name='ps2client.exe'")
if($allClients.Count -ne 1 -or $allClients[0].ProcessId -ne $priorOwner.pid){throw 'Foreign console owner'}
New-Item -ItemType Directory -Path $resetRun | Out-Null
$resetProc=Start-Process -FilePath $priorOwner.executable -ArgumentList @('-h','192.168.100.150','-t','10','reset') -WorkingDirectory $priorOwner.bin -WindowStyle Hidden -RedirectStandardOutput (Join-Path $resetRun 'reset.stdout') -RedirectStandardError (Join-Path $resetRun 'reset.stderr') -PassThru
$resetStart=$resetProc.StartTime.ToUniversalTime()
$done=$resetProc.WaitForExit(15000)
if(!$done){$resetCheck=Get-Process -Id $resetProc.Id;if($resetCheck.Path -ne $priorOwner.executable -or $resetCheck.StartTime.ToUniversalTime().Ticks -ne $resetStart.Ticks){throw 'Reset client identity changed'};Stop-Process -Id $resetProc.Id}
$priorCheck=Get-Process -Id $priorOwner.pid -ErrorAction SilentlyContinue
if($priorCheck){if($priorCheck.Path -ne $priorOwner.executable -or $priorCheck.StartTime.ToUniversalTime().Ticks -ne ([datetime]$priorOwner.started).ToUniversalTime().Ticks){throw 'Prior identity changed after reset'};Stop-Process -Id $priorCheck.Id}
@{status='BOUNDED_STANDARD_NETWORK_RESET_ATTEMPT_NOT_FRESH_ELF_QUALIFICATION';usbRemovedUserReported=$true;previousClient=$priorOwner;resetClientPid=$resetProc.Id;resetClientStarted=$resetStart.ToString('o');executable=$priorOwner.executable;args=@('-h','192.168.100.150','-t','10','reset');exitedWithin15Seconds=$done;freshElfQualified=$false;USBFailureCauseEstablished=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resetRun 'attempt.json') -Encoding utf8
Write-Output "Reset attempt completed; clientExitedWithinBound=$done; fresh ELF still required"
