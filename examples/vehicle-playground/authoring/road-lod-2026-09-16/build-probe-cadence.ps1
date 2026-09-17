# A BOUNDING PROBE for S4, the shared reflection probe
# (docs/ee-submission-rearchitecture.md, "Supporting changes").
#
# This does NOT ship and is not a candidate. It halves the shared 128x128
# target's refresh rate - every fourth frame instead of every second - purely
# to price the cadence half of S4 on hardware before anyone designs against it.
# The cadence is already adaptive (`adaptiveReduced ? 3U : 1U`), so the probe
# only forces the reduced branch's constant; nothing else is touched, and in
# particular the retained CAPTURE BASIS is untouched, because Task 5 of the
# Motor District plan is correctness work and must not be undone by a cadence
# experiment.
#
# It is built by patching the generated fixture AFTER --refresh-gen, so the
# editor and the codegen are identical to the arm it is compared against.
[CmdletBinding()]
param(
    [string]$From = 'cand',
    [string]$Arm  = 'probe-cadence4',
    [string]$Root = 'D:/tyra-roadlod-0916'
)
$ErrorActionPreference = 'Stop'
$worktree = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$src = Join-Path $Root "arms/$From"
$dst = Join-Path $Root "arms/$Arm"
if (!(Test-Path -LiteralPath $src)) { throw "No source arm at $src" }
if (Test-Path -LiteralPath $dst) { throw "Archive $dst before re-running this probe" }

Copy-Item -Recurse -Force $src $dst
Remove-Item -Recurse -Force (Join-Path $dst 'bin') -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force (Join-Path $dst 'obj') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $dst 'bin') | Out-Null
foreach ($d in 'aoatlas', 'aomap') {
    $s = Join-Path $src "bin/$d"
    if (Test-Path -LiteralPath $s) { Copy-Item -Recurse -Force $s (Join-Path $dst "bin/$d") }
}

$gen = Join-Path $dst 'src/terrain_game.cpp'
$text = [IO.File]::ReadAllText($gen)
$old = '(adaptiveReduced ? 3U : 1U)'
$new = '(adaptiveReduced ? 7U : 3U)'
if ($text.IndexOf($old) -lt 0) { throw "The probe cadence literal moved; re-aim this probe" }
$text = $text.Replace($old, $new)
[IO.File]::WriteAllText($gen, $text)

& (Join-Path $worktree 'tools/toolchain/native-build.ps1') `
    -Project $dst -Engine (Join-Path $worktree 'vendor/tyra') `
    -Cache (Join-Path $env:LOCALAPPDATA 'tyra-editor/native-build/roadlod-0916') `
    -Toolchain (Join-Path $env:LOCALAPPDATA 'tyra-editor/toolchain/ps2dev')
if ($LASTEXITCODE -ne 0) { throw 'native-build failed' }

$elf = Join-Path $dst 'bin/vehicle-playground.elf'
$hash = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
[pscustomobject]@{
    arm = $Arm; derivedFrom = $From; probe = 'shared reflection probe every 4th frame'
    elfSha256 = $hash; built = (Get-Date).ToString('o')
    commit = (& git -C $worktree rev-parse HEAD)
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dst 'ARM.json')
Write-Output "ELF sha256 $hash"
