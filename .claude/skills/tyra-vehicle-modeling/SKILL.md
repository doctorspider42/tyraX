---
name: tyra-vehicle-modeling
description: >
  How to MODEL a new PS2-ready vehicle (or restyle an existing one) for TyraX
  with a headless Blender script, the way the Ravager in
  examples/vehicle-playground was built: finding Blender, the lofted
  character-line body, the world-space painted texture atlas, the Cycles AO
  bake, the vehicle import's rules (symmetric wheels, material names, budgets,
  glass/interior), and how to check the result in Blender renders, the vehicle
  bake and PCSX2. Use this skill whenever the user asks for a new car, truck or
  bike model, a nicer/restyled vehicle, a vehicle "made in Blender", or asks
  what tools modelling one would need - before writing any geometry code.
---

# Modelling a vehicle for the PS2 path

The worked example is **`examples/vehicle-playground/authoring/make-ravager.py`**
(the Ravager, a late-60s muscle coupe). Copy it and change its design data
rather than starting from nothing: every lesson below is already encoded in it.
`make-pica.py` and `make-strix.py` use the shared `carkit.py` helpers for two
other current vehicle designs.

## 0. Find Blender (never hard-code a path in anything committed)

The script runs INSIDE Blender (`bpy`, `bmesh`, Cycles, the glTF exporter).
Look for it in this order and stop at the first hit:

1. `blender` on PATH (`which blender` / `where blender`), or `$BLENDER` if set.
2. Windows: `C:\Program Files\Blender Foundation\Blender <ver>\blender.exe`, and
   any `<drive>:\Program Files\Blender*\blender.exe` on other drives (people
   install to D:, F:...). A `blender-launcher.exe` is the GUI wrapper - use the
   `blender.exe` NEXT TO it. Also Steam (`...\steamapps\common\Blender\blender.exe`),
   scoop/winget shims. A quick sweep:
   `Get-ChildItem 'C:\','D:\','E:\','F:\' -Filter blender.exe -Recurse -Depth 4 -ErrorAction SilentlyContinue`
   (bounded depth - never walk a whole disk).
3. Linux: `/usr/bin/blender`, `/snap/bin/blender`, `flatpak run org.blender.Blender`,
   `~/Applications`/`~/Downloads` for an extracted tarball. macOS:
   `/Applications/Blender.app/Contents/MacOS/Blender`.

Confirm with `blender -b --factory-startup --version`. If nothing is found,
**ask the user** - to add Blender to PATH, give its location, or install it
(blender.org, `winget install BlenderFoundation.Blender`, or the portable zip).
Downloading it yourself needs their explicit permission (it is ~350 MB).
Blender 4.x/5.x both work; the script was written against 5.1.

Two Blender-5 facts: with `--factory-startup` the render-engine enum lists only
EEVEE, but `scene.render.engine = 'CYCLES'` still works (the AO bake needs it);
and the glTF exporter no longer takes `export_texcoord`/`export_normals` - pass
only `export_format`, `use_selection`, `export_apply`, `export_yup`,
`export_materials`, `export_image_format`.

## 1. The workflow, and where the user comes in

1. **Agree on the style first** (a real reference car "in the spirit of", or
   an original). Do not use a real make/model NAME for the vehicle - the
   Ravager is "like a '69 Charger", not called one.
2. Copy the script, edit the design data (section 2), run it with
   `blender -b --factory-startup --python make-x.py -- --out <tmp>.glb --preview <tmpdir>`.
   It prints `body N tris {per material}; wheel N tris; texture WxH K colours`.
3. **Look at the renders yourself** (front34 / rear34 / side / chase / top /
   cabin + the atlas), fix the obvious, THEN show the user a contact sheet and
   iterate on their verdict. They judge shape far better than you do.
