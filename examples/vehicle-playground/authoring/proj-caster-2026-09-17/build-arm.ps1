# Build one arm of the CHEAP PROJECTED-SHADOW CASTER round.
#
# The lever, from the garage-day frame inventory in
# ../wheel-strip-2026-09-17/README.md: 87% of the `proj_shadows` bracket is the
# CASTER'S OWN MODEL BAGS re-submitted from the light - 60 of 69 packages and
# 4 440 of 4 632 vertices, decomposing exactly as the CC96 body's three parts
# plus the ggbot body's one. Those bags are submitted textured and with
# per-vertex colours, which is the 75-vertices-a-package VU1 class, and the
# 64x64 shadow map reads neither attribute - only alpha coverage. The arm
# submits the same vertices through the single-colour untextured class at 150.
#
# This is a COUNTS fixture. It drains StaPipCore telemetry at every producer
# boundary, so its own milliseconds are meaningless and none are recorded.
# PCSX2 is enough: its counters are exact even though its milliseconds are not.
#
# ONE EDITOR, BOTH ARMS. The silhouette bag is generated into the game
# unconditionally; an arm sets the single `#define` the generated game guards
# the consumer with, patched into the arm's own terrain_game.cpp after
# --refresh-gen. So the arms share a baker, a generated source and an engine
# and differ in ONE token.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Editor,
    [Parameter(Mandatory = $true)][string]$Arm,
    # 1 = the silhouette casts from the single-colour untextured bag;
    # 0 = the caster's own textured, per-vertex-coloured bags (today).
    [ValidateSet(0, 1)][int]$Caster = 0,
    [ValidateSet('debug', 'quiet-debug', 'release')][string]$Profile = 'quiet-debug',
    # Skip the telemetry instrument: for the PICTURE arms, which need the game
    # to photograph itself rather than count anything.
    [switch]$NoInstrument,
    # THE KNOWN-BAD ARM. Removes the caster submit altogether, so the slot is
    # rendered empty and no silhouette reaches the receiver patch. It exists to
    # prove the picture gate can SEE the thing under test: if this arm's
    # capture matches the control's, then the pose does not show the shadow and
    # an identical control-vs-cheap capture means nothing. Never an arm to
    # measure - only to validate the measurement.
    [switch]$NoSilhouette,
    # Aim the GARAGE poses at the ground instead of down the road. The stock
    # benchmark camera sits behind the coupe at eye level, where the car's own
    # body hides the shadow it casts - the blind-check arm proved that removing
    # the silhouette ENTIRELY does not move one pixel of that shot, so it
    # cannot be the shot the picture gate is read from. This looks down on the
    # ground between the two casters, where the shadows actually land.
    [switch]$ShadowCam,
    # Turn on the shipped PROJDBG line (SHADOW_VOLUMES_DEBUG), which reports
    # per slot: the occupant, the light kind, the distance, the FADE and the
    # reach. A diagnostic arm, never a measured one - it says where the
    # shadow is and why it is or is not on screen.
    [switch]$ProjDebug,
    # Lift the receiver patch clear of the surface it is depth-tested against.
    # A DIAGNOSTIC, not a measured arm: in this district both casters stand on
    # the ROAD, the patch is placed on the surface under them and is then
    # rejected by the road's own depth, so the shipped shot shows no shadow at
    # all and the picture gate is blind (the blind-check arm proves it). With
    # the patch lifted the silhouette is visible in BOTH arms, which is what
    # makes a control-vs-cheap shape comparison possible at all.
    [double]$PatchLift = 0,
    # Leave the AI drivers their routes, so the CASTERS MOVE. A vehicle's
    # shadow that is right when parked and swims when driving is a rejected
    # arm, and a parked fixture cannot see that. The cost is stated in
    # benchmark-district.py: the pixels stop being repeatable frame to frame,
    # so this arm is read from its COUNTS (which are per-frame averages over
    # 240 frames of real motion) and from whether the shadow tracks the body,
    # never from a capture hash.
    [switch]$KeepRoutes,
    # A MOVING projected caster, which the shipped district does not contain:
    # its only two shadowMode 3 objects (the coupe and the Rally 04) are
    # PARKED, and its only two routed vehicles (rival, rival-2) are shadowMode
    # 2, i.e. blob shadows. So -KeepRoutes alone changes nothing - the capture
    # comes back byte-identical to the parked one, which is how this was
    # found. This promotes the two routed rivals to projected casters and aims
    # the garage poses at the circuit they drive, so the silhouette is
    # re-rendered every frame for a caster that is genuinely moving and
    # turning. Implies -KeepRoutes.
    [switch]$MovingCaster,
    # Where the baked asset tree comes from. Produced ONCE by
    #   <editor> --build C:\tyra-probe-0917-caster\example
    # on a C: copy of the example, because a WSL `make` in-tree replaces the
    # bin/obj junctions with real directories and the linker then dies.
    [string]$Baked = 'C:/tyra-probe-0917-caster/example/.res-baked',
    [string]$Root = 'C:/tyra-probe-0917-caster'
)
$ErrorActionPreference = 'Stop'

