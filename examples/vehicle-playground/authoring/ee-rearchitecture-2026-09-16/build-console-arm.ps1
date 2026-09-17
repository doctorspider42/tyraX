# Build one PHYSICAL-PS2 timing arm of the EE submission rearchitecture.
#
# This is not the PCSX2 harness. A timing arm carries NONE of the instruments:
#
#   - no TYRA_STAPIP_VIFHASH      - the gate folds 2.7 MB a frame
#   - no TYRA_STAPIP_BAKED_VERIFY - it rebuilds every block instead of replaying
#   - no TYRA_STAPIP_BAKED_POISON - it memsets every eviction
#   - no TYRA_STAPIP_BAKED_REPORT - a host: write with a period lands inside the
#                                   sampling window, and over ps2link every one
#                                   is a network round trip
#   - no TYRA_FRAME_PROFILE       - same reason
#
# A gate arm and a verify arm are correctness arms. Neither may produce a
# millisecond, and running the candidate WITH verify on would measure the
# rebuild rather than the replay. The only knob that differs between these two
# arms is TYRA_STAPIP_BAKED_STREAM.
#
# Built with tools/toolchain/native-build.ps1 through the editor's `--build`,
# never with the instrumentation regenerated away: --refresh-gen runs FIRST,
# then instrument-frame-cost.py patches the fixture's generated
# src/terrain_game.cpp, then the build.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('ctl','cand')][string]$Arm,
  [string]$Root = "$env:TEMP\tyra-editor-test\eeR2con"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$editor = Join-Path $repo 'build\tyrax-editor.exe'
if (-not (Test-Path $editor)) { throw 'Build the editor from THIS worktree first: ./build.ps1' }
$fx = Join-Path $Root "arms\$Arm"

$qb = Join-Path $repo 'vendor\tyra\engine\inc\renderer\3d\pipeline\static\core\stapip_qbuffer_renderer.hpp'
$vh = Join-Path $repo 'vendor\tyra\engine\inc\renderer\3d\pipeline\static\core\stapip_vif_hash.hpp'
$fp = Join-Path $repo 'vendor\tyra\engine\inc\debug\frame_profile.hpp'
function Set-Switch([string]$File, [string]$Name, [int]$Value) {
  $t = Get-Content -Raw -LiteralPath $File
  $new = $t -replace "(?m)^#define $Name \d+$", "#define $Name $Value"
  if ($new -eq $t -and $t -notmatch "(?m)^#define $Name $Value$") { throw "Could not set $Name" }
  Set-Content -LiteralPath $File -Value $new -NoNewline
}
$baked = if ($Arm -eq 'cand') { 1 } else { 0 }
foreach ($n in 'TYRA_STAPIP_BAKED_REPORT','TYRA_STAPIP_BAKED_VERIFY','TYRA_STAPIP_BAKED_POISON') {
  Set-Switch $qb $n 0
}
Set-Switch $qb 'TYRA_STAPIP_BAKED_STREAM' $baked
Set-Switch $vh 'TYRA_STAPIP_VIFHASH' 0
Set-Switch $fp 'TYRA_FRAME_PROFILE' 0
Write-Output "[$Arm] BAKED_STREAM=$baked, every instrument OFF"

& $editor --refresh-gen $fx 2>&1 | Select-Object -Last 2
if ($LASTEXITCODE -ne 0) { throw '--refresh-gen failed' }
python (Join-Path $repo 'examples\vehicle-playground\authoring\instrument-frame-cost.py') $fx 2>&1 | Select-Object -Last 2
if ($LASTEXITCODE -ne 0) { throw 'instrument-frame-cost.py failed' }
# NATIVE BUILD DIRECTLY, never `--build`. An editor --build runs its own
# --refresh-gen and REGENERATES THE INSTRUMENTATION AWAY - the game then boots,
# runs its 1440 frames, writes district-benchmark.csv and never writes
# frame-cost.csv at all, which reads as a dead console rather than as a build
# mistake. Cost one run here; ee-probes-2026-09-16/build-arm.ps1 says so too.
& (Join-Path $repo 'tools\toolchain\native-build.ps1') `
    -Project $fx -Engine (Join-Path $repo 'vendor\tyra') `
    -Cache (Join-Path $env:LOCALAPPDATA "tyra-editor\native-build\eeR2con-$Arm") `
    -Toolchain (Join-Path $env:LOCALAPPDATA 'tyra-editor\toolchain\ps2dev') 2>&1 |
  Select-Object -Last 4
if ($LASTEXITCODE -ne 0) { throw 'native-build failed' }

# vendor/tyra is LF ONLY (.gitattributes; vclpp chokes on CRLF) and a PowerShell
# write is exactly the kind that reintroduces them.
foreach ($f in $qb, $vh, $fp) {
  if ([IO.File]::ReadAllBytes($f) -contains 13) { throw "CRLF crept into $f" }
}

$elf = Join-Path $fx 'bin\vehicle-playground.elf'
if (-not (Test-Path $elf)) { throw "[$Arm] no ELF - the build failed" }
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $elf).Hash
$run = (Select-String -LiteralPath (Join-Path $fx 'src\terrain_game.cpp') -Pattern 'stripRun = 7\d' | Select-Object -First 1).Line.Trim()
Write-Output "[$Arm] elf sha256 $sha"
Write-Output "[$Arm] $run   (must be 75u)"
@{ arm=$Arm; bakedStream=$baked; elfSha256=$sha; stripRun=$run;
   commit=(git -C $repo rev-parse HEAD); built=(Get-Date -Format o) } |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fx 'ARM.json')
