# Reflective materials (sphere-mapped "chrome")

The way PS2-era games faked reflections on car paint and chrome: the GS has no
pixel shaders, so a *spherical environment map* (a small texture of the
surroundings) is sampled per **vertex** with UVs derived from the camera-space
normal, and the mesh is drawn a **second time** with additive blending. The
highlight slides across the surface as the camera moves — that is the whole
effect.

## Authoring

Open *Tools > Material Editor*, pick a material, and in the **Reflection**
section choose a **sphere map** PNG and a **strength** (0–1). A vertical sky
gradient with a bright horizon band makes a convincing default; the map is a
normal power-of-two PNG living next to the `.mtl` (imported like any texture).
Both the Material Editor preview and the scene viewport show the effect live,
with the same math the console runs.

The material file stores it as the standard Wavefront `refl` statement, with
the strength riding in the `-mm` option's gain operand:

```
newmtl chrome
Kd 0.8 0.1 0.1
refl -type sphere -mm 0 0.8 chrome-sky.png
```

Any object using the material — primitives via *Material*, `.obj` models via
their own or an override `.mtl` — gets the reflection pass. Materials without
`refl` are unaffected.

### Rounded normals (flat surfaces)

Matcap UVs come from the surface normal, so a **flat face has one normal →
one sample of the map stretched across the whole face** — a box reflects as
six uniform patches while a sphere sweeps the entire map. Tick **Rounded
normals** in the Reflection section (stored as the TyraX `-rounded` flag,
placed before the filename so last-token parsers stay compatible):

```
refl -type sphere -mm 0 0.9 -rounded @sky
```

The env pass then uses normals **radiating from the part's centroid**
(`normalize(vertex − centroid)`, recomputed at geometry rebuild) instead of
the face normals: every corner of a flat face gets a different UV and the
face sweeps a gradient of the map that pans with the camera — the
curved-lacquer look flat walls and monoliths need. Spheres are unchanged by
construction (their real normals are already radial); base lighting and
geometry are untouched. Zero runtime cost — it is just different data in the
env bag's ST slot.

### Dynamic mode (GT3 style)

Pick **`<dynamic - live sky>`** as the sphere map instead of a PNG (stored as
the filename token `@sky`):

```
refl -type sphere -mm 0 0.9 @sky
```

The game then re-renders the scene's **sky dome** into a small VRAM texture
every frame and samples that as the sphere map — reflections follow the live
sky, including script retints (*Set Sky Color*). The editor viewport
approximates it with the analytic horizon/zenith gradient.

**Objects in reflections:** mark an object's *Show in reflections* checkbox
(Properties; stored as `"reflected": true`) and it is rendered into the env
map too — chrome then mirrors it, GT3-style. Each marked object costs a
second (small, wide-FOV) render per frame, so mark the few props that sell
the effect. The env pass owns a dedicated 128×128 z-buffer, so marked
objects occlude each other correctly inside the map. The editor viewport's
approximation shows the sky only — check object reflections in the game.

A marked object the camera is standing right next to is **skipped** from the
map (within ~1.9× its bounding radius): it would swamp the whole reflection —
typically as the inspected surface's own dark self-reflection, which read as
ugly patches up close. It fades back in as you step away.

## How the PS2 side works

- `LeanObjLoader` parses `refl` (texture + strength) alongside `Kd`/`map_Kd`.
- At geometry build, the generated game captures the **world-space normal** of
  every emitted vertex for reflective parts (`pushVert`, `g_envNormals`).
- The part is submitted a second time as its own `StaPipBag`: same vertex
  array (and `bboxVersion` — the frustum-bbox cache entry is shared), all-white
  colors, the sphere map as texture, a standard GEQUAL z-test (the passes are
  coplanar, so the env pass re-writes the same depths — benign; the
  `TestOnly` alpha-fail trick used at first corrupted close-up frames on the
  EE-clipped path: later objects punched through the reflective surface), and
  `fogDisabled` (GS fog would *add* the fog color through the additive
  equation).
- **The matcap ST math runs on VU1** (phase 2): the env bag's texture bag has
  `coordinatesAreNormals` set, the normals ride in the vertex stream's ST
  slot, and the `TCE` program family (`stapip_cull_tce_vu1.vclpp` /
  `stapip_as_is_tce_vu1.vclpp` / `stapip_clip_tce_vu1.vclpp`,
  `CalculateTyraEnvStq` in `tyra_macros.i`) computes
  `u = 0.5 + 0.5·(n·right)`, `v = 0.5 − 0.5·(n·up)` from the per-mesh camera
  basis uploaded at `VU1_ENV_BASIS_ADDR`. The EE only refreshes two vectors
  per reflective part per frame — in **both** clipping modes: the clip_tce
  variant computes the ST from the normal *before* the Sutherland–Hodgman
  pass, so it interpolates through the cuts like a regular texture
  coordinate (the earlier EE-computed-ST fallback for VU1-clipping scenes
  is gone).
