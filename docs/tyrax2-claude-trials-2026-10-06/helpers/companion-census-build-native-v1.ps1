# ROOT ONLY: real native build; preparation agent must not invoke.
$ErrorActionPreference='Stop'
$lab='F:/Projects/tyrax2-lab-20261001'; $fixture="$lab/companion-census-physical-v1"
$manifest=Get-Content -Raw -LiteralPath "$fixture/target-source-manifest.json" | ConvertFrom-Json
if(!$manifest.frozen){throw 'Freeze V10B actual inputs first'}
$log="$lab/night-ablation-native-v52.log"; $err="$lab/night-ablation-native-v52.err"; $result="$fixture/root-native-command-exit.json"
foreach($path in @($log,$err,$result)){if(Test-Path -LiteralPath $path){throw "Unique attempt outputs required: $path"}}
$parameters=@{Project="$fixture/game";Engine="$fixture/tyra";Cache="$lab/native-cache";Toolchain='C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2dev'}
$builder='F:/Projects/tyra-editor/tools/toolchain/native-build.ps1'
$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$builder,'-Project',$parameters.Project,'-Engine',$parameters.Engine,'-Cache',$parameters.Cache,'-Toolchain',$parameters.Toolchain)
$child=Start-Process -FilePath powershell.exe -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $err -PassThru -Wait
$exit=$child.ExitCode
@{status='ROOT_observed_native_child_process_completion';processExitCode=$exit;actualCommandParameters=$parameters;buildLog=$log;errorLog=$err;childProcessId=$child.Id;sourceManifestSha256=(Get-FileHash -Algorithm SHA256 -LiteralPath "$fixture/target-source-manifest.json").Hash.ToLowerInvariant()} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $result -Encoding utf8
if($exit -ne 0){throw "Native child exited $exit; failed attempt preserved"}
$text=Get-Content -Raw -LiteralPath $log
if(!$text.Contains('[editor] Native build complete:')){throw 'Exit0 alone is insufficient: native publication marker missing; attempt preserved'}
foreach($file in @('vehicle-playground.elf','vehicle-playground.elf.sym')){if(!(Test-Path -LiteralPath "$fixture/game/bin/$file")){throw "Missing actual output: $file"}}
Write-Output 'Native child completed; use finish-native.ps1 for strict source/assets/provenance verification.'
