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

## How the PS2 side works

- `LeanObjLoader` parses `refl` (texture + strength) alongside `Kd`/`map_Kd`.
- At geometry build, the generated game captures the **world-space normal** of
  every emitted vertex for reflective parts (`pushVert`, `g_envNormals`).
- The part is submitted a second time as its own `StaPipBag`: same vertex
  array (and `bboxVersion` — the frustum-bbox cache entry is shared), all-white
  colors, the sphere map as texture, `PipelineZTest_TestOnly` (coplanar with
  the base pass), `fogDisabled` (GS fog would *add* the fog color through the
  additive equation).
- **The matcap ST math runs on VU1** (phase 2): the env bag's texture bag has
  `coordinatesAreNormals` set, the normals ride in the vertex stream's ST
  slot, and the `TCE` program family (`stapip_cull_tce_vu1.vclpp` /
  `stapip_as_is_tce_vu1.vclpp`, `CalculateTyraEnvStq` in `tyra_macros.i`)
  computes `u = 0.5 + 0.5·(n·right)`, `v = 0.5 − 0.5·(n·up)` from the
  per-mesh camera basis uploaded at `VU1_ENV_BASIS_ADDR`. The EE only
  refreshes two vectors per reflective part per frame. (Scenes running the
  hidden `"clipping": "vu1"` mode fall back to EE-computed STs — that
  program set has no env variant.)
- The additive blend equation travels **in-band**: every StaPip mesh's tag
  block carries a GS `ALPHA` A+D pair (`VU1_ALPHA_ADDR`,
  `StoreTyraGifTags*Alpha`) — alpha-over by default, `Cv = Cs·FIX/128 + Cd`
  with `FIX = strength · 128` when the bag sets
  `PipelineInfoBag::additiveBlendFix`. No FINISH barriers, no practical limit
  on reflective mesh count. (The dynamic pipeline keeps the original macros
  and knows nothing of the ALPHA qword.)

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
- Dynamic mode reflects the **sky only** — scene geometry (terrain, objects)
  is not in the env render. The plumbing (`RendererCoreEnvMap::begin/end` +
  `RendererCore3D::pushEnvView/popEnvView`) supports submitting more bags into
  the bracket if a project ever wants true GT3 surroundings; it costs frame
  time per extra pass.
- Remaining "pro" idea: smoothed normals for the env pass.

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