4. Check it against the import (section 4) on a SCRATCH COPY of the example
   under `%TEMP%\tyra-editor-test\` (short path - PCSX2 host: limit), never the
   checked-in project: `tyrax-editor --refresh-gen <copy>` and read the
   `[vehicle] <name>:` lines.
5. Boot it in PCSX2 on the copy and photograph it with the game's own
   `--capture-frame` (section 5). Many defects only show there.
6. Only then add it to the real example: model in `res/models/`, a vehicle
   definition in the `.tyra`, a placed object, `--resave` + `--refresh-gen`,
   the example README, a `preview/<name>.png`, and commit (tyra-docs rules).

## 2. Building the body so it does not look like a box

- **Design 1:1 in metres, export at a SCALE** (the Ravager is 5.2 m real, 0.92
  exported so it fits the district's streets). Blender axes in the script: +X
  forward, +Y left, +Z up; glTF export converts.
- **Loft CHARACTER LINES, not cross-sections of a box.** ~11 longitudinal lines
  per half side (keel, floor edge, rocker, tuck-under, flank, belt crease,
  fender round, sill, greenhouse rail, rail inner edge, crown), each a function
  of x per region (hood / windscreen / roof / rear window / deck). That is what
  buys tumblehome (side glass leaning in), a coke-bottle hip (plan-view width
  and belt height rising over the rear wheel), a crease, a tunnel rear window
  between sails. Keys through a monotone cubic (`pchip`); straight glass gets
  collinear keys.
- **~34 stations, dense where the shape turns**: around each arch
  (`axle +- R * {1,.92,.7,.38,0}`), windscreen base/top, roof end, rear window
  base, both ends. Merge stations closer than 2 cm.
- **Wheel arches**: lift the lower lines onto the arch circle over each axle;
  faces whose corners ALL lie on the arch are well roof -> black cell. Do not
  paint the whole arch range black (it paints the flank's vertical edge and
  looks like mud flaps).
- **Recessed end caps** (grille, tail panel): inset ring + fan, the recess walls
  black.
- Mirror a half; build left, mirror with reversed winding and the SAME UVs.
  Orient every face explicitly against an outward reference vector.
- Normals: `shade_smooth()` + `set_sharp_from_angle(~38 deg)`.
- Small parts (bumpers as swept chrome bars, mirror, exhaust) are cheap boxes -
  keep them SMALL; an oversized mirror reads as a toy.

## 3. The texture: where the look actually comes from

- **One textured material ("body paint") for everything opaque** - paint,
  trim, chrome, grille, interior, wheel wells - so the body stays one submit.
  256x256 atlas: side tile (x, z), top tile (x, |y|), front and rear tiles
  (|y|, z), plus flat CELLS (black, chrome ramp, interior greys). Each face
  projects onto the tile its normal picks; mirrored halves share texels; faces
  facing inward get a flat cell (they would overlap the outer side's texels).
- **Paint in WORLD coordinates** with numpy masks over each tile's world grid,
  4x supersampled, then box-downsampled. Every feature is a mask in metres.
- What makes it read as a PS2-era car rather than a lofted blob: a vertical
  light gradient on the flank; a bright line on the crease lip with a soft
  shadow band under it; shut lines as a DARK gap plus a LIT edge beside it;
  shadowed arch lips; a dark rocker; **chrome as sky-horizon-ground** (light
  top, dark band just below the middle, warm grey bottom) rather than flat
  white; grille bars lit on top and dark below; emblems, plates, markers with
  chrome bezels; vinyl with a little grain.
- **Avoid sub-5 cm repeating detail** (louvres, fine slats): at chase-camera
  distance it aliases into specks. Make vents solid with a frame.
- **Cycles AO bake** into the same UVs (bake with the wheels present so the
  wells darken), blur 3x3, multiply only outside the cells.
- **Texture depth**: ship truecolour and let the vehicle bake fold it. At the
  project's 4 bit a shaded car bands into steps; set the MODEL's Texture depth
  to 8 bit (`Project::textureQuality[<model path>] = "8bit"`, Vehicle Editor >
  Texture depth). There is no 16-bit texture format in the engine. Watch VRAM:
  the Motor District has ~0.12 MB of GS heap free and a 256x256 8-bit texture
  is 64 KB.

## 4. The import's rules (docs/vehicles.md, "Importing a model")

- ONE `.glb`, wheels as FOUR IDENTICAL SEPARATE NODES (one shared mesh), found
  by shape. Name them `wheel front left` etc. anyway. The front overhang must be
  SHORTER than the rear, or the importer guesses the nose wrong.
- **The wheel must be SYMMETRIC** (dish/spokes on both faces): the runtime draws
  all four wheels from one unmirrored mesh, so a one-sided rim shows the tyre's
  open back on the car's right side. ~140-160 triangles - the EE rebuilds the
  wheel batch every frame. Untextured palette materials (tyre / rim chrome /
  rim barrel).
- Material names drive the bake: `headlights` / `rear lights` (the emissive
  lamp part; the runtime overrides their colour, grey when off), anything with
  `glass`/`window` (shiny; the glass part), `rubber`/`trim` (matte).
- Budget: body <= the definition's bodyTris (2400 default); the bake decimates
  above it.
- **Exhaust pipes are EMPTIES named `exhaust*`** (docs/vehicles.md, "Exhaust
  pipes"): one per opening, Single Arrow rotated to point out of the pipe
  (`(0, -90 deg, 0)` = -X, out of the back of a +X-forward car). The nitrous
  flame, the upshift pop and the exhaust smoke leave there. The car scripts list
  the openings as `EXHAUSTS` (design metres) and `carkit.place_exhausts` exports
  them with the body; `authoring/add-exhaust-markers.py` splices the same nodes
  into an already shipped GLB (JSON chunk only, repeatable). The glTF exporter
  puts the arrow on the node's +Y, the FBX one on +Z - the bake knows; an
  unrotated empty (arrow up) reads as "out of the back".
- **Interior + see-through glass**: give the glass its own UNTEXTURED material
  and set the definition's Glass opacity < 1 (docs/vehicles.md, "See-through
  glass"); model a minimal interior (tub, seats, dash, wheel - ~100 tris,
  UV'd into dark cells). With opacity 1 the interior is invisible but harmless.
- The bake reports measured wheelBase / track / radius - they must equal what
  the script built (times SCALE); copy them into the definition's drive block.
  bodyOverhang = max body extent from the axle centre minus wheelBase/2.

## 5. Checking it in the game

- Scratch copy, point an existing definition (or a new one) at the model, move
  the start-in-car flow graph (OnStart -> EnterVehicle) to it so the game boots
  driving it, `--build <copy> --run`.
- Photograph with `tyrax-editor --capture-frame <copy> -o shot.png` (the game's
  own frame, no window, no focus). Swing the camera with a background
  `--pad <copy> "stick r 127 0; wait 5; neutral"` and capture ~2 s in for a
  side view. Crop and upscale the car to judge detail.
- **Ask the user not to touch the pad/emulator during captures** - they may be
  playing it, and a driven car ruins the sequence.
- Stop only YOUR PCSX2, by the project path on its command line.

## Traps this cost

- A shared wheel mesh cannot `transform_apply` (multi-user) - scale the mesh
  data with `mesh.transform(Matrix.Scale(...))` and move the object locations.
- Geo helpers that default `mirror=True` will silently double the wheel; the
  wheel faces must pass `mirror=False`.
- Do not name a local `out` in `main()` if the output path is `out`.
- Writing edit scripts through a bash heredoc eats backslashes here (`\n`
  inside C++ strings turned into real newlines). Write scripts with the Write
  tool (memory: bash-heredoc-backslash-trap).
- Commit only your own files: other sessions may be committing on the same
  branch in the same checkout.

## Authored fast wheels

The three playground models carry a closed, symmetric 144-triangle
`wheel_blur`: full tyre and a concentric blurred dish/hub on both faces.
Avoid alternating spokes (they alias at speed). Re-run
`authoring/add-fast-wheels.py` in headless Blender after generating a car.
It derives dimensions from the ordinary wheel and splices only the new mesh
into the GLB, preserving original geometry/material/image bytes. Repeated runs
replace the previous auxiliary mesh. Check both car sides and authored,
Automatic and None modes in Live preview. `wheel_blur` is reserved and excluded
from chassis/wheel detection in every mode. Automatic budgets can exceed tiny
limits to preserve the tyre; inspect actual bake cost.
