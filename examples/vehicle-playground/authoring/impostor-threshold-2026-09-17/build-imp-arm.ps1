# Build one arm of the IMPOSTOR THRESHOLD A/B.
#
# The two arms differ in ONE number: `impostorDistance`, 0 in the control and
# the threshold in the candidate. Both carry the same baked impostor assets, so
# the asset tree, the loaded model set and the RAM budget are identical and the
# package delta can only be the threshold. See set-impostors.py.
#
# THREE RULES THIS OBEYS, each paid for by an earlier round:
#
#  * THE EDITOR MUST COME FROM THE WORKTREE UNDER TEST. A binary in build/ is
#    not "the editor at the tree's commit" - `--refresh-gen` regenerates a
#    fixture with a stale baker and nothing in any log says so. Both hashes
#    land in ARM.json.
#  * NEVER an editor `--build` on a fixture: it runs its own `--refresh-gen`
#    and regenerates the instrumentation away. native-build.ps1 is called
#    directly.
#  * NEVER BUILD IN-TREE. WSL `make` replaces the example's `bin`/`obj`
#    junctions with real directories on the (full) D: drive and the linker
#    dies with "Input/output error". -Project and -Root are both C: paths: the
#    source project is a copy taken out of the tree, and every fixture lands
#    beside it.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Editor,
    # The out-of-tree project copy carrying the baked impostor assets.
    [string]$Project = 'C:/tyra-imp-0917/project',
    [Parameter(Mandatory = $true)][string]$Arm,
    # 0 = control (replacement disabled), >0 = the threshold under test.
    [Parameter(Mandatory = $true)][float]$Distance,
    [ValidateSet('debug', 'quiet-debug', 'release')][string]$Profile = 'quiet-debug',
    # counts -> instrument with inventory-frame.py; pixels -> leave it alone and
    # install the route sampler instead.
    [ValidateSet('counts', 'pixels')][string]$Kind = 'counts',
    [switch]$Route,
    [string]$Root = 'C:/tyra-imp-0917'
)
$ErrorActionPreference = 'Stop'

$worktree = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$cache    = Join-Path $env:LOCALAPPDATA 'tyra-editor/native-build/imp-0917'
$toolchain= Join-Path $env:LOCALAPPDATA 'tyra-editor/toolchain/ps2dev'
$fixture  = Join-Path $Root "arms/$Arm"

if (!(Test-Path -LiteralPath $Editor)) { throw "No editor at $Editor" }
if (!(Test-Path -LiteralPath (Join-Path $Project '.res-baked'))) {
    throw "No .res-baked in $Project - bake the impostors, assign them, then " +
          "run `"$Editor --build $Project`" once"
}
if (Test-Path -LiteralPath $fixture) { throw "Archive $fixture before re-running this arm" }
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'arms') | Out-Null

# --- 1. the fixture, from the OUT-OF-TREE project copy -------------------
& python (Join-Path $Project 'authoring/benchmark-district.py') $fixture --profile $Profile
if ($LASTEXITCODE -ne 0) { throw 'benchmark-district.py failed' }
Copy-Item -Recurse -Force (Join-Path $Project '.res-baked') (Join-Path $fixture '.res-baked')
New-Item -ItemType Directory -Force -Path (Join-Path $fixture 'bin') | Out-Null
foreach ($d in 'aoatlas', 'aomap') {
    $src = Join-Path $Project "bin/$d"
    if (Test-Path -LiteralPath $src) { Copy-Item -Recurse -Force $src (Join-Path $fixture "bin/$d") }
}

# --- 2. THE ARM'S ONE NUMBER, then the sampler, then regenerate ----------
& python (Join-Path $PSScriptRoot 'set-impostors.py') $fixture --distance $Distance
if ($LASTEXITCODE -ne 0) { throw 'set-impostors.py failed' }
if ($Route) {
    & python (Join-Path $PSScriptRoot 'route-sampler.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'route-sampler.py failed' }
}
& $Editor --refresh-gen $fixture
if ($LASTEXITCODE -ne 0) { throw '--refresh-gen failed' }
if ($Kind -eq 'counts') {
    & python (Join-Path $Project 'authoring/inventory-frame.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'inventory-frame.py failed' }
}

# --- 3. FIXTURE IDENTITY, before anything is compiled --------------------
# A matching capture hash is a PICTURE check and never a fixture check.
$gen = Get-ChildItem -LiteralPath (Join-Path $fixture 'src') -Recurse -Filter '*.cpp' |
       Select-String -Pattern 'stripRun\s*=\s*(\d+)u' -AllMatches
$runs = ($gen.Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) -join ','
Write-Output "stripRun in generated source: $runs"
# The arm's own number, read back OUT OF THE GENERATED SOURCE rather than
# trusted from the setter - that is what catches a field the baker ignored.
$gd = Get-ChildItem -LiteralPath (Join-Path $fixture 'inc') -Filter '*.hpp' |
      Select-String -Pattern 'impostorDistance|IMPOSTOR' -List
$assigned = (Select-String -LiteralPath (Join-Path $fixture 'inc/scene_data.hpp') `
             -Pattern 'impostors/district-(tower|loft)-far' -AllMatches).Matches.Count
Write-Output "impostor asset references in generated scene data: $assigned"
$distances = (Select-String -LiteralPath (Join-Path $fixture 'inc/scene_data.hpp') `
              -Pattern '(?<!\w)100(?:\.0)?F?\s*,\s*/\*\s*impostorDistance' -AllMatches).Matches.Count

# --- 4. compile with the native toolchain directly -----------------------
& (Join-Path $worktree 'tools/toolchain/native-build.ps1') `
    -Project $fixture -Engine (Join-Path $worktree 'vendor/tyra') `
    -Cache $cache -Toolchain $toolchain
if ($LASTEXITCODE -ne 0) { throw 'native-build failed' }

$elf = Join-Path $fixture 'bin/vehicle-playground.elf'
if (!(Test-Path -LiteralPath $elf)) { throw 'No ELF produced' }
[pscustomobject]@{
    arm = $Arm; kind = $Kind; profile = $Profile
    impostorDistance = $Distance
    impostorAssetRefsInGeneratedScene = $assigned
    stripRun = $runs
    sampler = $(if ($Route) { 'route: camera parked at station N of a drive-in route' }
                else { 'parked: garage day-night, outer day-night' })
    instrument = $(if ($Kind -eq 'counts') { 'inventory-frame.py (per-producer + per-object counters)' }
                   else { 'none (capture arm)' })
    elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
    editor = $Editor
    editorSha256 = (Get-FileHash -LiteralPath $Editor -Algorithm SHA256).Hash
    commit = (& git -C $worktree rev-parse HEAD)
    built = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'ARM.json')
Get-Content -LiteralPath (Join-Path $fixture 'ARM.json')
