# Build the dynamic-shadow A/B fixture project, headlessly, in a SHORT path.
#
#   powershell -File .claude\skills\tyra-testing\scripts\make-shadow-fixture.ps1 `
#       -Editor build-dev\tyrax-editor.exe -Force
#
# Writes %TEMP%\tyra-editor-test\spotab (override with -Project) and a
# vantages.json beside the manifest.  No Docker, no GUI: `--new` scaffolds the
# whole project and `--resave` proves the hand-written objects load.
#
# What is in the scene, and why each piece is there:
#
#   flat 100x100 terrain        the `--new` default; a plain floor for the pool
#   lamp-1 / lamp-2             point lights with "spot": true, half-angle 30,
#                               radius 12, 4 u up, dynamic, "shadowVolumes": 0
#                               (= follow the project, so the project-wide
#                               switch is what the A/B toggles)
#   caster-1 / caster-2         1.5 u boxes on the ground, 0.9 u OFF the lamp
#                               axis so the cone's shadow is a streak beside
#                               the box rather than hidden under it
#   wall-1 / wall-2             8 x 4 x 0.4 boxes 3 u behind each caster - the
#                               backdrop the flashlight's shadow lands on, and
#                               what makes the "lower third" number move
#   player-1                    flashlight ON, frozen (walkSpeed/lookSpeed 0)
#
# TWO pairs, 20 u apart, because the interesting failure of a per-light feature
# is not "does one lamp work" but "do two, and does the second one starve".  The
# count band is one buffer for the whole frame (see src/version.hpp, 1.67.0), so
# a fixture with a single lamp cannot show a budget being shared.
#
# The lamps are deliberately left at "follow the project": the rig toggles the
# PROJECT setting, and a per-light override would silently win over it.  To test
# the override instead, set "shadowVolumes" to 1 or 2 on one lamp by hand and
# watch only the other one change.

param(
    # The editor to scaffold with.  Use YOUR build (build-dev\tyrax-editor.exe);
    # the main checkout's build\ exe is usually locked by a running editor.
    [Parameter(Mandatory = $true)][string]$Editor,
    # Keep it SHORT: PCSX2's host: loader truncates long ELF paths and the game
    # crashes to a null PC before the Tyra banner.
    [string]$Project = "$env:TEMP\tyra-editor-test\spotab",
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\shadow-ab.lib.ps1"

$Editor = (Resolve-Path $Editor).Path
$parent = Split-Path -Parent $Project
$name = Split-Path -Leaf $Project

if (Test-Path $Project) {
    if (-not $Force) { throw "'$Project' already exists. Pass -Force to replace it." }
    Remove-Item -Recurse -Force $Project
}
if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force $parent | Out-Null }

Write-Host "== scaffolding $name (fpp, 100x100) =="
& $Editor --new $name $parent 100 100 fpp
if ($LASTEXITCODE -ne 0) { throw "--new failed with exit code $LASTEXITCODE." }

$manifest = Get-TyraManifest -Project $Project
$objDir = Join-Path $Project 'objects'

# --- the scene -------------------------------------------------------------

# Fixed ids, so a re-run of the rig patches the same files and a report can name
# an object without a lookup.
$ids = @{
    lamp1 = '5000000000000001'; caster1 = '5000000000000002'; wall1 = '5000000000000003'
    lamp2 = '5000000000000011'; caster2 = '5000000000000012'; wall2 = '5000000000000013'
}

