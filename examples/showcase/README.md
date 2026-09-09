# Aster — The Tide Observatory

Aster is a playable coastal observatory: limestone arcades, copper roofs,
formal gardens, a tidal channel and a brass planetarium. A short lens hunt
and working instruments are woven into one coherent place.

![The observatory, captured from the running PS2 build](preview/observatory.png)

![The brass orrery and tidal garden](preview/orrery.png)

Open `showcase.tyra` in TyraX, or build and launch it:

```powershell
build/tyrax-editor.exe --build examples/showcase --run
```

On Linux use `build/tyrax-editor` with the same arguments. Docker and a
configured PCSX2 installation are required for the game build and launch.

## Explore

The arrival film returns control at the entrance. Follow either promenade;
the crossing connects them halfway along. Three brass lenses sit on optical
benches at **(-8, 17)**, **(8, 1)** and **(-8, -17)** (world X/Z). Collect
them, then use the central instrument at **(0, -6)**. It explains what is
missing if approached early. Lenses disappear on pickup and cannot be counted
twice. Their visibility and the expedition's progress travel in save slots.

| Control | Action |
|---|---|
| Left / right stick | Walk / look |
| R2 / Cross | Sprint / jump |
| Square | Use a lens, stand, keeper or instrument; pick up a weight |
| Circle | Throw a held weight; toggle the visitor's flashlight |
| Select | Replay the 22-second guided film |
| Start | Pause and open the expedition save menu |

The calibration court is east of the entrance. The hovering keeper patrols
the east garden and offers a clue. A portal hidden in the east entrance's
stone vestibule leads to a vaulted instrument cellar twelve metres below
the rotunda. Enter the narrow passage from the north, near **(8.9, 17)**,
then turn left into the vestibule and right towards the frame at **(11.7, 24)**.
The return portal opens onto the enclosed vestibule, with the garden hidden
behind two turns. Inside the rotunda, the silver instrument
traces reflections and the gold instrument shows a second camera's view.
A save point waits at the entrance. Films use the engine's skippable-sequence
controls and return the gameplay camera afterward.

![The screened portal into the instrument cellar](preview/cellar-portal.png)

![Inside the vaulted cellar, captured from the running PS2 build](preview/cellar.png)

## What is running

| Feature | Where to see it |
|---|---|
| Imported geometry and mesh LOD | Complete arcades, profiled columns, dome, planters and props |
| CC0 import and cutout textures | Quaternius ivy, supply crates and survey cart |
| Skeletal animation and navigation | The keeper's retained `Idle` action, rig and three-waypoint patrol |
| Terrain and collision | A support heightfield follows the paved island; mesh collision preserves arch openings; four invisible walls protect the perimeter |
| Baked GI and probe lighting | Warm architecture, shaded colonnades and the keeper; the GI cache ships with the project |
| Dynamic lights and bloom | The planetarium core, blue vault light and warm lanterns |
| Reflective materials | Gold, copper and water sample the live sky; moving brass rings use a cheaper matte surface |
| Spatial portals | A vaulted underground cellar and a screened surface vestibule, each with a complete, bounded destination mesh |
| VU0 ray tracing | The silver lens: a 32px experiment with analytic sphere targets, drawn within 15m |
| Live camera textures | The gold monitor watches the moving core and rings |
| Particles | A small firefly field around the celestial mechanism |
| Flow graphs and save values | Single-use pickups, three-lens gate, completion state and information stands |
| Physics and pickup/throw | Three calibration weights and a backstop |
| Camera and object animation | Arrival, tour and alignment films; the finale changes the core's scale and colour |
| Audio and reverb | Original stereo composition, pickup chimes and the rotunda's hall reverb |
| UI | Title typography, contextual prompts, clues, pause and memory-card saves |

The scene stays resident. Streaming districts have their own dedicated
examples elsewhere in the repository.

## Art and reproducibility

The architectural module is an **8.4m bay**, with a 4m arch spring line. Walking
slabs end at Y=0, foundations extend below the sea, and curbs close on the
promenade edges. Columns and dome share a measured circular footprint. The
channel water uses 2m patches; paving UVs are authored in metres. The sea is a
single surface with 20m patches, with no nearly coplanar underside. Neither
water mesh uses LOD simplification. Both use a procedural ripple texture and
the live sky reflection material; this is stylized water, not fluid simulation.
Their authored base color uses the pre-lit route to avoid the island's finite
GI probe grid producing broad triangular light patches across the open sea.
The calibration guide and shortened backstop keep the entrance court clear;
weights sit outside the portal's approach. The
lighthouse base intersects its supporting rock instead of resting on its tip.