$worktree = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$example  = Join-Path $worktree 'examples/vehicle-playground'
$cache    = Join-Path $env:LOCALAPPDATA 'tyra-editor/native-build/caster-0917'
$toolchain= Join-Path $env:LOCALAPPDATA 'tyra-editor/toolchain/ps2dev'
$fixture  = Join-Path $Root "arms/$Arm"

if (!(Test-Path -LiteralPath $Editor)) { throw "No editor at $Editor" }
if (!(Test-Path -LiteralPath $Baked))  { throw "No baked tree at $Baked" }
if (Test-Path -LiteralPath $fixture) { throw "Archive $fixture before re-running this arm" }
New-Item -ItemType Directory -Force -Path (Join-Path $Root 'arms') | Out-Null

# --- 1. the fixture, with the baked asset tree copied in ------------------
$districtArgs = @($fixture, '--profile', $Profile)
if ($KeepRoutes -or $MovingCaster) { $districtArgs += '--keep-routes' }
& python (Join-Path $example 'authoring/benchmark-district.py') @districtArgs
if ($LASTEXITCODE -ne 0) { throw 'benchmark-district.py failed' }
if ($MovingCaster) {
    foreach ($id in 'cc96car000000002', 'cc96car000000003') {
        $of = Join-Path $fixture "objects/$id.json"
        $o = Get-Content -LiteralPath $of -Raw
        if ($o -notmatch '"shadowMode":\s*2') { throw "$id is not shadowMode 2 any more" }
        Set-Content -LiteralPath $of -NoNewline `
            -Value ($o -replace '"shadowMode":\s*2', '"shadowMode": 3')
    }
    # Aim the garage poses at the circuit the rivals drive, instead of the
    # parked pair in the garage.
    $sampler = Join-Path $fixture 'src/scripts/zz_district_benchmark.cpp'
    $s = Get-Content -LiteralPath $sampler -Raw
    $oldEye = 'ctx.cameraEye = phase < 2 ? Tyra::Vec4(0,4,-32,1) : Tyra::Vec4(4,9,102,1);'
    $oldAt  = 'ctx.cameraAt = phase < 2 ? Tyra::Vec4(0,1,-12,1) : Tyra::Vec4(65,3,106,1);'
    if (-not $s.Contains($oldEye)) { throw 'benchmark camera shape changed' }
    # Straight down from 34 units over the middle of the circuit: inside
    # PROJ_SHADOW_DISTANCE, and a downward cone keeps a driving car in view for
    # most of its lap instead of for the first few seconds.
    $s = $s.Replace($oldEye, 'ctx.cameraEye = phase < 2 ? Tyra::Vec4(110,40,-85,1) : Tyra::Vec4(4,9,102,1);')
    $s = $s.Replace($oldAt,  'ctx.cameraAt = phase < 2 ? Tyra::Vec4(110,5,-85,1) : Tyra::Vec4(65,3,106,1);')
    Set-Content -LiteralPath $sampler -Value $s -NoNewline
}

if ($ShadowCam -and -not $MovingCaster) {
    $sampler = Join-Path $fixture 'src/scripts/zz_district_benchmark.cpp'
    $s = Get-Content -LiteralPath $sampler -Raw
    $oldEye = 'ctx.cameraEye = phase < 2 ? Tyra::Vec4(0,4,-32,1) : Tyra::Vec4(4,9,102,1);'
    $oldAt  = 'ctx.cameraAt = phase < 2 ? Tyra::Vec4(0,1,-12,1) : Tyra::Vec4(65,3,106,1);'
    if (-not $s.Contains($oldEye)) { throw 'benchmark camera shape changed - -ShadowCam cannot be patched in' }
    $s = $s.Replace($oldEye, 'ctx.cameraEye = phase < 2 ? Tyra::Vec4(10,10,-30,1) : Tyra::Vec4(4,9,102,1);')
    $s = $s.Replace($oldAt,  'ctx.cameraAt = phase < 2 ? Tyra::Vec4(3,0.5F,-19,1) : Tyra::Vec4(65,3,106,1);')
    Set-Content -LiteralPath $sampler -Value $s -NoNewline
}
Copy-Item -Recurse -Force $Baked (Join-Path $fixture '.res-baked')
New-Item -ItemType Directory -Force -Path (Join-Path $fixture 'bin') | Out-Null
foreach ($d in 'aoatlas', 'aomap') {
    $src = Join-Path (Split-Path -Parent $Baked) "bin/$d"
    if (Test-Path -LiteralPath $src) { Copy-Item -Recurse -Force $src (Join-Path $fixture "bin/$d") }
}

# --- 2. regenerate, set the arm's knob, then instrument -------------------
& $Editor --refresh-gen $fixture
if ($LASTEXITCODE -ne 0) { throw '--refresh-gen failed' }

$game = Join-Path $fixture 'src/terrain_game.cpp'
$g = Get-Content -LiteralPath $game -Raw
$pattern = '#define TYRA_CHEAP_PROJ_CASTER 0'
if ($g -notmatch [regex]::Escape($pattern)) {
    throw 'TYRA_CHEAP_PROJ_CASTER default is not 0 in the generated game - the arms cannot be set from here any more'
}
$g = $g.Replace($pattern, ('#define TYRA_CHEAP_PROJ_CASTER ' + $Caster))
if ($NoSilhouette) {
    $submit = '      for (GeoPart& part : g.parts) renderAtFloor(casterBag(part));'
    if (-not $g.Contains($submit)) { throw 'caster submit shape changed - the blind check cannot be patched in' }
    $g = $g.Replace($submit, '      for (GeoPart& part : g.parts) { (void)part; }  // BLIND CHECK: no silhouette')
}
Set-Content -LiteralPath $game -Value $g -NoNewline

if ($PatchLift -ne 0) {
    $g2 = Get-Content -LiteralPath $game -Raw
    $oldY = 'const Vec4 p(px, patchY(px, pz) + 0.05F, pz, 1.0F);'
    if (-not $g2.Contains($oldY)) { throw 'receiver patch placement shape changed' }
    $newY = 'const Vec4 p(px, patchY(px, pz) + ' + `
            ([string]::Format([cultureinfo]::InvariantCulture, '{0:0.00}', $PatchLift)) + 'F, pz, 1.0F);'
    Set-Content -LiteralPath $game -NoNewline -Value $g2.Replace($oldY, $newY)
}

if ($ProjDebug) {
    $cfg = Join-Path $fixture 'inc/terrain_config.hpp'
    $c = Get-Content -LiteralPath $cfg -Raw
    $old = 'constexpr int SHADOW_VOLUMES_DEBUG = 0;'
    if (-not $c.Contains($old)) { throw 'SHADOW_VOLUMES_DEBUG shape changed' }
    Set-Content -LiteralPath $cfg -NoNewline `
        -Value $c.Replace($old, 'constexpr int SHADOW_VOLUMES_DEBUG = 1;')
}

if (-not $NoInstrument) {
    & python (Join-Path $example 'authoring/inventory-frame.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'inventory-frame.py failed' }
}

# --- 3. FIXTURE IDENTITY, before anything is compiled --------------------
# A matching capture hash is a PICTURE check and never a fixture check. These
# lines are functions of the baker and of the arm, and they do catch it.
$gen = Get-ChildItem -LiteralPath (Join-Path $fixture 'src') -Recurse -Filter '*.cpp' |
       Select-String -Pattern 'stripRun\s*=\s*(\d+)u' -AllMatches
$runs = ($gen.Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) -join ','
$knob = (Select-String -LiteralPath $game -Pattern '#define TYRA_CHEAP_PROJ_CASTER \d').Matches.Value
$roadLine = (Select-String -LiteralPath $game -Pattern 'ROADSTRIP' -SimpleMatch | Select-Object -First 1).Line
$terrLine = (Select-String -LiteralPath $game -Pattern 'TERRAINSTRIP' -SimpleMatch | Select-Object -First 1).Line
Write-Output "stripRun in generated source: $runs (expect 75 at the branch tip)"
Write-Output "arm knob: $knob"
Write-Output "ROADSTRIP producer:    $roadLine"
Write-Output "TERRAINSTRIP producer: $terrLine"

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
    cheapProjCaster = $Caster
    noSilhouette = [bool]$NoSilhouette
    shadowCam = [bool]$ShadowCam
    projDebug = [bool]$ProjDebug
    patchLift = $PatchLift
    keepRoutes = [bool]($KeepRoutes -or $MovingCaster)
    movingCaster = [bool]$MovingCaster
    roadStrip = $roadLine; terrainStrip = $terrLine
    instrument = $(if ($NoInstrument) { 'none (picture arm)' }
                   else { 'inventory-frame.py (per-producer counters; NOT a timing run)' })
    elfSha256 = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
    editor = $Editor
    editorSha256 = (Get-FileHash -LiteralPath $Editor -Algorithm SHA256).Hash
    commit = (& git -C $worktree rev-parse HEAD)
    built = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'ARM.json')
Get-Content -LiteralPath (Join-Path $fixture 'ARM.json')
