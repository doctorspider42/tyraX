---
name: tyra-engine-dev
description: >
  Guide to editing the in-tree Tyra PS2 engine fork in vendor/tyra — the
  renderer/clipper/VU1 pipeline, audio (audsrv), file loading over PS2 host fs,
  and how engine changes reach running games through the Docker build. Use this
  skill whenever you touch ANY file under vendor/tyra, work on PS2-side
  rendering, clipping, VU1 microprograms, textures, audio playback or asset
  loading, or when diagnosing in-game symptoms like rendering corruption, giant
  smeared polygons, crackling/wrong-speed audio, or "Failed to load" asserts.
  The pitfalls section records expensive dead ends — read it before attempting
  any PS2 rendering or performance work, even if the change looks trivial.
---

# Working on the in-tree Tyra engine fork

## Fork policy

`vendor/tyra/engine` is a **versioned fork** of [h4570/tyra](https://github.com/h4570/tyra)
(Apache 2.0, forked at upstream commit `9273416`), maintained directly in this
repo. Edit it like normal project code — no patch machinery, no submodule.
Rules:

- Mark every departure from upstream with a `Modified by TyraX` comment
  near the top of the file (grep for existing examples:
  `audio_song.cpp`, `stapip_clipper.cpp`, `planes_clip_algorithm.cpp`,
  `stapip_qbuffer.cpp`, `render_bbox.cpp`, `vcl_sml.i`).
- **LF line endings only** under `vendor/tyra/**` — enforced by
  `.gitattributes`; the `vclpp` VU1 preprocessor chokes on CRLF. Don't fight it.
- The rest of `vendor/` (imgui, glfw, imguizmo, imnodes, stb, and tyra's
  non-engine parts) is git-ignored and cloned by `setup.ps1` — never edit those.

## How an engine change reaches the game

You don't rebuild the engine by hand. The editor's Runner (`src/runner.cpp`)
does it on every game build (F5 or `tyrax-editor.exe --build <projectDir>`):

1. `vendor/tyra` is bind-mounted **read-only** at `/engine-src` in the
   project's container (service `compiler`, container `<name>-compiler-1`).
2. The Runner checksum-rsyncs `/engine-src` into the shared volume
   `tyra-engine-shared` (mounted at `/tyra`), shared by all projects.
3. If anything changed, `libtyra` is rebuilt once (VU1 microprograms are
   force-rebuilt too) and the game ELF is dropped so it relinks.
4. Unchanged engine → the rsync is a no-op and builds take seconds.

So the loop is: edit a file under `vendor/tyra/engine`, run a game build, and
the change is in the ELF. No container restarts needed. If the shared volume
gets into a weird state, `git checkout` inside `/tyra` restores originals (it's
a git checkout of the fork).

## Engine layout

`vendor/tyra/engine/{inc,src}/` mirror each other by subsystem:
`audio` (audsrv music + ADPCM sfx), `debug`, `file`, `info`, `irx`, `loaders`
(PNG, obj/md2), `math`, `pad`, `physics`, `renderer` (the big one — 2D, 3D
static/dynamic pipelines, core GS/VU1 code), `thread`, `time`; plus
`engine.hpp` / `game.hpp` and the VCL/VU1 sources under the renderer
(`*.i`, `*.vcl` — preprocessed by vclpp inside the container).

Editor-specific engine additions so far: Cohen–Sutherland outcodes in the EE
clipper, the StaPip `clip` VU1 program family (on-VU1 Sutherland–Hodgman,
**the default** clipping mode for new projects since M4; the EE clipper stays
selectable as "Precise clipping on EE (legacy)" and remains the load-time
default for pre-M4 `.tyra` files without a `clipping` key — design + status
in `docs/vu1-clipping-plan.md`), static pools in `stapip_clipper.cpp` /
`stapip_qbuffer.cpp`,
`RendererCorePostFx` (bloom + film grain + depth of field + god rays via GS
blits — god rays (`PassGodRays`, `setGodRays` strength + per-frame
`setGodRaysSun` screen position/visibility fed by the game) bright-pass the
frame on the quarter-res buffers (subtract flat threshold 150, double back
up - 96 washed the whole frame white, the sky IS bright) and iteratively
zoom it toward the sun (2 ping-pong passes, s=0.72) before compositing
additively at half the requested strength; DoF
(`PassDof`, authored in the UI Editor / overridden by the Set Depth Of Field
flow node) reuses the bloom blur chain and composites the blur back through
full-screen sprites drawn at real GS depths under the pass's GEQUAL z-test,
world distance → GS z solved from the shared perspective matrix; it must run
right after the 3D scene, before ANY 2D — sprites stamp z = max across their
whole rect and punch sharp rectangles into a later z-tested pass; plus
`applyCustom()` +
`RendererCore::applyCustomPostFx()` for user-authored full-screen effects — see
the editor's custom screen effects, `docs/custom-screen-effects.md`; the effect
body appends GS primitives through the now-public `blit()`/`flatQuad()` and the
framebuffer/noise/scratch-buffer accessors, and the engine wraps the state
setup/teardown + DMA kick), WAV-header-aware song
player, `bboxVersion` on `StaPipBag` for moving geometry, `LeanObjLoader`
(OBJ+MTL, host:/cdrom0:-safe; parsing semantics mirror the editor's
`src/objparser.cpp` — keep the two in sync; parses the `refl` sphere-map
statement for reflective materials incl. the TyraX `-rounded` flag:
centroid-radial env normals for flat surfaces), per-bag additive blending for the
reflective-material env pass (`PipelineInfoBag::additiveBlendFix` — non-zero
makes `StaPipCore::render` drain PATH1 via `sync.align3D()` and switch the
global GS `ALPHA` register to `Cs*FIX/128 + Cd` through
`RendererCoreGS::setAlpha`, restoring alpha-over after the bag's own drain;
superseded by the in-band per-mesh ALPHA qword: `VU1_ALPHA_ADDR` +
`StoreTyraGifTags*Alpha` — every StaPip mesh's tag block carries its blend
equation, no barriers; dynpip keeps the original 7/5-qword macros), the
StaPip `TCE` env program family (matcap ST from normals in the ST slot +
`StaPipTextureBag::coordinatesAreNormals` + the camera basis at
`VU1_ENV_BASIS_ADDR`), `RendererCoreShadowMap` (projected silhouette
shadows: 4 lazy-allocated 64×64 VRAM slots + one shared cleared z, the env
map's raster-redirect bracket per caster - begin(slot) per caster, ONE end();
the game re-submits the caster's existing bags under a `pushEnvView` "light
camera" and draws a terrain patch sampling the slot's VRAM-resident texture
by light-space UVs; `allocate()` is called by generated games only when a
project has "Cast shadow" objects, and init() re-places the buffers after a
display-mode VRAM reset), `RendererCoreEnvMap` (128×128 VRAM render target for
GT3-style dynamic reflections: FRAME/SCISSOR/XYOFFSET/ZBUF redirect bracket
with a dedicated 128×128 z-buffer — "reflected" scene objects submitted
inside the bracket occlude correctly — + `RendererCore3D::pushEnvView/
popEnvView`; exposed as a VRAM-resident `Texture::vramResident` that
`useTexture` binds without a PATH3 upload — see
`docs/reflective-materials.md`), `Sprite::additive` (2D sprites can opt into the additive blend equation
Cs*As + Cd - lens-flare ghosts, glows; Renderer2D pins alpha-over per sprite
otherwise), the **scene dynamic lights** registry
(`RendererCore::dynLights[8]` + `clearDynLights`/`addDynPointLight`/
`pickDynLight`; the color VU1 programs have ONE spot-light slot per mesh, so
`StaPipCore::render` picks the strongest contributor - flashlight or point
light - per bag on its world bounding sphere and routes it through
`StaPipQBufferRenderer::setBagLight`; a *point* light is expressed through
the SAME spot-cone constants - zero direction, `cosCut2 = -1`, saturated
`invSoft` - so no VU1 program changed and no micro memory was spent),
`physics/CollisionMesh` (XZ-grid
triangle collider) + `Ray::intersectTriangle`, a guard in `debug.cpp` so
TYRA_LOG never opens `cdrom0:LOG.TXT` for write (that wedged every ISO boot),
`renderer/models/unique_id.hpp` (`generateUniqueId()`) replacing upstream's
`rand() % 1000000` object ids (see the pitfall below), and a **quiet-halt
assert** (`debug/debug.hpp` `TyraDebug::trap`): a failed `TYRA_ASSERT` /
`TYRA_TRAP` no longer runs upstream's `init_scr()` + infinite `scr_printf` loop
that seized the whole screen; it still prints the dump to the console / host
`log.txt` (with the stable `======= TYRA =======` … `================`
delimiters the editor parses), then `for(;;) SleepThread()` so the last frame
stays up and the editor's error dialog surfaces it. The full-screen dump is
gated behind `Tyra::Info::drawAssertScreen` (default **off**, `info.{hpp,cpp}`)
for standalone hardware debugging — nothing wires it on in generated games.
Paired with that, a **`TYRA_SOFT_ERROR`** macro (also `debug/debug.hpp`) logs
the *same* delimited `====== TYRA ======` block a fatal assert does — so the
editor surfaces it identically — but with a `Non-fatal error (game keeps
running)!` header and **no halt**, for recoverable asset failures. The asset
loaders use it: `png_loader.cpp` returns an 8x8 magenta placeholder
(`makePlaceholderTexture`) on a missing/empty texture instead of trapping, and
`audio_song.cpp` skips a missing music WAV (`load()` returns early, `play()`
no-ops when unloaded). The fork's other loaders were already non-fatal —
`LeanObjLoader`/`TanmLoader`/`TskLoader` return `nullptr` on a bad file (callers
null-check) and `AudioAdpcm::load` returns `nullptr` + `tryPlay` guards — so a
missing model/anim/sfx already skips cleanly. **When adding a new asset loader,
follow this pattern** (soft-error + safe fallback), don't `TYRA_ASSERT` on a
missing file. Note: legacy `md2_loader` / TinyObjLoader `obj_loader` still assert
— fine, generated games don't use them.

## Hard-won pitfalls (dead ends already explored — don't repeat them)

**Rendering**
- **Never submit bags with `frustumCulling = None`.** Off-screen geometry wraps
  the GS 4096-px raster window → "objects render twice / giant smeared
  polygons". PCSX2's HW renderer often *masks* this; the SW renderer and real
  hardware show it. This was the root cause of a long-standing corruption bug —
  not the clipper patches (all were bisected; even pure upstream reproduced it).
- **Widening the cull programs' ADC test is retired; real VU1 clipping is a
  separate program family.** Three attempts at a guard band inside
  `PerformClipCheck` all corrupted ADC bits (documented in `vcl_sml.i`); the
  working approach is the StaPip `clip` programs (the default `"clipping":
  "vu1"` mode), which never derive ADC from clip flags. VU1 microcode traps already
  paid for there: `fcand` sets VI01 to 0/1 (any-bit), NOT the masked bit
  pattern; a vertex clipped to exactly |x| = w scales to GS coordinate 4096.0
  and wraps the 12.4 XYZ2 field (clip the sides at 0.9w, let the scissor
  finish); every VU1 vertex-loop count must be a multiple of 3 or the loop
  runs off into memory; overflowing the 2048-instruction micro memory is
  SILENT in release builds (the assert is compiled out) - check program sizes
  with nm on the .o files when adding VU1 code. Two vclpp preprocessor traps
  (each cost a debugging session): a `;` comment line INSIDE a `#macro` body
  makes vclpp SILENTLY swallow the whole macro — every call site expands to
  nothing, the program builds fine and just misses your code (document above
  the `#macro` line instead); and vclpp does not expand nested macro calls —
  a macro invoking another macro leaves mangled text that `vcl` rejects with
  "can't find instruction" (inline the callee by hand); and vclpp expands
  `#define`s only ONE level — an alias define (`#define A B` where B is
  another define) reaches dvp-as unresolved ("unresolved expression"), so
  VU1-side defines must be literals. When VU1 output looks
  wrong, `docker exec <proj>-compiler-1 cat /tyra/engine/obj/.../<prog>.o.vcl`
  shows exactly what vclpp produced — check the expansion before suspecting
  your math.
- **A GIF A+D giftag whose NLOOP undercounts its register writes stalls the
  GIF forever** — the stray qword parses as a new giftag with a garbage
  NLOOP. Symptom: the game hangs on the loading screen (spinning in
  `draw_wait_finish()` / a FINISH handshake that never arrives), no assert,
  clean log. Count the qwords after every PACK_GIFTAG edit.
- The engine bbox cache is keyed by bag pointer — geometry that changes at
  runtime must bump `bboxVersion` on its `StaPipBag`, or culling uses stale
  boxes.
- Upstream's default `PlanesClipAlgorithm::clipMargin` pushes the near plane
  ~10 units from the camera; generated games override it.
- Judge rendering correctness on **PCSX2's software renderer** — it is the
  honest one. See tyra-testing for how.
- **Object ids must be unique, not random.** `Sprite`/`Mesh`/`MeshFrame`/
  `MeshMaterial`/`MeshMaterialFrame` share ONE lookup namespace in
  `TextureRepository`: `addLink(id)` binds a texture to a sprite/material and
  `getBySpriteId`/`getByMeshMaterialId` return the FIRST texture whose links
  contain that id. Upstream drew ids from `rand() % 1000000` (never seeded), so
  a collision bound the wrong texture to a sprite → garbled/black HUD sprites,
  worst right after opening a menu (a burst of new sprites raises the collision
  odds against the always-present debug-HUD glyph). Fixed with
  `renderer/models/unique_id.hpp` `generateUniqueId()`. Use it for any new
  id-bearing render object; don't reintroduce `rand()` ids. (`audio_song.cpp`
  intentionally keeps `rand()` — separate namespace, assigned off-thread.)