Meshes share positions and use deliberate UV seams. Untextured surfaces have
constant UVs so the LOD welder can collapse their interior edges; lathed stone
has continuous cylindrical UVs. Decorative per-face UV islands on a plain
column had prevented its LOD tiers from being produced.

The recipe joins 156 static pieces into six district meshes.
The resulting scene has 74 runtime objects. Walking slabs and interactive
props remain independent. District meshes retain their full geometry; smaller
props use mesh LOD. The rotating matte rings use the engine's transform fast
path. These choices reduce submissions and avoid rebuilding reflective ring
geometry every frame.

The cellar and vestibule occupy separate district meshes. Each portal lists
only its complete destination district and disables terrain rendering. The
cellar has a real opening in its collision mesh; the support heightfield is
lowered below its floor so walking collision does not push visitors upstairs.
Surface model floors remain in place. Seven vertical GI probe levels cover
the cellar as well as the garden, with warm baked lights and local reverb.

The committed `.tyra`, object JSON, `res/` sources and `terrain-Aster.heights`
open and build without Python, Blender, downloads or `C:/Assets`. Generated
C++ and model binaries are derived data. The optional authoring recipe needs
Python 3 and Pillow:

```powershell
python examples/showcase/build-showcase.py
build/tyrax-editor.exe --bake-gi examples/showcase
build/tyrax-editor.exe --build examples/showcase --run
```

**Re-running the recipe replaces the authored scene and graphs.** Keep manual
editor changes in version control first. GI is explicitly baked and committed
in `.res-baked/gi/scene0.gi`; re-bake after geometry or lighting changes.
A normal build does not invent a fresh GI solution.

Optional CC0 preparation steps reproduce the checked-in imported assets:

```powershell
python examples/showcase/prepare-assets.py C:/Assets
blender --background --python examples/showcase/prepare-keeper.py -- C:/Assets
```

Both accept any asset-root path on either platform. The first preserves prop
geometry/UVs, centres origins at their feet and replaces absolute material
references. The second keeps the keeper's rig/actions, limits its mesh to
approximately 1,000 triangles and embeds 128px textures. Architecture,
botanical meshes, instruments, surface textures and music are original to
this example. Imported art is Quaternius CC0; see
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) and `res/aster/CC0-*.txt`.

## Budgets and verification

Remote pad and Live Debugger remain enabled for unattended checks. Live Link,
Live Logic and time-machine recording are off. Performance HUD counters are
off for presentation; pass `--profile` to the Python recipe to enable all three
counters, then rebuild. Optical experiments have bounded target lists; the
raytraced lens does not trace the whole garden from the entrance. PCSX2 tests
rendering and controls; its measurements do not establish real-console speed.

Use `--ui-script` to inspect the saved project, then run
`build/tyrax-editor.exe --pad examples/showcase "press select; wait 23; neutral"`
to exercise the running guided film. Inspect
`bin/log.txt`, walk both promenades, test the early-use gate and all pickups,
align the instrument, pass through both portals and throw a weight. Saving
and reopening must preserve every graph link: ordinary action nodes have no
execution output, so sibling actions fan out from a trigger or control node.

Validation on Windows / PCSX2 (software renderer): release editor compilation,
Docker PS2 build, GI bake, project re-save with all 37 graph links retained,
arrival/tour rendering, pad movement and two lens pickups, and a moving keeper.
Live Debugger trigger tests covered the remaining pickup, early-use rejection,
duplicate collection and the successful alignment (`lenses=3`, `aligned=1`).
The pad-driven save/load menu restored a one-lens save after collecting another
lens. WASAPI reported nonzero output from the emulator's audio session.
The profiling build measured 25 FPS in the wide garden view and 50 FPS on the
north promenade; these are emulator observations, not a hardware benchmark.
The boundary update was checked with the editor's real UI (insert, toggle,
undo, save and re-open), refreshed GI, a Docker build and pad movement against
the perimeter. Portal traversal and low-angle sea views were exercised in
PCSX2. A full pickup/throw circuit remains a manual acceptance check. The
images above are unretouched game captures, not editor renders or concept art.

The cellar update was checked with a fresh Release editor and Docker PS2
build, a new GI bake, and pad traversal from the garden through both bends,
into the cellar and back out to the garden. A temporary time-machine capture
confirmed the eye height changes from Y=1.83 to Y=-10.2 and back; recording is
disabled in the shipped project. Both portal views use their destination's
enclosed geometry. The vault end walls overlap its crown to close sky/water
gaps above the door and storage wall.
