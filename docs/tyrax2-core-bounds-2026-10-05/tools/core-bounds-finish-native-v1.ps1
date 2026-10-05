# ROOT ONLY: restore/bind actual native outputs after inspected successful build.
$ErrorActionPreference='Stop'
$lab='F:/Projects/tyrax2-lab-20261001';$wlab='/mnt/f/Projects/tyrax2-lab-20261001';$fixture="$lab/core-bounds-partition-physical-v1"
$result=Get-Content -Raw -LiteralPath "$fixture/root-native-command-exit.json" | ConvertFrom-Json
if($result.processExitCode -ne 0){throw 'Actual child build failure'}
if($result.sourceManifestSha256 -ne (Get-FileHash -Algorithm SHA256 -LiteralPath "$fixture/target-source-manifest.json").Hash.ToLowerInvariant()){throw 'Manifest changed after actual build'}
& wsl -d Ubuntu -- python3 "$wlab/core-bounds-partition-physical-v1/tools/complete-native-provenance.py" --fixture "$wlab/core-bounds-partition-physical-v1" --build-log "$wlab/night-ablation-native-v34.log" --reported-exit-code 0 --out "$wlab/core-bounds-partition-physical-v1/root-native-provenance.json"
if($LASTEXITCODE -ne 0){throw 'Actual source/assets/native authority rejected; all build evidence preserved'}
