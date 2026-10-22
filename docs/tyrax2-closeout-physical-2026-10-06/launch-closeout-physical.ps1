param([int]$Kind=0,[int]$Order=0,[Parameter(Mandatory=$true)][string]$RunName,[string]$SourceBin='F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-ps2-v4-kind0-order0-v1/bin',[string]$ElfName='vehicle-playground')
$ErrorActionPreference='Stop'
if($Kind -notin 0,1,2,3 -or $Order -notin 0,1 -or $RunName -notmatch '^tyrax2-closeout-[a-z0-9-]+$'){throw 'Invalid run inputs'}
$clients=@(Get-CimInstance Win32_Process -Filter "name='ps2client.exe'")
if($clients.Count){throw 'Existing console client; ownership must be resolved first'}
$labPath='F:/Projects/tyrax2-lab-20261001'
$sourcePath=(Resolve-Path -LiteralPath $SourceBin).Path
if(!$sourcePath.StartsWith((Resolve-Path $labPath).Path+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Source outside lab'}
$runPath=Join-Path $labPath $RunName
if(Test-Path -LiteralPath $runPath){throw 'Refuse evidence overwrite'}
$binPath=Join-Path $runPath 'bin'
New-Item -ItemType Directory -Path $binPath | Out-Null
Get-ChildItem -LiteralPath $sourcePath | Where-Object {$_.Name -notin @('closeout-samples.txt','log.txt','closeout.cfg','ps2link.run')} | Copy-Item -Destination $binPath -Recurse
[IO.File]::WriteAllText((Join-Path $binPath 'closeout.cfg'),"$Order $Kind`n",[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $binPath 'ps2link.run'),'ps2link',[Text.Encoding]::ASCII)
$elfPath=Join-Path $binPath ($ElfName+'.elf')
if(!(Test-Path $elfPath)){throw 'Missing ELF'}
$pins=@(Get-ChildItem -LiteralPath $binPath -File -Recurse | ForEach-Object {@{name=[IO.Path]::GetRelativePath($binPath,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()}})
@{kind=$Kind;order=$Order;sourceBin=$sourcePath;bin=$binPath;files=$pins;sourceHead='5b6cd898b05fbbdcc11466bfc8a1911a9c285d4b';productionCommentOnlyFollowup='ac2c52f2c';hardwarePerformanceQualified=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runPath 'launch-inputs.json') -Encoding utf8
$clientPath=(Resolve-Path 'F:/Projects/tyra-editor/tools/ps2client/bin/ps2client.exe').Path
$proc=Start-Process -FilePath $clientPath -ArgumentList @('-h','192.168.100.150','execee',('host:'+$ElfName+'.elf')) -WorkingDirectory $binPath -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runPath 'ps2client.stdout') -RedirectStandardError (Join-Path $runPath 'ps2client.stderr') -PassThru
@{pid=$proc.Id;started=$proc.StartTime.ToUniversalTime().ToString('o');executable=$clientPath;bin=$binPath;elf=$ElfName;run=$runPath} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runPath 'owned-launch.json') -Encoding utf8
Write-Output "Started $RunName PID $($proc.Id)"
