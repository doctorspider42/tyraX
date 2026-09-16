# Build one arm of the EE-submission-rearchitecture A/B.
#
# ONE worktree, ONE project directory, ONE knob: TYRA_STAPIP_BAKED_STREAM, 0
# against 1. Both arms additionally carry, identically, the measurement flips -
# TYRA_FRAME_PROFILE (the FTCLIP line), TYRA_STAPIP_BAKED_REPORT (STAPIPBAKE /
# STAPIPMISS and chainQw, which is compiled into the CONTROL too or there is
# nothing to compare) and TYRA_STAPIP_VIFHASH (the acceptance gate, leg 1).
#
# A GATE ARM IS NEVER A TIMING ARM. The hash fold reads every payload quadword
# the frame submits, so a build from this script runs at roughly 15 Hz. It
# produces counts, hashes and pixels. Never a millisecond.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('ctl','cand')][string]$Arm,
  [string]$Fx  = "$env:TEMP\tyra-editor-test\eeR2",
  [string]$Out = "$env:TEMP\tyra-editor-test\eeR2-arms",
  [switch]$NoHash
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$eng  = Join-Path $root 'vendor\tyra\engine\inc'
$editor = Join-Path $root 'build\tyrax-editor.exe'
if (-not (Test-Path $editor)) { throw "Build the editor from THIS worktree first: ./build.ps1" }

$qb   = Join-Path $eng 'renderer\3d\pipeline\static\core\stapip_qbuffer_renderer.hpp'
$vh   = Join-Path $eng 'renderer\3d\pipeline\static\core\stapip_vif_hash.hpp'
$fp   = Join-Path $eng 'debug\frame_profile.hpp'

function Set-Switch([string]$File, [string]$Name, [int]$Value) {
  $t = Get-Content -Raw -LiteralPath $File
  $new = $t -replace "(?m)^#define $Name \d+$", "#define $Name $Value"
  if ($new -eq $t -and $t -notmatch "(?m)^#define $Name $Value$") {
    throw "Could not set $Name in $File"
  }
  Set-Content -NoNewline -LiteralPath $File -Value $new
}

$baked = if ($Arm -eq 'cand') { 1 } else { 0 }
$hash  = if ($NoHash) { 0 } else { 1 }
Set-Switch $qb 'TYRA_STAPIP_BAKED_STREAM' $baked
Set-Switch $qb 'TYRA_STAPIP_BAKED_REPORT' 1
Set-Switch $vh 'TYRA_STAPIP_VIFHASH'      $hash
Set-Switch $fp 'TYRA_FRAME_PROFILE'       1
Write-Output "[$Arm] BAKED_STREAM=$baked BAKED_REPORT=1 VIFHASH=$hash FRAME_PROFILE=1"

# --refresh-gen BEFORE the build, with the editor built from this worktree. An
# editor binary sitting in build/ is not the editor at the tree's commit, and
# --refresh-gen regenerates faithfully with a stale baker - that is the trap
# that made a whole round of round-one evidence describe a 72-run scene.
& $editor --refresh-gen $Fx 2>&1 | Select-Object -Last 3
& $editor --build $Fx 2>&1 | Select-Object -Last 12

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$elf = Join-Path $Fx 'bin\vehicle-playground.elf'
if (-not (Test-Path $elf)) { throw "[$Arm] no ELF - the build failed" }
Copy-Item -LiteralPath $elf -Destination (Join-Path $Out "$Arm.elf") -Force
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $elf).Hash
Write-Output "[$Arm] elf sha256 $sha"

# THE FIXTURE CHECK. A matching capture hash is a PICTURE check and never a
# FIXTURE check; the baked constant is.
$gen = Join-Path $Fx 'src\terrain_game.cpp'
$run = (Select-String -LiteralPath $gen -Pattern 'stripRun = 7\d' | Select-Object -First 1).Line.Trim()
Write-Output "[$Arm] $run   (must be 75u)"

@{ arm=$Arm; bakedStream=$baked; vifHash=$hash; elfSha256=$sha; stripRun=$run;
   commit=(git -C $root rev-parse HEAD); built=(Get-Date -Format o) } |
  ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Out "$Arm.json")
Write-Output "[$Arm] done"