- **DTV display modes (480p/1080i)**: ps2sdk's `graph_set_screen` always
  programs the mode's full VCK width into DISPLAY.DW, and no 64-aligned
  framebuffer width divides the 1440/1920-VCK DTV rasters — the GS scans
  garbage past the buffer's right edge. `RendererCoreGS::setDtvDisplay`
  programs DISPLAY1/2 directly instead. Also: the gsKit/OPL 1080i recipe
  (interlaced FRAME mode + MagV--) **hard-crashes PCSX2 v2.3.205** (the
  process dies seconds after SetGsCrt, no crash dialog); 1080i in FIELD
  mode with MAGV=2x is visually equivalent (both fields step through every
  buffer line) and works.
- **Runtime display switching**: `RendererCore::setDisplayOutput(mode, ws)`
  (TyraX fork) switches the scan mode / widescreen between frames.
  A mode change resets the whole VRAM bump allocator (`vram.reset()`),
  rebuilds frame/z buffers + post fx, and `texture.evictAll()` drops every
  texture allocation (they lazily re-upload) — never call it mid-frame.
  The projection aspect lives in `RendererSettings::updateGeometry`
  (fixed 4:3-baseline look; widescreen scales it anamorphically).
- **`endFrame` only throttles when it renders.** It calls `graph_wait_vsync()`
  (gated by `isFrameLimitOn`, default true) then flips buffers — so a loop that
  presents a frame each iteration is paced to 50/60 Hz, but a loop that draws
  *nothing* (no `beginFrame`/`endFrame`) is not, and PCSX2 races the EE ahead of
  the display. Consequence for timed holds: a COP0-`Count`-based wait (294.912
  MHz) measures wall time correctly **only while frames are being drawn**; a
  no-draw busy-wait finishes in a fraction of the intended time. This is why the
  boot Tyra splash (`info/banner.cpp`) holds ~2s by re-rendering the logo in a
  COP0-timed loop rather than sleeping — re-drawing is also required because
  `beginFrame` clears the framebuffer, so you cannot "hold" a previous frame by
  doing nothing. The generated games' boot sequence relies on the same fact:
  the first `loadScene` runs from the loop (not `init()`) so its loading-screen
  progress is vsync-paced (see the editor's loading-screen feature).

**Audio**
- audsrv streams PCM only; ADPCM is for one-shots (`adpcm.tryPlay`), and ADPCM
  voices cannot be stopped — the editor round-robins SPU channels to avoid
  drop-outs. Channels 16–23 are reserved by generated games for sound emitters.
- WAV files: 8-bit PCM is unsigned (0x80 = silence) but audsrv mixes signed —
  convert (XOR 0x80) or it wraps at every zero crossing (loud crackle at
  correct pitch — that exact symptom happened).
- Mono/low-rate streams need smaller chunk size + fill threshold or audsrv's
  ring buffer starves.

**Files / assets**
- `fseek`/`ftell` are unreliable over the PS2 host filesystem — the WAV parser
  walks RIFF chunks in memory for this reason. Prefer read-into-memory parsing.
- Textures must be power-of-two sized; PNG 32/24bpp or palletized 8/4bpp
  (palletized is fastest on PS2).
- `cdrom0:` paths differ from `host:` paths — ISO9660 uses `\`, upper-case and
  a `;1` version suffix; `FileUtils::fromCwd` and the extension helpers handle
  the conversion. Test asset-loading changes on BOTH boot paths (host: via
  normal Build & Run, cdrom0: via Export PS2 ISO).

**Build environment**
- PS2SDK's `math3d.h` `#define`s names like `LIGHT_AMBIENT` — prefix your
  constants (the codebase uses `SCENE_*`).
- The compiler is `mips64r5900el-ps2-elf-g++` inside the `h4570/tyra` image;
  there is no way to compile engine code on the host. Even a syntax check
  requires a game build (see tyra-testing).

## Performance context

The 98k-vertex benchmark scene went 12 → 50 FPS through: outcode early-out in
the EE clipper, static pools (no per-call heap), and finally exact per-package
frustum classification. The StaPip packager's per-submit `new[]`/`delete[]`
package arrays are pooled too (grow-only vectors in `StaPipBagPackager`;
callers must not free the result) — measured worth only ~2% of the partial
branch in PCSX2: the real cost of PARTIALLY_IN_FRUSTUM geometry is the
per-package bbox classification + EE clipping, which scale with vertex count
(see PROGRESS entry 79: a single detail-16 box near the camera = 9.2k verts =
~36 ms of EE; the fix was authoring-side detail, not the allocator).
**Classification itself was then rebuilt (PROGRESS entry 100)**: the frustum
planes are transformed into the bag's object space once per bag
(`CoreBBox::computeObjectSpacePlanes`) and every package/subpackage is
classified with the p-vertex/n-vertex AABB test on min/max corners
(`CoreBBox::frustumCheckAABB`) — no per-package corner transforms, no merged
8-corner boxes, and `clipFrustumCheck`'s duplicate zero-guard-band re-check
is gone. Plus a VU0 `vmini`/`vmax` min/max scan in
`CoreBBox(const Vec4*, count)` and `memcpy` fills in
`StaPipQBuffer::fillByCopy*`. Net: 47 → 73 FPS on the vsync-off 98k
benchmark, pixel-identical; the hidden `"clipping": "vu1"` mode hits 120 FPS
on the same scene now that classification is cheap. When touching
classification, mind the AABB invariant: every CoreBBox the packager sees is
axis-aligned with `vertices[0]`/`vertices[7]` as min/max — only the
matrix-transform constructor breaks that, and it must never feed the AABB
test. Known next target (from PROGRESS.md backlog): retire the EE clipper —
flip `"clipping"` to vu1 by default (M4 in docs/vu1-clipping-plan.md, gated
on a real-PS2 pass).
Measure with PCSX2's FPS display on the software renderer, 3+ samples, before
and after; pixel-compare screenshots to prove output is unchanged.