function New-Lamp([string]$id, [string]$oname, [double]$x) {
    # "spot": true is a cone down local -Y (rotation [0,0,0] = straight down).
    # "shadowVolumes": 0 = follow the project - see the header.
    return '{ "id": "' + $id + '", "name": "' + $oname + '", "type": "point-light", ' +
           '"position": ' + (Format-JsonVec3 @($x, 4, 0)) + ', "rotation": [0, 0, 0], "scale": [1, 1, 1], ' +
           '"color": [1, 0.92, 0.72], "physics": false, ' +
           '"light": { "brightness": 1.8, "radius": 12, "dynamic": true, "flicker": 0, ' +
           '"spot": true, "spotAngle": 30, "shadowVolumes": 0, "beam": 0 } }'
}
function New-Caster([string]$id, [string]$oname, [double]$x) {
    return '{ "id": "' + $id + '", "name": "' + $oname + '", "type": "box", ' +
           '"position": ' + (Format-JsonVec3 @($x, 0.75, 0)) + ', "rotation": [0, 0, 0], "scale": [1.5, 1.5, 1.5], ' +
           '"color": [0.85, 0.85, 0.85], "physics": false }'
}
function New-Wall([string]$id, [string]$oname, [double]$x) {
    # 1 u thick, not paper.  A torch's second pass hands its receiver slots out
    # first-come-first-served and a genuinely thin wall claims none, so a 0.1 u
    # slab is a fixture that measures a missing feature rather than an absent
    # one (see the flashlight receiver-budget notes in docs/flashlight.md).
    return '{ "id": "' + $id + '", "name": "' + $oname + '", "type": "box", ' +
           '"position": ' + (Format-JsonVec3 @($x, 2, 3)) + ', "rotation": [0, 0, 0], "scale": [8, 4, 1], ' +
           '"color": [0.78, 0.78, 0.8], "physics": false }'
}

$files = [ordered]@{
    $ids.lamp1   = New-Lamp   $ids.lamp1   'lamp-1'    0.0
    $ids.caster1 = New-Caster $ids.caster1 'caster-1'  0.9
    $ids.wall1   = New-Wall   $ids.wall1   'wall-1'    0.0
    $ids.lamp2   = New-Lamp   $ids.lamp2   'lamp-2'   20.0
    $ids.caster2 = New-Caster $ids.caster2 'caster-2' 20.9
    $ids.wall2   = New-Wall   $ids.wall2   'wall-2'   20.0
}
foreach ($id in $files.Keys) {
    [IO.File]::WriteAllText((Join-Path $objDir "$id.json"), $files[$id])
}

# --- the player ------------------------------------------------------------
#
# Frozen camera (docs/player-start.md): speeds at 0 and the pose authored, so
# every boot shows the same frame.  The flashlight is ON so the rig can be
# proven on `flashShadowVolumes`, which already works, before the spot runtime
# exists.  shadow-ab.ps1 rewrites position/rotation per vantage.
#
# The whole object is rewritten rather than patched, because the torch offsets
# below are written by the serializer only when they are not zero - on a fresh
# project there is no key to patch.  Everything omitted here (thirdPerson, ...)
# is defaulted by the loader and written back by --resave.
#
# THE OFFSETS ARE THE WHOLE REASON THIS FIXTURE CAN PROVE ANYTHING TODAY.  A
# torch that sits exactly on the eye casts shadows that are, by construction,
# hidden behind the things casting them - the camera occludes precisely what
# the light does, and both switch positions photograph identically.  Dropping
# the lamp 1 m (the clamp's limit) and 0.6 m to the side separates the two, and
# the caster's shadow appears on the wall as a band ABOVE its own silhouette.
# Measured before the offsets were added: d centre 0.004, d lower third 0.000 -
# a perfect, meaningless A/B.
$playerFile = Get-PlayerObjectFile -Project $Project
$playerText = [IO.File]::ReadAllText($playerFile)
$playerId = ([regex]::Match($playerText, '"id"\s*:\s*"([^"]+)"')).Groups[1].Value
[IO.File]::WriteAllText($playerFile,
    '{ "id": "' + $playerId + '", "name": "player-1", "type": "player", ' +
    '"position": [0, 0, -8], "rotation": [8, 0, 0], "scale": [1, 1, 1], ' +
    '"color": [0.15, 0.9, 0.9], "physics": false, ' +
    '"player": { "mode": "walk", "walkSpeed": 0, "lookSpeed": 0, "eyeHeight": 1.8, ' +
    '"jumpSpeed": 4.5, "canJump": true, ' +
    # "toggle": "" on purpose - an unbound torch cannot be switched off by a
    # stray button in the middle of a measured run.
    '"flashlight": { "enabled": true, "color": [0.85, 0.85, 0.7], "range": 34, ' +
    '"angle": 26, "toggle": "", "offsetRight": 0.6, "offsetDown": 1 } } }')

# --- the manifest ----------------------------------------------------------

