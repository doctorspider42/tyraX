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

## How the PS2 side works

- `LeanObjLoader` parses `refl` (texture + strength) alongside `Kd`/`map_Kd`.
- At geometry build, the generated game captures the **world-space normal** of
  every emitted vertex for reflective parts (`pushVert`, `g_envNormals`).
- Each frame, `renderScene` derives the camera basis (forward/right/up) and
  rewrites the part's env ST array on the EE:
  `u = 0.5 + 0.5·(n·right)`, `v = 0.5 − 0.5·(n·up)` — a few dot products per
  vertex, negligible for prop-sized meshes.
- The part is submitted a second time as its own `StaPipBag`: same vertex
  array (and `bboxVersion` — the frustum-bbox cache entry is shared), all-white
  colors, the sphere map as texture, `PipelineZTest_TestOnly` (coplanar with
  the base pass), `fogDisabled` (GS fog would *add* the fog color through the
  additive equation).
- The additive blend itself is the TyraX engine fork's per-bag
  `PipelineInfoBag::additiveBlendFix`: a non-zero value makes
  `StaPipCore::render` switch the global GS `ALPHA` register to
  `Cv = Cs·FIX/128 + Cd` around that bag's draw (FINISH-barrier drain before
  and after — keep reflective meshes to a handful per frame), where
  `FIX = strength · 128`.

The editor's GLSL twin lives in the viewport fragment shader (`uReflOn` block)
— flat normals from screen-space derivatives, the same camera-basis formula.
**When you change one side, change the other** (the usual twin rule).

## Limits (v1) and the planned "pro" version

- Normals are the loaders' **flat per-face normals** (`vn` is ignored, exactly
  like base lighting), so low-poly chrome looks faceted — a disco ball at low
  sphere detail. Raise the primitive detail or model density for smoother
  highlights.
- Animated (`.glb`) models and terrain don't take reflections; static
  primitives and `.obj` models do.
- The ST math runs on the EE. Phase 2 (GT3-style "pro" version) moves the
  UV-from-normal computation into the StaPip VU1 microprograms — no EE cost,
  no per-bag pipeline drain — and can add smoothed normals for the env pass,
  or even a dynamically rendered environment texture.