- The additive blend equation travels **in-band**: every StaPip mesh's tag
  block carries a GS `ALPHA` A+D pair (`VU1_ALPHA_ADDR`,
  `StoreTyraGifTags*Alpha`) — alpha-over by default, `Cv = Cs·FIX/128 + Cd`
  with `FIX = strength · 128` when the bag sets
  `PipelineInfoBag::additiveBlendFix`. No FINISH barriers, no practical limit
  on reflective mesh count. (The dynamic pipeline keeps the original macros
  and knows nothing of the ALPHA qword.) Consequence for everything drawn
  AFTER the 3D scene: the GS `ALPHA` register holds whatever the last mesh
  set, so the 2D sprite path pins the standard source-alpha equation in
  every sprite packet (`RendererCore2D::render`) — without it the debug
  HUD font drew additively after a reflective frame and its black outline
  vanished (found on real hardware).
- The dynamic env map is re-rendered **every second frame** (the GT3 cadence
  — the VRAM target persists, and a 25/30 Hz refresh of a blurry 128 px
  reflection is imperceptible), halving the pass's per-frame cost.

The editor's GLSL twin lives in the viewport fragment shader (`uReflOn` block)
— flat normals from screen-space derivatives, the same camera-basis formula.
**When you change one side, change the other** (the usual twin rule).

## Limits (v1) and the planned "pro" version

- Normals are the loaders' **flat per-face normals** (`vn` is ignored, exactly
  like base lighting), so low-poly chrome looks faceted — a disco ball at low
  sphere detail. **Magnified up close this reads as irregular smudges/patches**
  (each facet samples one point of the map; adjacent facets jump across its
  bands). Raise the primitive detail (24 → 48 visibly cleans a hero sphere)
  or model density for smoother highlights.
- An object crossing the screen edge goes PARTIALLY_IN_FRUSTUM and takes the
  EE-clipper path — that cost is per pass, so a reflective object pays it
  twice; expect an FPS dip when a big reflective mesh straddles the edge
  (same engine characteristic as unreflective geometry, doubled). The VU1
  matcap re-normalizes the clipper's lerped normals, so clipped strips
  sample correctly.
- Animated (`.glb`) models and terrain don't take reflections; static
  primitives and `.obj` models do.
- Dynamic mode reflects the **sky only** — scene geometry (terrain, objects)
  is not in the env render. The plumbing (`RendererCoreEnvMap::begin/end` +
  `RendererCore3D::pushEnvView/popEnvView`) supports submitting more bags into
  the bracket if a project ever wants true GT3 surroundings; it costs frame
  time per extra pass.
- Remaining "pro" idea: smoothed normals for the env pass.

## Probe aim: reflected ray (Preferences > Rendering)

The classic pass aims the env camera **level along the player's forward**
from the eye — the GT3 trick, correct for skies and "good enough" for
everything else. **Reflection probe: aim along the reflected ray** replaces
that with **one probe render PER reflective object**, anchored to the
object: the eye→center ray is reflected at the surface, using analytic
shapes —

- boxes / save points / planes → **OBB face** test in the object's own
  frame (live rotation honored), the hit face's normal;
- spheres / cylinders / cones → sphere (radius = half the largest scale
  axis; the exact eye→center hit's normal faces the eye, so the probe
  looks straight back at the player — the crystal-ball look-back);
  models → bounding sphere.

Each object's map re-renders **right before that object draws**,
interleaved on the single shared VRAM target (the env bracket's `begin()`
drains PATH1, so the previous object's draws sample *their* map before it
is overwritten). Because the eye→center pose depends only on positions —
never on where the camera points — reflections **stay put when the player
looks around**, and the pose is continuous per object, so there is **no
smoothing at all**. Two reflective objects side by side show genuinely
different, simultaneously-correct reflections. (Two earlier cuts are
recorded in PROGRESS 157: a crosshair-anchored shared probe decayed to the
classic aim whenever the object left the screen center, and its constant
smoothing trailed the camera by ~20 frames.)

Honest limits: **cost scales with the reflective object count** — every
probe is a full 128² render (PATH1 drain + sky + the reflected list) per
frame; a scene with ten of them will crawl, budget accordingly. A single
110° probe camera still cannot cover the full reflected hemisphere, and
inside split-screen halves probes are skipped (surfaces keep the last
map). Off by default (existing projects keep their look).

## Dynamic env map internals

- `RendererCoreEnvMap` (engine fork): a 128×128×32 render target allocated at
  init **below the texture region** (the bump allocator's FIFO free can never
  reclaim it), exposed as a **VRAM-resident `Texture`**
  (`Texture::vramResident`) that `useTexture` binds directly — no PATH3
  upload, never evicted.
- Per frame, `renderScene` (when any loaded material uses `@sky`):
  `envMap.begin(horizonColor)` drains PATH1 and redirects
  FRAME/SCISSOR/XYOFFSET at the target (z writes masked — the pass shares the
  main z-buffer address), clears it, then the sky dome is submitted under
  `renderer3D.pushEnvView(...)` (square 110° projection along the camera's
  level forward, widened frustum planes), and `envMap.end()` restores the
  frame state (+ TEXFLUSH so the scene samples fresh texels).
- Beware the GIF NLOOP pitfall hit while building this: an A+D giftag whose
  NLOOP undercounts its register writes stalls the GIF forever — the game
  hangs on the loading screen inside `draw_wait_finish()`.