# The scene's object list: the Player `--new` made, plus everything above.
$t = [IO.File]::ReadAllText($manifest)
$m = [regex]::Match($t, '"objects"\s*:\s*\[(?<b>[^\]]*)\]')
if (-not $m.Success) { throw "No scene object list in $manifest." }
$existing = @()
foreach ($q in [regex]::Matches($m.Groups['b'].Value, '"([0-9a-fA-F]+)"')) { $existing += $q.Groups[1].Value }
$all = @($existing) + @($ids.lamp1, $ids.caster1, $ids.wall1, $ids.lamp2, $ids.caster2, $ids.wall2)
$list = '"objects": [' + (($all | ForEach-Object { '"' + $_ + '"' }) -join ', ') + ']'
$t = $t.Substring(0, $m.Index) + $list + $t.Substring($m.Index + $m.Length)
[IO.File]::WriteAllText($manifest, $t)

# Settings the A/B depends on.  Both shadow-volume switches are written OFF so
# the fixture starts from a real "before" - and the --resave below then DROPS
# both keys again, because the serializer writes them only when they are true.
# That is fine and is the case shadow-ab.ps1's Set-TyraSetting is built for: a
# key the manifest does not carry is inserted rather than missed.
$settings = [ordered]@{
    keyboardMouse      = 'false'   # nothing may turn the frozen camera
    displayMode        = '"progressive"'  # interlaced alternates fields between captures
    showFps            = 'true'    # the rig reads this region back
    showMemory         = 'false'
    showProfiler       = 'false'
    flashShadowVolumes = 'false'
    spotShadowVolumes  = 'false'
    # A dim scene: the point of the fixture is what the LAMPS and the torch do,
    # and an ambient term that lights the shadow back up is the fastest way to
    # measure nothing.
    ambient            = '0.1'
    diffuse            = '0.12'
    skyColor           = '[0.03, 0.04, 0.07]'
    skyTopColor        = '[0.01, 0.01, 0.03]'
    fogEnabled         = 'false'
    aoEnabled          = 'false'
    # And the ambience PRESET has to go, or none of the four lines above reach
    # the game.  A new project ships one preset ("Default", bright blue sky,
    # ambient 0.55) and `defaultAmbience: 0` applies it at boot ON TOP of the
    # project settings - which is why the first cut of this fixture rendered a
    # sunny afternoon while its manifest said ambient 0.1.  -1 = no preset.
    defaultAmbience    = '-1'
}
foreach ($k in $settings.Keys) { Set-TyraSetting -Path $manifest -Key $k -Value $settings[$k] }

# --- the vantages ----------------------------------------------------------
#
# Rotation is [pitchDown, heading, 0]; heading 0 looks along +Z, so a player at
# negative Z faces the lamp groups.  8 u back frames one box, its shadow and the
# wall behind it.
#
# `between` stands midway and TWENTY units back, not eight: from 8 u the two
# groups sit 51 degrees off the axis and a 60-degree frame holds neither of
# them.  From 20 u they are +-26.5 degrees, i.e. both in shot at once - which is
# the vantage that can answer "only the nearest spot carves volumes" by showing
# one group with a shadow and the other without.
$vantages = @'
[
  { "name": "lamp1-8u",  "position": [0, 0, -8],    "rotation": [8, 0, 0] },
  { "name": "between",   "position": [10, 0, -20],  "rotation": [4, 0, 0] },
  { "name": "lamp2-8u",  "position": [20, 0, -8],   "rotation": [8, 0, 0] }
]
'@
$vantageFile = Join-Path $Project 'vantages.json'
[IO.File]::WriteAllText($vantageFile, $vantages)

# --- prove it loads --------------------------------------------------------

Write-Host "== --resave (must load with no warnings) =="
$log = & $Editor --resave $Project 2>&1
$code = $LASTEXITCODE
$log | ForEach-Object { Write-Host "   $_" }
if ($code -ne 0) { throw "--resave failed with exit code $code." }
$bad = @($log | Where-Object { $_ -match 'warn|error|could not|failed' })
if ($bad.Count -gt 0) { throw "--resave complained:`n$($bad -join "`n")" }

Write-Host ""
Write-Host "fixture:  $Project"
Write-Host "vantages: $vantageFile"
Write-Host "objects:  $($all.Count) in scene (player + 2 lamps + 2 casters + 2 walls)"
