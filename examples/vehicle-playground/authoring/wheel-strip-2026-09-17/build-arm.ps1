# Build one arm of the WORST-PACKED-PRODUCER round
# (docs/ee-submission-rearchitecture.md, and the frame inventory in
# ../reflection-probe-2026-09-16/README.md that found the target).
#
# This is a COUNTS fixture, like the inventory round it extends: it drains
# StaPipCore telemetry at every producer boundary, so its own milliseconds are
# meaningless and none are recorded. PCSX2 is enough - its counters are exact
# even though its milliseconds are not.
#
# ONE EDITOR, BOTH ARMS. The strip is baked into the .tmdl and generated into
# the game unconditionally; what an arm sets is the two `#define`s the
# generated game guards the CONSUMERS with, patched into the arm's own
# terrain_game.cpp after --refresh-gen. So the arms share a baker, a generated
# source and an engine, and differ in two tokens - there is no second editor to
# go stale between them, which is the trap the reflection round hit.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Editor,
    [Parameter(Mandatory = $true)][string]$Arm,
    # 1 = the wheel batch concatenates the baked strip; 0 = the triangle list
    # it always concatenated.
    [ValidateSet(0, 1)][int]$StripWheels = 1,
    # 1 = the projected-shadow receiver patch is written as a strip; 0 = list.
    [ValidateSet(0, 1)][int]$StripPatch = 1,
    [ValidateSet('debug', 'quiet-debug', 'release')][string]$Profile = 'quiet-debug',
    # Skip the telemetry instrument: for the PICTURE arms, which need the game
    # to photograph itself rather than count anything.
    [switch]$NoInstrument,
    [string]$Root = 'C:/tyra-probe-0917-strip'
)
$ErrorActionPreference = 'Stop'

$worktree = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$example  = Join-Path $worktree 'examples/vehicle-playground'
$cache    = Join-Path $env:LOCALAPPDATA 'tyra-editor/native-build/strip-0917'
$toolchain= Join-Path $env:LOCALAPPDATA 'tyra-editor/toolchain/ps2dev'
$fixture  = Join-Path $Root "arms/$Arm"

if (!(Test-Path -LiteralPath $Editor)) { throw "No editor at $Editor" }
if (Test-Path -LiteralPath $fixture) { throw "Archive $fixture before re-running this arm" }
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'arms') | Out-Null

# --- 1. the fixture, with the baked asset tree copied in ------------------
& python (Join-Path $example 'authoring/benchmark-district.py') $fixture --profile $Profile
if ($LASTEXITCODE -ne 0) { throw 'benchmark-district.py failed' }
if (!(Test-Path -LiteralPath (Join-Path $example '.res-baked'))) {
    throw "No .res-baked in $example - run `"$Editor --build $example`" once, " +
          'then `git checkout -- examples/vehicle-playground/{inc,res,src}`'
}
Copy-Item -Recurse -Force (Join-Path $example '.res-baked') (Join-Path $fixture '.res-baked')
New-Item -ItemType Directory -Force -Path (Join-Path $fixture 'bin') | Out-Null
foreach ($d in 'aoatlas', 'aomap') {
    $src = Join-Path $example "bin/$d"
    if (Test-Path -LiteralPath $src) { Copy-Item -Recurse -Force $src (Join-Path $fixture "bin/$d") }
}

# --- 2. regenerate, set the arm's two knobs, then instrument --------------
& $Editor --refresh-gen $fixture
if ($LASTEXITCODE -ne 0) { throw '--refresh-gen failed' }

$game = Join-Path $fixture 'src/terrain_game.cpp'
$g = Get-Content -LiteralPath $game -Raw
foreach ($knob in @(@('TYRA_STRIP_WHEELS', $StripWheels),
                    @('TYRA_STRIP_PROJ_PATCH', $StripPatch))) {
    $pattern = '#define ' + $knob[0] + ' 1'
    if ($g -notmatch [regex]::Escape($pattern)) {
        throw ($knob[0] + ' default is not 1 in the generated game - the arms ' +
               'cannot be set from here any more')
    }
    $g = $g.Replace($pattern, ('#define ' + $knob[0] + ' ' + $knob[1]))
}
Set-Content -LiteralPath $game -Value $g -NoNewline

if (-not $NoInstrument) {
    & python (Join-Path $example 'authoring/inventory-frame.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'inventory-frame.py failed' }
}

# --- 3. FIXTURE IDENTITY, before anything is compiled --------------------
# A matching capture hash is a PICTURE check and never a fixture check: a strip
# changes how a surface is cut into runs, not which pixels it covers. These
# lines are functions of the baker and of the arm, and they do catch it.
$gen = Get-ChildItem -LiteralPath (Join-Path $fixture 'src') -Recurse -Filter '*.cpp' |
       Select-String -Pattern 'stripRun\s*=\s*(\d+)u' -AllMatches
$runs = ($gen.Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) -join ','
$knobs = (Select-String -LiteralPath $game -Pattern '#define TYRA_STRIP_\w+ \d').Matches.Value -join '; '
Write-Output "stripRun in generated source: $runs (expect 75 at the branch tip)"
Write-Output "arm knobs: $knobs"
# ...and what the WHEEL .tmdl actually carries, which is the half of this
# change that lives in an asset rather than in code.
$wheelInfo = & python (Join-Path $PSScriptRoot 'wheel-strip-report.py') `
                      (Join-Path $fixture '.res-baked/vehicles')
Write-Output $wheelInfo

# --- 4. compile with the native toolchain directly -----------------------
# Never an editor --build: it would regenerate the instrumentation away.
& (Join-Path $worktree 'tools/toolchain/native-build.ps1') `
    -Project $fixture -Engine (Join-Path $worktree 'vendor/tyra') `
    -Cache $cache -Toolchain $toolchain
if ($LASTEXITCODE -ne 0) { throw 'native-build failed' }

$elf = Join-Path $fixture 'bin/vehicle-playground.elf'
if (!(Test-Path -LiteralPath $elf)) { throw 'No ELF produced' }
[pscustomobject]@{
    arm = $Arm; profile = $Profile; stripRun = $runs
    stripWheels = $StripWheels; stripProjPatch = $StripPatch
    wheelTmdl = ($wheelInfo -join ' | ')
    instrument = $(if ($NoInstrument) { 'none (picture arm)' }
                   else { 'inventory-frame.py (per-producer counters; NOT a timing run)' })
    elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
    editor = $Editor
    editorSha256 = (Get-FileHash -LiteralPath $Editor -Algorithm SHA256).Hash
    commit = (& git -C $worktree rev-parse HEAD)
    built = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'ARM.json')
Get-Content -LiteralPath (Join-Path $fixture 'ARM.json')
