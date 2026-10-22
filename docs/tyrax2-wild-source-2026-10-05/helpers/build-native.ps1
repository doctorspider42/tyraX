param([Parameter(Mandatory=$true)][string]$Fixture,[ValidatePattern('^night-ablation-native-v[0-9]+$')][string]$Stem='night-ablation-native-v1')
$ErrorActionPreference='Stop'
$lab='F:/Projects/tyrax2-lab-20261001'
$resolved=(Resolve-Path -LiteralPath $Fixture).Path
if(!$resolved.StartsWith((Resolve-Path $lab).Path+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture must be inside LAB'}
$manifest=Get-Content -LiteralPath "$Fixture/target-source-manifest.json" -Raw | ConvertFrom-Json
if(!$manifest.frozen){throw 'Source not frozen'}
$log="$lab/$Stem.log"; $err="$lab/$Stem.err"
foreach($path in @($log,$err,"$Fixture/root-native-command-exit.json")){if(Test-Path -LiteralPath $path){throw "Unique output required: $path"}}
$parameters=@{Project="$Fixture/game"; Engine="$Fixture/tyra"; Cache="$lab/native-cache"; Toolchain='C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2dev'}
$argsBuild=@('-NoProfile','-ExecutionPolicy','Bypass','-File','F:/Projects/tyra-editor/tools/toolchain/native-build.ps1','-Project',$parameters.Project,'-Engine',$parameters.Engine,'-Cache',$parameters.Cache,'-Toolchain',$parameters.Toolchain)
$child=Start-Process powershell.exe -ArgumentList $argsBuild -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $err -PassThru -Wait
$result=@{status='ROOT_OBSERVED_NATIVE_CHILD_EXIT';processExitCode=$child.ExitCode;parameters=$parameters;pid=$child.Id;buildLog=$log;errorLog=$err;sourceManifestSha256=(Get-FileHash "$Fixture/target-source-manifest.json").Hash.ToLower()}
[IO.File]::WriteAllText("$Fixture/root-native-command-exit.json",($result|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
if($child.ExitCode -ne 0){throw "Native build failed: $($child.ExitCode); preserved"}
if(!(Get-Content -LiteralPath $log -Raw).Contains('[editor] Native build complete:')){throw 'Missing native publication marker'}
Write-Output 'Native build complete; source/assets/symbol provenance remains required.'
