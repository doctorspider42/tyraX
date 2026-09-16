# Build the PER-PRODUCER INVENTORY fixture of the Motor District
# (docs/ee-submission-rearchitecture.md, "Order of work" item 3 - S4's round).
#
# This is a COUNTS fixture. It drains StaPipCore telemetry at every producer
# boundary, so its own milliseconds are meaningless and none are recorded; what
# it produces is bin/frame-inventory.csv - triangles, VU1 packages, bags,
# vertices and packet flushes per producer, per parked pose. PCSX2 is enough
# and no console is needed, because PCSX2's COUNTS are exact even though its
# milliseconds are not (it emulates no EE data cache).
#
# THE EDITOR MUST COME FROM THIS WORKTREE. An editor binary sitting in build/
# is not "the editor at the tree's commit" - `--refresh-gen` will faithfully
# regenerate a fixture with a stale baker, and nothing in any log says so. That
# is what produced the EE probes' 72-run control. Both hashes land in ARM.json.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Editor,
    [string]$Arm = 'inventory',
    # quiet-debug, like every other round on this fixture: the live tools' host:
    # pollers cost 6.44 ms of `work`. They change no COUNT, but keeping the
    # profile identical across rounds is what makes the rows comparable.
    [ValidateSet('debug', 'quiet-debug', 'release')][string]$Profile = 'quiet-debug',
    [switch]$KeepRoutes,
    [string]$Root = 'D:/tyra-probe-0916'
)
$ErrorActionPreference = 'Stop'

$worktree = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$example  = Join-Path $worktree 'examples/vehicle-playground'
$cache    = Join-Path $env:LOCALAPPDATA 'tyra-editor/native-build/probe-0916'
$toolchain= Join-Path $env:LOCALAPPDATA 'tyra-editor/toolchain/ps2dev'
$fixture  = Join-Path $Root "arms/$Arm"

if (!(Test-Path -LiteralPath $Editor)) { throw "No editor at $Editor" }
if (Test-Path -LiteralPath $fixture) { throw "Archive $fixture before re-running this arm" }
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'arms') | Out-Null

# --- 1. the fixture, with the baked asset tree copied in ------------------
$bd = @((Join-Path $example 'authoring/benchmark-district.py'), $fixture,
        '--profile', $Profile)
if ($KeepRoutes) { $bd += '--keep-routes' }
& python @bd
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

# --- 2. regenerate with THIS editor, THEN instrument ----------------------
& $Editor --refresh-gen $fixture
if ($LASTEXITCODE -ne 0) { throw '--refresh-gen failed' }
& python (Join-Path $example 'authoring/inventory-frame.py') $fixture
if ($LASTEXITCODE -ne 0) { throw 'inventory-frame.py failed' }

# --- 3. FIXTURE IDENTITY, before anything is compiled --------------------
# A matching capture hash is a PICTURE check and never a fixture check: the
# strip run changes how a surface is cut into runs, not which pixels it covers.
# These three lines are functions of the baker and do catch it.
$gen = Get-ChildItem -LiteralPath (Join-Path $fixture 'src') -Recurse -Filter '*.cpp' |
       Select-String -Pattern 'stripRun\s*=\s*(\d+)u' -AllMatches
$runs = ($gen.Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) -join ','
Write-Output "stripRun in generated source: $runs (expect 75 at the branch tip)"

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
    keepRoutes = [bool]$KeepRoutes
    instrument = 'inventory-frame.py (per-producer counters; NOT a timing run)'
    elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
    editor = $Editor
    editorSha256 = (Get-FileHash -LiteralPath $Editor -Algorithm SHA256).Hash
    commit = (& git -C $worktree rev-parse HEAD)
    built = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'ARM.json')
Get-Content -LiteralPath (Join-Path $fixture 'ARM.json')
