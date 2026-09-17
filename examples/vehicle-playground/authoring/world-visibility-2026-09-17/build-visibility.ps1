# Build the PER-OBJECT VISIBILITY fixture of the Motor District
# (docs/ee-submission-rearchitecture.md, "Order of work" item 5 - world
# visibility). Modelled on ../reflection-probe-2026-09-16/build-inventory.ps1
# and obeying the same three rules that round paid for.
#
# TWO ARM KINDS, and they are deliberately not the same fixture:
#
#   -Kind pixels   `debug`, the VISIBILITY sampler, NO instrumentation. The
#                  game photographs itself through the Live Debugger's command
#                  channel, which quiet-debug switches off. This arm answers
#                  "what is SEEN".
#   -Kind counts   `quiet-debug`, the parked sampler, inventory-frame.py. This
#                  arm answers "what is SUBMITTED", per object, and reproduces
#                  the 2026-09-16 inventory at THIS commit rather than trusting
#                  it across 168 commits.
#
# THE EDITOR MUST COME FROM THIS WORKTREE. A binary sitting in build/ is not
# "the editor at the tree's commit" - `--refresh-gen` will faithfully
# regenerate a fixture with a stale baker and nothing in any log says so. Both
# hashes land in ARM.json.
#
# NEVER an editor `--build`: it runs its own `--refresh-gen` and would
# regenerate the instrumentation away. native-build.ps1 is called directly.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Editor,
    [ValidateSet('pixels', 'counts')][string]$Kind = 'pixels',
    [string]$Arm,
    [string]$Root = 'D:/tyra-probe-0917'
)
$ErrorActionPreference = 'Stop'

if (-not $Arm) { $Arm = "vis-$Kind" }
$profileName = if ($Kind -eq 'pixels') { 'debug' } else { 'quiet-debug' }

$worktree = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$example  = Join-Path $worktree 'examples/vehicle-playground'
$cache    = Join-Path $env:LOCALAPPDATA 'tyra-editor/native-build/probe-0917'
$toolchain= Join-Path $env:LOCALAPPDATA 'tyra-editor/toolchain/ps2dev'
$fixture  = Join-Path $Root "arms/$Arm"

if (!(Test-Path -LiteralPath $Editor)) { throw "No editor at $Editor" }
if (Test-Path -LiteralPath $fixture) { throw "Archive $fixture before re-running this arm" }
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'arms') | Out-Null

# --- 1. the fixture, with the baked asset tree copied in ------------------
& python (Join-Path $example 'authoring/benchmark-district.py') $fixture --profile $profileName
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

# --- 2. the arm's sampler, then regenerate and (for counts) instrument -----
if ($Kind -eq 'pixels') {
    & python (Join-Path $PSScriptRoot 'visibility-sampler.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'visibility-sampler.py failed' }
}
& $Editor --refresh-gen $fixture
if ($LASTEXITCODE -ne 0) { throw '--refresh-gen failed' }
if ($Kind -eq 'counts') {
    & python (Join-Path $example 'authoring/inventory-frame.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'inventory-frame.py failed' }
}

# --- 3. FIXTURE IDENTITY, before anything is compiled --------------------
# A matching capture hash is a PICTURE check and never a fixture check: the
# strip run changes how a surface is cut into runs, not which pixels it covers.
# These lines are functions of the baker and do catch it.
$gen = Get-ChildItem -LiteralPath (Join-Path $fixture 'src') -Recurse -Filter '*.cpp' |
       Select-String -Pattern 'stripRun\s*=\s*(\d+)u' -AllMatches
$runs = ($gen.Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) -join ','
Write-Output "stripRun in generated source: $runs (expect 75 at the branch tip)"
$roadLine = (Select-String -LiteralPath (Join-Path $fixture 'src/terrain_game.cpp') `
    -Pattern 'ROADSTRIP' -SimpleMatch | Select-Object -First 1).Line
$terrLine = (Select-String -LiteralPath (Join-Path $fixture 'src/terrain_game.cpp') `
    -Pattern 'TERRAINSTRIP' -SimpleMatch | Select-Object -First 1).Line
Write-Output "ROADSTRIP producer:    $roadLine"
Write-Output "TERRAINSTRIP producer: $terrLine"

# --- 4. compile with the native toolchain directly -----------------------
& (Join-Path $worktree 'tools/toolchain/native-build.ps1') `
    -Project $fixture -Engine (Join-Path $worktree 'vendor/tyra') `
    -Cache $cache -Toolchain $toolchain
if ($LASTEXITCODE -ne 0) { throw 'native-build failed' }

$elf = Join-Path $fixture 'bin/vehicle-playground.elf'
if (!(Test-Path -LiteralPath $elf)) { throw 'No ELF produced' }
[pscustomobject]@{
    arm = $Arm; kind = $Kind; profile = $profileName; stripRun = $runs
    sampler = $(if ($Kind -eq 'pixels') {
                    'visibility: parked garage day, command file names objects to HIDE' }
                else { 'parked: garage day-night, outer day-night' })
    instrument = $(if ($Kind -eq 'counts') {
                       'inventory-frame.py (per-producer + per-object counters; NOT a timing run)' }
                   else { 'none (capture arm)' })
    elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
    editor = $Editor
    editorSha256 = (Get-FileHash -LiteralPath $Editor -Algorithm SHA256).Hash
    commit = (& git -C $worktree rev-parse HEAD)
    built = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'ARM.json')
Get-Content -LiteralPath (Join-Path $fixture 'ARM.json')
