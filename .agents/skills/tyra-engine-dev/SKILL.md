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

> **A note on `PROGRESS 123` citations.** They point at numbered entries of
> `PROGRESS.md`, retired at ~15 800 lines. They remain exact pointers — the file
> is in git history, and `docs/backlog.md` has the recipe. New work records
> itself in its commit message and PR body instead.

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
  non-engine parts) is git-ignored and cloned by `setup.ps1` / `setup.sh` — never edit those.

## How an engine change reaches the game

You don't rebuild the engine by hand. The editor's Runner (`src/runner.cpp`)
does it on every game build (F5 or `tyrax-editor.exe --build <projectDir>`):

1. `vendor/tyra` is bind-mounted **read-only** at `/engine-src` in the
   project's container (service `compiler`, container `<name>-compiler-1`).
2. The Runner checksum-rsyncs `/engine-src` into the shared volume
   `tyra-engine-<hash of the engine source path>` (mounted at `/tyra`), shared
   by every project built from the same checkout — parallel worktrees get their
   own, or they would rsync diverging engines over each other forever. Because
   it is shared it is declared **`external: true`** in the generated
   `docker-compose.yml` and created by the Runner (`docker volume create`,
   idempotent) rather than by compose: a volume compose creates is labelled with
   whichever project got there first, and every other project then warns on
   every `up` that it "already exists but was created for project X".
3. If anything changed, `libtyra` is rebuilt once and the game ELF is dropped so
   it relinks.
4. Unchanged engine → the rsync is a no-op and builds take seconds.

**The VU1 microprograms are the expensive special case.** They sit outside
make's dependency tracking (a `.vclpp` `#include`s `.i`/`.h` files nothing
declares), so the Runner force-rebuilds them — but only when the rsync reports a
changed `.vclpp`/`.vcl`/`.vsm`/`.i`/`.h`. Rebuilding the set costs **109 s**
(measured, `-j6`; `vcl` is slow enough that its optimizer times out on some
programs and says so). It used to run on *any* engine change, so a one-line
comment in a `.cpp` paid it in full; now that same edit is a ~7 s build. If you
ever change what a VU source may include, widen that extension list in
`runner.cpp` — a missed extension means stale microprograms, which is the kind
of bug that looks like a renderer bug.

So the loop is: edit a file under `vendor/tyra/engine`, run a game build, and
the change is in the ELF. No container restarts needed. If the shared volume
gets into a weird state, `git checkout` inside `/tyra` restores originals (it's
a git checkout of the fork) — and **Build > Rebuild** (`--build --rebuild`)
throws away the whole compiled engine, VU1 objects included, and builds it
again from source.

`vendor/tyra/Makefile.base` is shared by the engine build and every generated
game, so an edit there moves both. TyraX changes in it: single-pass dependency
generation (`-MMD -MP`; it used to run the compiler a second time per file just
to write the `.d`), `| directories` order-only prerequisites so `-j` cannot
reach an absent `bin/`, `cp -ru` for the resource copy, and **`src/vu/` and
`src/vu0/` excluded from `SOURCES`** - those are HOST C++ (a project's own VU1
programs and VU0 kernels, docs/vu-authoring.md), compiled and run at build time
by the container's g++, and handing them to the PS2 compiler fails on the very
first include. A new host-code directory has to be added to that `find`
exclusion or its first file breaks every build that has one. Verified
byte-identical: the same project built with the old and new rules produced the
same `md5` for its stripped ELF.

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
additively at half the requested strength; bloom
takes an optional **bright-pass threshold** (`setBloomThreshold`), one extra
quarter-res sprite subtracting a flat grey from the downsampled frame through
`(0 - Cs)*128/128 + Cd`; the GS clamps at zero, so sub-threshold pixels drop
out of the blur and the halo collapses onto emissive materials instead of
veiling the frame (`docs/emissive-materials.md`), plus a **spread**
(`setBloomSpread`, 1..4 soften rounds with doubled tap offsets each, buffers
ping-ponging) that grows the halo into a corona for 4 sprites a round. NOTE the
postfx `packet2_create` size (512 qwords) is sized for the WORST case of every
pass at once — a blit is 12 qwords, a flat quad 7; an undersized packet
corrupts the GIF stream, so grow it whenever a pass gains primitives — DoF
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
player, `bboxVersion` on `StaPipBag` for moving geometry,
`StaPipBag::packageSize` (0 = derive) pinning coplanar passes over one vertex
array to identical package boundaries — see the pitfall below, `Pad::setActuators` (act-direct DualShock rumble —
the on/off buzz motor + 0-255 heavy motor — behind the Vibrate Pad flow node /
`padVibrate()` script helper), `LeanObjLoader`
(OBJ+MTL, host:/cdrom0:-safe; parsing semantics mirror the editor's
`src/objparser.cpp` — keep the two in sync; parses the `refl` sphere-map
statement for reflective materials incl. the TyraX `-rounded` flag:
centroid-radial env normals for flat surfaces; parses `Ke` as the emission
floor of emissive materials (`docs/emissive-materials.md` - the
`# tyra-glow*` hint lines are editor-side only, the baked tables carry
everything the game needs); parses the TyraX
`# tyra-uvrect u0 v0 du dv` hint the texture-atlas bake writes into baked
.mtl files (docs/texture-atlasing.md) - model vertex UVs multiply through
it at load and `LeanMtlMaterial::uvRect` exposes it for the generated
game's primitive builders; quietly picks up the TyraX
`<model>.aov` baked-ambient-occlusion sidecar — "TXAO" + u32 count + one
visibility byte per obj `v` entry, docs/ambient-occlusion.md — into
per-vertex `vertexAo` bytes the generated game folds into its shade bake),
**`TmdlLoader`** (`loaders/3d/tmdl_loader/`, docs/model-pipeline.md — the
binary static-model format the generated game actually loads; the ASCII
`LeanObjLoader` above is now only the fallback path. Reads the whole file
sequentially and memcpys each part, because the stored layout IS
`LeanObjMaterial::vertices` — everything else (triangulation, flat normals,
material assignment, atlas UV rects, texture paths, LOD tiers) was resolved
by the editor's `templates::bakeStaticModels`; the layout lives in the
editor's `src/tmdl.hpp`, **keep the two in sync**. It returns the same
`LeanObjMesh` so the game keeps one geometry path, with two differences the
caller must know: texture names are already **cwd-relative** — do NOT prepend
a directory — and `LeanObjMaterial::lods` may carry decimated tiers, which an
`.obj` never has. Loading a 9216-vertex model went 286 ms -> 39 ms), per-bag additive blending for the
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
`VU1_ENV_BASIS_ADDR`), the StaPip `billboard` program family
(`StaPipBillboardBag`: the vertex slot carries PARTICLE CENTERS, the ST slot
one qword of 2×2 basis weights per particle, colors one per particle; VU1
expands each center into a camera-facing quad — 6 GS vertices — from the
camera right/up basis at `VU1_BILLBOARD_BASIS_ADDR` transformed by the MVP
once per mesh, and culls per QUAD with one `clipw` judgement per corner.
The two programs are NOT resident: the VU1-clipping program set fills micro
memory to ~2036/2042, so they live in their own packet swapped in on demand
(`StaPipQBufferRenderer::ensureProgramSet`) and the resident set is lazily
restored by the next non-billboard bag. The C++ side must keep the prim
giftag NLOOP at 6× the input count (`gsVertexCount`) — an undercounting
NLOOP stalls the GIF. Billboard bags require multi-color, no lighting,
frustum culling `None` + no clip checks (the one legitimate `None` — the
program's per-quad ADC replaces the wrap protection), and a texture bag whose image may
be null — it carries the params channel; swapping `right`/`up` on the bag
and re-rendering draws the same centers for another view, which is what a
portal through-view pass needs), `RendererCore::camFeed` (a second `RendererCoreEnvMap`
instance for the camera texture feeds — CCTV monitors, docs/
texture-feeds.md; permanently allocated below every texture like the env
map, +128 KB VRAM, Clamp wrap because feeds sample through plain surface
UVs — which also read the raster target UPSIDE DOWN unless the surface V
flips: GS rows run top-down, texture V grows down from row 0),
`RendererCoreEnvMap` (128×128 VRAM render target for
GT3-style dynamic reflections: FRAME/SCISSOR/XYOFFSET/ZBUF redirect bracket
with a dedicated 128×128 z-buffer — "reflected" scene objects submitted
inside the bracket occlude correctly — + `RendererCore3D::pushEnvView/
popEnvView`; exposed as a VRAM-resident `Texture::vramResident` that
`useTexture` binds without a PATH3 upload — see
`docs/reflective-materials.md`), `Sprite::additive` (2D sprites can opt into the additive blend equation
Cs*As + Cd - lens-flare ghosts, glows; Renderer2D pins alpha-over per sprite
otherwise), **`Sprite::drawSize`** (a per-axis destination size, 0 = the stock
`size * scale`; it exists because the framebuffer is a different SHAPE per scan
mode - 512x448 interlaced, 448x448 in 480p, 448x540 in 1080i, 512x512 in full
PAL - so UI authored in one logical space must be drawn into a differently
proportioned rect per mode. Neither existing field could express that: `scale`
is one float, and in `MODE_REPEAT` `size` is the SOURCE rect, so changing it
would sample different texels. Used by the generated menu compositor -
docs/menu-styles.md "Resolutions"), **`RendererSettings::getWindowAspect()`**
(the PHYSICAL shape of the display window on the TV - 4:3, 16:9 when widescreen
is on, the pillarboxed 1792/1920 window in widescreen 1080i. `updateGeometry`
already computed it to derive the projection aspect; it is exposed because 2D
needs it too. Widescreen is ANAMORPHIC, so a sprite gets stretched a third wider
with nothing to widen it back - anything that must keep its authored proportions
divides its horizontal scale by this over 4:3, which is what the generated menu
compositor and the cutscene letterbox masks do), the **scene dynamic lights** registry
(`RendererCore::dynLights[8]` + `clearDynLights`/`addDynPointLight`/
`pickDynLight`; the color VU1 programs have ONE spot-light slot per mesh, so
`StaPipCore::render` picks the strongest contributor - flashlight or point
light - per bag on its world bounding sphere and routes it through
`StaPipQBufferRenderer::setBagLight`; a *point* light is expressed through
the SAME spot-cone constants - zero direction, `cosCut2 = -1`, saturated
`invSoft` - so no VU1 program changed and no micro memory was spent;
`PipelineInfoBag::dynLightPick = false` opts a bag out of the pick - the
generated games set it on TERRAIN CHUNKS (neighboring chunks picking
different lights truncate a pool in a hard rectangle at the chunk border;
the lights' ground pools draw as smooth additive patches instead) and on
the sky dome (camera-centered - a nearby light would tint the whole sky)),
`docs/reflective-materials.md`), the TyraX portal through-view machinery
(`RendererCore3D::pushPortalView` — view-matrix-only swap, main projection
kept, exact frustum planes at the virtual camera — plus
`RendererCore::portalViewBegin/End` → `RendererCorePostFx::portalMask*`:
the destination scene renders IN-PLACE into the real framebuffer right
after the frame clear, scissored to the quad's screen bbox, and the shaped
opening is carved with reversed-z ops since the GS has no stencil: re-far
the bbox z, cap the quad interior with a z-only ALWAYS fan at the surface
depth, repaint the still-far ring via a GEQUAL sprite at z=0 that hits
exactly the reset pixels; both wrappers drain PATH1 unconditionally and do
NOT latch the post-fx drain gate; maskEnd's NLOOP is 13 + fan verts; see
`docs/portals.md`), the **VU0 micromode ray tracer**
(`renderer/rt/vu0_raytracer.{hpp,cpp}` + `vu0_rt_kernel.vclpp`, exported by
the `<tyra>` umbrella header — raytraced mirror reflections,
`docs/raytraced-reflections.md`): the ONLY VU0 microprogram in the codebase —
built through the same vclpp/vcl/dvp-as pipeline as the VU1 programs but
uploaded by the EE to VU0 micro memory (0x11000000) and kicked with
`vcallms 0` per image row, params/results through VU0 data memory
(0x11004000), sync by polling VPU STAT bit 0 (`cfc2 $29`). VU0-honest
kernel: no XGKICK/EFU/xtop; branchless nearest-hit via saturation step
masks (`clamp(x*1e38,0,1)` — VU floats saturate, no inf/nan). Runs
synchronously: macro-mode COP2 (Vec4/M4x4) shares VU0's register file, so
the tracer never overlaps engine math, and clobbering VF01+ between kicks
is safe because every macro op reloads its operands,
`Texture::sourcePath` (the full load path, set by
`TextureRepository::add` - `name` keeps only the basename; the generated
texture hot-reload poller `live_tex.gen.cpp` matches repainted files against
it and re-uploads via `RendererCoreTexture::updateTextureInfo` to the SAME
VRAM address - see docs/live-link.md; NOTE the engine always constructs the
clut `TextureData`, a non-paletted texture just has `clut->data == nullptr` -
test data presence, not the object pointer),
`physics/CollisionMesh` (XZ-grid
triangle collider) + `Ray::intersectTriangle`, a guard in `debug.cpp` so
TYRA_LOG never opens `cdrom0:LOG.TXT` for write (that wedged every ISO boot),
`renderer/models/unique_id.hpp` (`generateUniqueId()`) replacing upstream's
`rand() % 1000000` object ids (see the pitfall below), **USB keyboard/mouse
input** (`pad/kbd_mouse.*` — `Engine::kbdMouse` polls the PS2SDK `ps2kbd`
(raw mode, 256-bit HID-code bitmap) and `ps2mouse` (DIFF mode: per-frame
deltas + button mask) drivers, loaded by the IrxLoader behind
`EngineOptions::loadUsbKbdMouse`; `Pad::injectVirtual` overlays a virtual
pad — held buttons OR into the polled state, click edges derived from the
previous overlay, stick offsets clamped — which is how generated games map
keys onto the pad. It takes an overlay **slot** (`Pad::VIRT_SLOTS`, one
`virtPrev` per slot): 0 is this keyboard/mouse fold, 1 is the editor's Remote
Pad (docs/remote-pad.md). A second source must never reuse slot 0 — the two
would each read as the other releasing everything, so every held button
re-clicks every frame; **skipped under ps2link**: ps2kbd/ps2mouse import usbd's
symbols and drivers added to an already-running ps2link's un-reset IOP never
come up cleanly (PS2MouseInit then spins forever on an RPC server that never
registered — a boot freeze on the Tyra logo). The
`loadUsbKbdMouseUnderPs2Link` option instead targets the **TyraX ps2link**
(`tools/ps2link` — bakes usbd+ps2kbd+ps2mouse into ps2link's OWN boot; it is
the ONLY ps2link the editor deploys to, so the option is on by default —
docs/ps2link-setup.md): the engine then loads NO USB modules and reuses that
resident stack. `KbdMouse::init(underPs2Link)` guards PS2MouseInit on the
keyboard device having opened, so running it against a stock ps2link logs
instead of hanging. Two real-hardware traps found the hard way: PS2MouseData must be
zero-initialised (a real mouse sends no packet on a still frame, so garbage
read as a constant delta spins the camera — PCSX2 never shows it) and the
read mode is set to DIFF explicitly; the IrxLoader gives HID a fixed settle
delay after load since USB enumeration is async on real hardware but instant
in PCSX2. **Hardware-unconfirmed** beyond "drivers ready" — the test devices
didn't speak USB HID boot protocol), **two-player support**
(docs/multiplayer.md): `Pad::initOptional(port, slot)` (padInit is now
once-global; an optional pad never blocks or asserts on a missing controller —
upstream `update()` busy-waited forever on DISCONN — and keeps polling for a
hot-join; sticks are centered while disconnected) plus
`RendererCoreSplitView` (`renderer/core/splitview/`, public
`RendererCore::splitView`): the split-screen raster bracket modeled on the
env-map redirect — per half it drains PATH1 and shifts XYOFFSET so the
CENTRAL h/2 rows of the *unchanged* full-screen projection land on that half
(a vertical crop; no projection change, proportions stay exact), scissored to
the half. Deliberately NO per-half clear: beginFrame's full-screen clear
covers both halves and the scissor clips every raster write (z included) —
the first version copied the env map's clear sprite and paid two half-screen
GS fills + FINISH stalls per frame for nothing. The game swaps cameras
between halves with `renderer3D.update(cam2)` (the pipelines read
view/frustum lazily per mesh) and must run animation advance/skinning only
in the FIRST half (see the generated game's `splitSecondPass`). Mind the
interplay: any bracket that restores a full-screen raster (env map!) must
not run inside a split half. And a **quiet-halt
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

## `RendererCoreBlss` — the neural upscaler's console half

`renderer/core/blss/` (docs/neural-upscaler.md; the arithmetic is
docs/blss-reconstruction.md). The 3D scene renders into a half-resolution VRAM
target and a per-tile MLP decides how to reconstruct it. Reached as
`engine->renderer.core.blss`; generated games call `configure()` + `setNet()` in
`init()` and bracket their scene with `beginScene()` / `endScene()` /
`composite()`. Inert and zero-VRAM when a project has it off.

**PLAIN MODE (`configure()`'s seventh argument, `network`) DELETES THE HALF THIS
CLASS IS NAMED AFTER**, and it is the mode most projects should be in
(docs/neural-upscaler.md, "Plain mode"). false = no bag proxies, no tile
accumulators, no reprojection, no feature grid, no MLP, and a composite that is
ONE textured quad instead of the 476-vertex Gouraud grid; the raster redirect,
the shrunken z buffer and the whole VRAM saving are untouched. Three things to
know before changing anything here. The gate the pipeline asks is
**`wantsProxies()`, never `isEnabled()`** — StaPipCore computes a world bounding
sphere per bag before it calls, so "the entry points are inert" is not the same
as free, and `proxy` is 2.34 ms of a 4.60 ms bill. `configure()` **forces
`jitterOn` false** there, in one place, because the only thing that can fuse two
jitter phases is the temporal pass and plain mode has none. And the base pass is
emitted as one CELL of the grid rather than as a GS **sprite** — a sprite would
save seven qwords and would hand the picture to a different rasteriser path,
which is a question about hardware nobody can answer from here; a one-cell
TRIANGLE_STRIP makes the identity a property of the packet. Checked: a neural
build whose net asks for nothing and a plain build are byte-identical over
811 426 compared pixels, nine cross-pairings.

**It is one half of a TWIN.** `src/blss.cpp` in the editor is the other, and
`docs/blss-reconstruction.md` is the contract: the same sampling (12.4 UV
quantisation, bilinear taps at UV − half a texel with 4-bit weights combined
`>>8`), the same blend chain in 8-bit with the GS's `(A−B)*C>>7` truncation and
0..255 clamps, the same `accumulate`/`buildReproj`/`buildFeatures`/forward pass.
That is not tidiness: the network's training labels are fitted against this
formula by an oracle running on the host, so a divergence trains the network for
a machine that does not exist. `tyrax-editor --blss-eval` is the regression test
(a parity break shows as the trained row falling well below the oracle row).
Change one side, change the doc and the other side.

**IT IS A PER-SCENE SETTING NOW, and `configure()` is still init-only.** The two
halves are `configure(..., nativeScenes)` and `setScene(upscale, network)`, and
the split is the whole design (docs/neural-upscaler.md, "Per scene"):

- **`configure()` re-lays the permanent VRAM region and evicts every texture**,
  because the z buffer's size follows the raster. That is safe at the top of
  `init()` and nowhere later - and the theory that a scene change would absorb it
  is FALSE, checked before anything was built: `loadScene()` frees and
  re-acquires textures one at a time, ref-counted, and never calls `vram.reset()`
  or `evictAll()`.
- **So a mixed game does not reconfigure. It PINS the layout.**
  `nativeScenes` true makes `configure()` call
  `RendererCoreGS::setZRasterScale(1, 1)` before the realloc decision, so the z
  buffer covers the full display for the whole run and `needsBufferRealloc()`
  stays false however often the active raster scale changes afterwards.
  `setScene()` then flips `enabled`/`useNet`, republishes
  `settings->setRasterScale`, re-derives the projection (`core3D->setFov`), drops
  `hasPrev` and restores the z mask. No allocation, no eviction, no packet.
  Measured in PCSX2 over ~1 200 scene switches: **zero evictions**, resident
  count and free VRAM constant, scene-load rate within 0.5 ms of both controls.
- **`endScene()` restores `gs->getZMaskDefault()`, never a literal 1.** Same
  number in every uniformly upscaled game (which is why nothing moved for them),
  0 in a mixed one - and the literal left the next NATIVE scene rendering its
  whole depth pass into a mask. The mask is still DERIVED by
  `allocateVramBuffers` and never assigned from outside; this is asking the
  allocator for its own answer, not overriding it.
- **`init()` re-places the low-res target on `allocated`, not on `enabled`.** In
  a mixed game a display-mode switch can land while a native scene is active, and
  gating on `enabled` would leave `lowVram` pointing into memory `vram.reset()`
  just returned to the texture heap.
- **`jitterWanted` is kept apart from `jitterOn`.** Plain mode forces the jitter
  off; with the mode a per-scene answer, the REQUEST has to survive a plain scene
  or the first one would silently disable the jitter for every neural scene after
  it - and those nets were fitted with it on.
- **The three frame-loop entry points need no `if`.** `beginScene`, `endScene`
  and `composite` all return immediately when `enabled` is false, so the
  generated loop emits the same three calls whether or not the scene is upscaled.

Eight things here that were paid for, and that any edit must keep:

- **The raster redirect is EXPLICIT STATE on `RendererCoreGS`, not a private
  trick.** `RasterTarget` (frame address, `FBW`, scissor rect, `XYOFFSET` in
  1/16 px) is published by `redirectRasterTo()` / `endRasterRedirect()`, read by
  `getRasterTarget()`, and put back by the **shared `emitRasterRestore()`** that
  the env map, the camera feed and the shadow map all call — so those brackets
  **nest** inside BLSS' instead of cancelling it. They used to restore
  `gs->getCurrentFrameBuffer()` + `settings->getWidth()/getRenderHeightF()`, i.e.
  the display buffer unconditionally, from inside the generated `renderScene()`;
  the first one silently turned the upscaler off for the rest of the frame.
  **Do not "simplify" this back into making `getCurrentFrameBuffer()` return the
  redirect target** — that was the prescribed fix and it is wrong: the restores
  touch four registers and only `FRAME` comes from that accessor, so it would
  leave a 512-wide `SCISSOR` over a 256-wide `FRAME` and a window-centred
  `XYOFFSET` for the wrong window with the jitter dropped, and it would break the
  accessor's four post-fx callers plus BLSS' own `endScene()`/`composite()`.
  `getCurrentFrameBuffer()` means **the display buffer**.
  Three traps inside the bracket itself: `XYOFFSET` is written BEFORE the clear
  sprite; the 3D path wants a **window-centred** offset (`2048 - lowW/2`) while
  the composite's 2D-style passes want the **screen-origin** one (`2048, 2048`);
  and `emitRasterRestore()` writes `ZBUF` explicitly and **LAST** rather than
  leaving it to ps2sdk's `draw_enable_tests` — every bracket points `ZBUF` at its
  OWN depth buffer, and returning from the shadow map with `ZBUF` still on the
  64×64 silhouette z made every projected-shadow receiver patch fail `GEQUAL`
  (drawn, then discarded). The jitter is added to the offset as raw 1/16 units —
  `XYOFFSET` is 12.4, so ±4 is exactly ±¼ pixel and the host reproduces it
  bit-for-bit. The shared restore also fixed a latent `InterlacedField` bug none
  of the three brackets had: the per-field `XYOFFSET` bias
  (`RendererCoreGS::getFieldYOffset16`) was never re-applied.
  `RendererCorePostFx::portalMaskBegin/End` was the same bug a **fourth** time
  and is **converted now** (`332f3193`): both read `getRasterTarget()` and
  restore through `emitRasterRestore()`, so this bracket finally carries the
  per-field `XYOFFSET` bias too. `Begin` re-narrows the scissor to the portal's
  bbox *after* the restore, because the destination view is the one thing here
  that must stay bounded; `End` lets it go. **All four brackets now share one
  restore — do not add a fifth that does not.**
- **The z buffer follows the RASTER, not the display buffer.**
  `allocateVramBuffers()` sizes it from
  `RendererSettings::getRasterWidthUI/HeightUI` — 57 344 words instead of
  229 376 at 2×2, so 672 KB back at 512×448 and 768 KB at 512×512 against
  224/256 KB for the low-res colour target. **BLSS leaves more texture VRAM than
  not using it** (measured, `Pal576i` 512×512, `VRAMSTAT` at frame 240: 0.227 MB
  free with BLSS off, 0.727 MB with it on and one eviction fewer). `FRAME` and
  `ZBUF` bases are independent registers, so the low-res pass writes a contiguous
  prefix of z at the low-res stride.
  **The invariant that makes it safe is `zBuffer.mask == 0` only INSIDE the
  low-res bracket** (`allocateVramBuffers` DERIVES the flag from the allocation
  it just made — 1 whenever z came out smaller than the display raster — and
  `beginScene`/`endScene` open and close it): every `draw_enable_tests` /
  `draw_setup_environment` in the engine reads
  that one field, so the 2D/HUD/post-fx half of the frame — full-screen sprites
  at `z = 0xFFFFFFFF` — cannot stamp past the smaller allocation.
  **Deriving it there is the fix for this feature's worst bug and must not be
  moved back to a caller**: `configure()` used to assign the flag one statement
  before the rebuild it triggers, the rebuild runs `allocateVramBuffers`, and
  that cleared it again — so the mask was 0 for the whole run and every
  full-resolution pass stamped depth 512×448 words past `ZBP` (`ZBUF` carries no
  width; the stride comes from `FRAME.FBW`), straight through the texture heap
  that starts just above the small allocation. Symptom: **every 4-bit palettised
  texture in the scene drew NOTHING** — the depth landed on the 8×2 CLUT, a
  zeroed CLUT has alpha 0, and `ATEST NOTEQUAL`/`AREF 0` discards it; 24-bit
  textures (no CLUT, alpha from `TEXA`) kept drawing, which made it look like a
  CLUT-descriptor bug for two sessions. General lesson for any buffer this
  engine shrinks: **an allocation is not an addressable extent.** Sizing z needs
  the scale, which only `blss.configure()` knows, and z is allocated third in
  `gs.init()`: the ordering is resolved by re-laying the permanent region from
  `configure()` through `RendererCore::rebuildPermanentBuffers()` (gated on
  `needsBufferRealloc()`), which is `setDisplayOutput`'s mode-change branch minus
  the mode. It deliberately does **not** call `gs.reinit()` — `programDisplay()`'s
  `graph_set_mode` would reset the GS mid-init for nothing.
- **The history is `frameBuffers[1 - context]`** — the previously presented frame,
  full resolution, free. That needed a new accessor on `RendererCoreGS` (the
  stock one returns the buffer being drawn INTO).
- **How a bag is DESCRIBED to the network, which was wrong for eleven commits and
  is the most expensive mistake in this feature's history.** `StaPipCore` used to
  hand BLSS the bag's bounding SPHERE, once per bag. For a floor or a terrain mesh
  that is grotesque — `wNear = w − radius` collapses to the near clamp and the
  screen box covers the frame — so **every tile read `depth = 1`, `depthGrad = 1`,
  `coverage = 1`**, i.e. the network was handed a constant and a generated game's
  entire frame was described by TWO proxies. Four rules replaced it, and every one
  of them is TWINNED with `blsscorpus.cpp` (`bagOf()`, `kProxyVerts = 24`):
  `addBagBox()` projects an object-space AABB through the MVP and near-clips it
  along its **twelve edges** (clip space is affine in the box's parametric
  coordinates, so eight corners cost one matrix-vector product plus three scaled
  columns); `StaPipCore` submits **one box per VU1 package** from the
  `StaPipBagPackagesBBox` it already caches for frustum classification, capped at
  `kMaxProxiesPerBag = 32` with consecutive parts merged above that (merging can
  only enlarge a box, so the worst case degrades toward the old whole-bag proxy);
  a box that **straddles the eye AND still fills the frame after clipping is
  dropped**, threshold-free, because its bbox is the frame by construction and its
  `wNear` is the clip constant; and a bag may opt out entirely via
  **`PipelineInfoBag::blssProxy`**, which codegen clears for the sky dome, the star
  field and the sun/moon discs — the straddle rule cannot catch a dome CAP, and
  only the submitter knows a mesh is a shell. `addBagSphere()` survives as the
  fallback for a bag with no package bbox. Measured on a booted `fpp` fixture,
  debug view 2, before → after: **2 → 41 proxies, 196/196 → 159/196 tiles covered,
  `depth` 1/1/1 → 0/.737/1, and 5.00 → 1.96 mean passes.**
  **A SIXTH RULE landed 2026-08-09 and it also SHIPS OFF: emitter bags.** A
  billboard bag runs `frustumCulling = None` on purpose, so it had no package
  bbox, fell to the radius-0 sphere and was rejected — i.e. **nothing described
  the particles**, which on `upscaler-lab` is 98.7 % of the fill and on
  `showcase` 95.6 %. `TYRA_BLSS_EMITTER_PROXY` (default 0, host twin
  `--emitter-proxy`) gives such a bag ONE box: the AABB over the centres it is
  about to submit, grown per axis by `|R.axis|*(max|m00|+max|m10|) +
  |U.axis|*(max|m01|+max|m11|)` off the params channel — exact for an ordinary
  emitter, √2-conservative for fog's swirl, and one box per BAG rather than per
  VU1 package because a pool's order is its SPAWN order and only a set's AABB is
  order-independent enough for the corpus to match. Measured with it on (PCSX2,
  parked `upscaler-lab`): 198 → 207 proxies, 147 → **224 of 224** covered tiles,
  `texDetail` 0.466 → 0.211 (it finally reports `puff.png`) — **and `coverage`
  becomes a CONSTANT 1.000/1.000/1.000** with `depthGrad` spread 0.101, because
  one AABB over a haze bank hands every tile the bank's whole depth range. That
  is the sky-dome failure in the channel the rule meant to rescue. Cost: BLSS EE
  **3.21 → 4.09 ms** (+0.88; `net` and `reproj` grow too — covering 224 tiles
  instead of 147 runs the MLP on all of them) and break-even **13.1 → ~15.3**
  coverages. So it stays at 0.
  **AND THE NAMED NEXT STEP - THE SPATIAL SPLIT - IS NOW A MEASURED NO
  (2026-08-09). Do not re-open it.** Binning the centres by COORDINATE is
  order-independent and perfectly twinnable, it was implemented on both twins,
  and it makes the two channels it was for MORE constant: at the same parked
  vantage, **224 of 224 covered tiles before and after**, `coverage` and
  `depthGrad` still 1.000/1.000/1.000, proxies 207 -> **241** of 310, tile
  updates 2 636 -> **6 077**, BLSS EE 4.07 -> **5.25 ms**, break-even ~15.3 ->
  **~18.2**. Two reasons, both general: **a partition of a solid region is a
  TILING of it, and a tiling has the same union** (so no spatial split can
  shrink `coverage`, and `depthGrad`'s max over bags reunites the range inside
  the tile), and a Tyra pool is never clustered - `updateParticles` spawns
  uniformly over the emitter's XZ rect. **The flat channel is the FIXTURE**:
  strip `upscaler-lab` to one small emitter and `coverage` reads 0.690 /
  67.8 % at 1.000 in all three arms, while `--blss-coverage` counts 71.65 of
  72.23 coverages as emitters on the shipped one - "covered everywhere" is
  simply true there. The rule and its tables are in
  docs/blss-reconstruction.md section 2; the emitter half's open item is that
  the CORPUS DRAWS NO PARTICLES - `docs/backlog.md`.
- **The instrument is PERMANENT, and deleting it is how this went unseen.**
  `logFeatureSpread()` under `blssDebugView = 2` logs one group a second into the
  game's `bin/log.txt`: `BLSSGRID` (tile/proxy counts), **`BLSSWORST`** (the widest
  single proxy — the line that named the sky dome in one shot), `BLSSFEAT` and
  `BLSSOUT` (min/mean/max per channel and per output) and `BLSSFILL` (occupancy
  through `emitGrid`'s own four-corner rule). Channel names and order are EXACTLY
  `blss::kFeatureNames`, because the host half is
  `tyrax-editor --blss-eval --probe "<BLSSFEAT line>"`, which places that vector
  inside the corpus distribution. A previous round added this measurement, read it
  once and removed it — after which the network was fitted to one distribution and
  run on another with nobody able to compare them. One gotcha left: it is inside
  `#ifndef NDEBUG`, so a `make release` build has no `TYRA_LOG` (the editor's own
  game build is not one). **Switching it on is a combo entry** — *Debug view* →
  "Log the feature spread to bin/log.txt (no tint)", in the BLSS window's
  *Project settings* tab and in *Project > Preferences*. Until `7d3dbf67` that
  combo offered only 0 and 1 against a field `project.cpp` clamps to `0..2`, so
  reaching view 2 meant editing `"blssDebugView": 2` into the `.tyra` by hand and
  a project that already had it displayed as "Off"; anything still saying that is
  stale.
- **The composite writes GS state the engine never wrote before**: `TEXA`
  (for the per-vertex-alpha trick: `TFX=MODULATE` + `TCC=0` + vertex RGB pinned
  to 128 makes RGB the untouched texel and A the vertex alpha) and **`COLCLAMP`**
  — the formula clamps and so does the host twin, and inheriting a `COLCLAMP` of
  0 would make the GS wrap instead and silently break parity on every saturated
  pixel. It also disables the **alpha test** for the duration: the weights ARE
  vertex alpha, so `ATEST_METHOD_NOTEQUAL` would discard zero-weight corners
  rather than blend them at zero (the post-fx path documents the same bug from
  the other direction).
- **It owns its own packet, sized for the worst case.** One tile row is a
  34-vertex `TRIANGLE_STRIP` at 3 qwords per vertex, so passes 2..5 are ~5 700
  qwords — the shared 768-qword post-fx packet would corrupt the GIF stream.
  Sparsity reduces what a frame draws, never what a frame MAY draw, so never size
  it off the typical case. And the strip's **vertex order is load-bearing**:
  `(i,j) (i,j+1) (i+1,j) (i+1,j+1) …`, because the weight field is piecewise
  linear over two triangles and the host models exactly that diagonal.
- **`emitGrid`'s skip rule is what the host's cost model is fitted to.** A cell is
  drawn unless ALL FOUR of its corner alpha bytes are zero, so one tile asking for
  a kernel lights up the nine cells around it. That rule is now inside the
  oracle's objective on the host (`--fill-weight`, charged as a step on the
  quantised byte), and `--blss-eval` reports occupancy through the same rule — so
  changing when a cell is skipped changes what the network was trained to want,
  even though the objective itself has no engine counterpart. Measured on the
  shipped net: mean **1.87 full-screen passes on held-out shots and 1.67 in
  distribution**, against 1.00 for plain bilinear and 5.00 for the worst case,
  while the oracle reaches a *better* PSNR at 1.36 — so about half a pass of the
  remaining fill is the network failing to generalise the cost model. At the
  shipped inference deadzone the point and sharpen passes are culled COMPLETELY
  (0 %) and only temporal is drawn over most of the screen. Occupancy is noisy
  (sd 0.30 over 39 cross-validation fold-runs, one fold at 2.12), and these are
  **fill counts, not timings** - and the conversion is measured now, not
  guessed: on a real PS2 one full-screen textured blended pass is **0.5896 ms
  at 512x512 and 0.5174 ms at 512x448** - the calibration gate is per RASTER,
  which it did not used to say, and 0.587 was a 512x512 (PAL 576i) figure read
  against 512x448 coverages for a year; both are now measured back to back on
  one console and `perMpx` agrees to 0.3 % (docs/profiling.md, "The calibration
  gate") - and PCSX2 reports **0.0077 ms** for
  the same sweep - it under-reports GS fill by **76x**, so NO PCSX2 GS number
  about this feature is admissible. The first real A/B on hardware: BLSS cost
  **+9.83 ms per frame and saved nothing**, because the frame was EE-bound
  (`drain` 0.02 ms, the `endScene` overhang 0.03 ms). **That number is
  PROVISIONAL**: it was taken on a build carrying the z-mask defect above, so
  the BLSS-on arm drew a frame with every palettised surface missing — the EE
  half stands, the GS half understates whatever fill BLSS saves, and the A/B
  needs retaking on hardware.
- **THE EE HALF IS PRICED, AND THE BIGGEST TERM IS THE MLP.** Splitting the two
  large terms onto their own counters (`tBlssProxy` in StaPipCore,
  `tBlssReproj`/`tBlssFeat`/`tBlssNet`/`tBlssPacket` in composite - the previous
  round got "~3.9 ms of scene submission" **by subtraction**, which is not a
  measurement) gives, in PCSX2 on the `blssrig` fixture: **net 3.96**, proxy
  3.26, pkt 0.61, reproj 0.39, feat 0.14. `runNet` being the largest is the
  finding - **newlib's tanhf/expf compute in DOUBLE and the EE has no
  double-precision FPU**, so 15 activations a tile are 15 software round trips.
  Four **bit-identical** cuts landed (2026-08-08), each measured on its own
  counter: the proxy near-clip takes each in-front corner ONCE instead of twelve
  edges x two endpoints (-1.10 ms, and a box wholly in front never enters the
  edge loop at all); the deadzone compare moved IN FRONT of the logistic, since
  logistic is monotone and a snapped-to-zero output does not need the expf that
  produced the number being discarded (-0.57 ms, guard-banded so the original
  compare still decides within 0.01 of the threshold); `passHasAlpha()` skips a
  composite pass with no non-zero corner, which at the shipped deadzone is point
  AND sharpen every frame (-0.30 ms); and `addBag` hoists the per-COLUMN overlap
  out of the row loop (-0.20 ms). **BLSS on/off went +8.76 -> +6.39 ms of EE in
  PCSX2; re-measured on the CONSOLE the same four cuts are worth 1.96 ms**
  (proxy 3.95->2.39, net 2.20->1.97, pkt 0.73->0.56), taking the whole EE bill
  from 7.92 to 5.95 ms. **PCSX2 does not transfer per-function**: it puts `net`
  above `proxy`, the console puts `proxy` above `net`, because the emulator
  over-weights libm. Attribute on hardware.
  Bit-identity was CHECKED, not asserted: under `blssDebugView` 2 the BLSSGRID /
  BLSSFEAT / BLSSOUT / BLSSFILL lines are byte-identical across 44 s of paired
  frames before and after. The **activation table is worth another 2.11 ms**
  (net 3.39 -> 1.29) and is the one big saving blocked on a decision: it must
  flip on BOTH twins in one commit (`TYRA_BLSS_ACT_TABLE` 512 + `src/blss.cpp`'s
  `--act-table` default), and until 2026-08-08 the engine's actTanh/actLogistic
  were never CALLED, so that switch used to control nothing. What is left is a
  **~5.95 ms EE floor on hardware** (proxy 2.39 + net 1.97 + 1.03 + 0.55);
  cutting the proxy term further means describing the frame more coarsely, which
  is a TWIN-CONTRACT change and needs `src/blsscorpus.cpp`'s `bagOf` to cut the
  same way - the rule is now WRITTEN and SWITCHED OFF: the **proxy budget**
  (`TYRA_BLSS_PROXY_BUDGET`, default 0) caps a bag's proxies at the number of
  grid TILES its whole box covers, since the grid resolves nothing finer than a
  tile; it takes 198 proxies to 116 on `upscaler-lab` and moves exactly one
  feature channel (`coverage` 0.631 -> 0.638), and it may not go to 1 until
  `bagOf()`/`bagList()` cut the same way. Two things the split counter
  `tBlssAccum` (`proxy=total/accum` in FTSPLIT) settled about that feed, both
  contradicting what the code said about itself: it is 262 PROJECTIONS for 198
  accepted proxies touching only **7.6 tiles each**, so the cost is per-PROXY and
  not per-tile - and `addBag`'s four `floorf` calls were **real out-of-line
  newlib calls, 68 instructions each** (the tanhf/expf finding one function
  along; replaced by a bit-identical cast + compare). Interleaving the six tile
  accumulators to save cache lines was tried and is SLOWER: a float is 4 bytes
  and a line is 64, so all sixteen tiles of a grid row already sit in one line of
  each array. Two more measured negatives worth not repeating: moving the
  composite's
  EE work in front of `endScene`'s drain can save at most `tBlssEnd`, which is
  **0.05 ms in PCSX2 and 0.11 ms on hardware**; and `beginScene`'s
  `draw_wait_finish()` is NOT removable - PATH1 preempts an in-flight PATH3
  transfer, so the clear sprite must complete before VU1 kicks, or the scene is
  drawn and then cleared over.
- **AND THE FEATURE HAS A REGIME AFTER ALL - IT IS HEAVY OVERDRAW.** On
  `examples/upscaler-lab` **as it stood at `0c3f05c3`** - 12 haze banks x 256
  large alpha-blended billboards = 3 072, plus the 2 fire, 2 smoke and rain that
  were always there and were never counted, for **3 448 billboards over 17
  emitters** whole-scene - a real PS2 measured **530 ms with BLSS off against
  157 ms with it on** - a **3.37x** speedup, on a scene running at **1.9 FPS**
  that nobody could watch. **Re-tuned against the console** (6 banks x 32 instead
  of 12 x 256, i.e. **11 emitters / 568 billboards** today - that is the figure
  to quote for the shipped demo, never the 3 072) it measures
  **52.95 ms off against 32.42 ms on** - d = +20.53 ms, 95 % CI [+20.46, +20.61],
  n = 1024 paired frames per pairing, **1.63x**, for **4.60 ms of EE** plus
  0.50 ms of composite fill. **Those milliseconds describe the fixture's PRE-CC0
  geometry**: its art assets were swapped for CC0 ones on 2026-08-09 (the cottage
  and spider had unverified redistribution terms), which left the fill alone
  (--blss-coverage 72.63 -> 72.23, emitters untouched) but took ~4 ms of EE out of
  both arms, almost all of it the animated model. The fill model fitted from these
  runs is current; the absolute A/B is owed a re-run. That second table is the one to quote, and it is the
  configuration the fixture SHIPS (`blssJitter` off). The owed jitter-off re-run
  has **landed** (2026-08-09): the earlier 52.86 / 32.98 / **1.60x** was the
  jitter-ON timing, the off arm is unchanged and the BLSS arm came out 0.56 ms
  faster, and `BLSSFILL` reads **`passes = 1.56`** in both - i.e. the prediction
  ("the jitter moves where the half-res raster samples, not how much of it there
  is") held. Keep both tables, labelled. Break-even is **13.1 full-screen
  coverages at 512x448** (`0.7548 x 0.5174 x C > 4.60 + 0.50`) and **11.5 at
  512x512** - same formula, different pass price - so quote the break-even WITH
  its raster or not at all. `breakEven()` and `--blss-coverage` PRINT the right
  one now (2026-08-09): `kPassMs` became `kPassMsPerMpx` (2.2524) plus
  `passMs(rasterPx)`, `speedFrom` takes the raster, and `CoverageReport` echoes
  back the `outW`/`outH` it counted at - 13.1 at 512x448, 11.4 at 512x512. The
  single scalar was right for a 576i project and 14 % optimistic for an ordinary
  PAL one. It changes nothing about the coverage over-read, whose two
  instruments shared one fixture at one resolution. BLSS keeps about a quarter of the fill,
  because `blssScale 0` is `Scale::X2Y2` - half in *each* axis, a quarter of the
  pixels, NOT half the fill, which is what the ~22 figure had assumed. The
  retention term is now FITTED on hardware over five load points (0.7548 saved,
  RMS 0.093) rather than assumed at 0.741, and the fit's intercept 5.10 ms
  reproduces the independently-counted 4.60 + 0.50 to two decimals. The
  activation table
  (`TYRA_BLSS_ACT_TABLE`, and the host's `actTable` in `src/blss.cpp` - **one
  number in two files, moved in the same commit or not at all**) took `net` from
  1.93 to **0.79 ms**; PCSX2 had predicted 2.11, because it over-weights libm
  and does not transfer per-function. The previous verdict
  ("it saves nothing, the frames are EE-bound") came from ONE low-fill fixture
  plus a **false discriminator**: `drain ~ 0` does NOT mean EE-bound, because GS
  backpressure stalls the EE inside the submission (charged to `submit`) and
  `drain` only measures the tail after the last packet. Both arms of the 3.4x
  win read `drain = 0.02 ms`. **Never infer boundedness from `drain`; change the
  GS load and see whether the frame shortens.** Two fixture traps found doing
  this: the project key for the Live Debugger is **`liveDebug`**, not
  `liveDebugger`, and leaving it on costs ~500 ms/frame of host: network I/O
  INSIDE the measurement; and the rig's own window mean summed ticks in a
  **u32**, which overflows above ~290 ms/frame and printed a 500 ms frame as
  37 ms (now u64 - the median was always right, which is how it was caught).
  `upscaler-lab` itself is a fine GS-bound stress case and a BAD demo: 1.9 FPS
  with BLSS off, because it was tuned against PCSX2's 76x-under-reported fill.
  The instrument is `inc/debug/frame_profile.hpp` (TYRA_FRAME_PROFILE, default
  0, so a shipped libtyra.a carries none of it) plus the FRAMETIME line in the
  generated drawDebugHud.
- **THE COMPOSITE'S LAST TWO TERMS WERE DUG INTO AND THEY ARE A FLOOR - STOP
  LOOKING FOR A FOURTH LIBM WIN** (2026-08-09, hardware, docs/profiling.md
  "The last terms in the composite"). `reproj` 0.275 + `feat` 0.190 was the only
  part of the bill nobody had opened. The method that found the previous three
  wins was applied first, and it came back empty: **`sqrtf` in `buildFeatures`
  compiles to a bare `sqrt.s`** - no call, no errno branch, `/ kTile` folded to
  a multiply - and `buildFeatures` is 346 instructions with **zero `jal` and
  zero `div.s`**. `tanhf`/`expf`/`floorf` were wins because they were CALLS; the
  arithmetic that is left is single instructions and there is no round trip to
  find. Two bit-identical changes landed anyway and are worth **+0.017 ms of a
  4.60 ms bill (0.4 %)**: `finishTileStats` writes `feat[][1]/[3]/[4]/[5]` and
  `tGrad` directly (five per-tile arrays and one 224-tile pass deleted - but the
  work MOVES rather than disappearing, `feat` 0.190 -> 0.141 against `reproj`
  0.275 -> 0.310), and `buildReproj` hoists the per-column/row screen ray and
  makes the neighbour-count average an exact binary scaling. That second one
  **prices a divide: ~565 fewer `div.s` a frame is 0.007 ms**, i.e. ~2 100 EE
  cycles - the conversion factor to use before optimising arithmetic here again.
  Bit-identity proved on hardware across all three arms (A1 = B1 = B2 = C1 = C2
  byte-identical on BLSSWORST/BLSSFEAT/BLSSOUT/BLSSFILL in the emitters-off
  segment). **And a fixture trap worth knowing: `proxy`/`accum` differ by
  0.051 ms between two runs of the IDENTICAL ELF** on `upscaler-lab`, because
  the parked scene sometimes settles with 201 proxies instead of 198 -
  `BLSSGRID`'s proxy count is the guard, read it before trusting any `proxy`
  number and discard a run whose count does not match. `reproj`/`feat`
  reproduce to +-0.001.
- **THE SHIPPED DEFAULT NET WORKS ON A SCENE IT HAS NEVER SEEN, AND THE MLP IS
  NOT WHY** (2026-08-09, real hardware, docs/profiling.md "The shipped default
  net"). A stock `--new` fpp project plus six haze banks, **no `blss.net` of its
  own**, so `blssBake` falls back to `resources/blss-default.net`: **30.65 ms
  off against 15.62 ms on, d = +15.03 ms, CI [+14.96, +15.10], n = 1024 paired
  frames, four cross-pairings spanning 0.018 ms - a 1.96x speedup, 25 FPS to a
  locked 50.** The boot log names the net (`BLSS: network = the editor's
  built-in default network (fitted on ...)`), which is what that line is for.
  **But `BLSSFILL` reads `passes = 1.00` and every `BLSSOUT` channel is 0.000**:
  the composite is ONE bilinear pass and the network chooses nothing, so the
  win is the quarter-area raster alone. `BLSSFEAT` shows `texDetail` identically
  **zero** - a stock project's terrain is vertex-coloured, not textured, and
  `texDetail` comes from a bag's `texelArea` - which is the same channel the
  one-net-lottery result named. This is NOT a defect: re-evaluated at that exact
  vantage the scene's headroom is **+0.000 dB** (`bilinear = oracle = 38.435`,
  native 45.543), i.e. bilinear IS optimal there and zero passes is the correct
  and cheapest answer. Training a project net changed frame time by **+0.03 ms
  (1.00x)** - the leave-one-project-out tie holds on hardware. Two traps this
  cost, and both are now closed: `--blss-train <projectDir>` wrote `blss.net`
  into the **cwd** and not the project, so the rebuild silently kept using the
  default (FIXED 2026-08-09 - a single project positional defaults the path to
  `<projectDir>/blss.net` on the read side as well as the write; the BLSS
  window never saw it because it runs with cwd = the project AND passes `-o`);
  and the corpus
  RENDERER draws no emitters, so a PSNR number for a billboard-heavy scene
  describes a frame the game never displays (both in docs/backlog.md).
- **The bob is the JITTER, and the per-field bias is NOT part of it.** The
  +-1/4-pixel per-frame raster jitter in `beginScene` is the confirmed cause: a
  person watched three builds of `examples/upscaler-lab` differing in nothing
  else and called them steady (BLSS off) / **"like an earthquake"** (jitter on)
  / steady (jitter off). `blssJitter` therefore defaults to **false** now. This
  bullet previously claimed the bob was net-dependent and that neither shipped
  fixture reproduced it; both are **retracted** — that reading came from an
  instrument pointed at `blssbug` (untextured, so a quarter-pixel resample can
  change nothing) and at `upscaler-lab` with its particles running (whose motion
  is larger than the artefact). With the emitters frozen, 16.3 % of the picture
  below the HUD alternates between two byte-identical phases. It is **not** a
  displacement — cross-correlation lag `(0,0)` — but a resample alternation on
  every textured edge, which is what a shake looks like from a chair. The rules
  that make it measurable are in docs/profiling.md, "The stability gate".
  **`examples/upscaler-lab` ships jitter OFF since 2026-08-09** and its net is
  refitted for that sampler; it used to ship `true` as "the jitter-on reference",
  i.e. the flagship demo shook until you edited it. Two confounds will make the
  gate lie to you, and both cost a burst: the debug HUD prints a live **frame
  counter** (turn `showFps`/`showMemory`/`showProfiler` off, do not try to crop
  around it), and **PAL interlaced mode alternates fields, which is itself a
  period-2 signal in a window capture** - run the gate at `displayMode`
  **progressive**. With both left in, a still scene reads 0.10-0.15/255 of noise
  against a 0.77/255 artefact.
  Separately, `getFieldYOffset16()` is non-zero for
  `DisplayMode::InterlacedField` **only**, so in the usual `Interlaced` mode it
  contributes nothing; `beginScene` used to add it anyway, unscaled, inside a
  raster whose row is `scaleY` physical rows, while `composite()` added the real
  one on top — 1.5 physical rows of field bias at 2x2 instead of 0.5. It is
  **removed** from `beginScene` now: the low-res target is an offscreen texture,
  so the interleave belongs only to the pass that writes the buffer the CRT
  scans. `configure()`'s new trailing
  `jitter` parameter (project field `blssJitter`, default true, no UI yet)
  pins the offset to 0 and the picture becomes indistinguishable from the
  BLSS-off control. The host twin in `src/blss.cpp` does NOT model the switch,
  so a net trained today and run with the jitter off is slightly out of
  distribution. Test for the PERIOD-2 SIGNATURE, never for "did it change": a
  sampler with an even frame stride lands on one jitter phase every time and
  reports a perfectly still picture.


Incompatible with **depth of field, portals, split view and FRAME
EXTRAPOLATION**. The first three read or write real GS depth at display
resolution, which since the z shrink is not merely unwritten but unallocated.
**The fourth is a different mechanism and cost a user a broken build to find**
(2026-08-11, docs/frame-extrapolation.md "Why not with the upscaler"): both
features rebuild a frame by reprojecting the previous one through the camera
delta, and extrapolation presents twice per loop, so the world runs at half the
field rate and the camera moves **twice as far between two RENDERED frames** —
which is exactly the interval BLSS' temporal pass reprojects across. Turning it
on does not add a second approximation beside the first, it doubles the input to
the first one as well. **It is invisible parked and only appears in MOTION**, so
every frozen-camera gate on this branch (the stability gate, the byte-identity
harnesses — all of which freeze the camera AND the emitters on purpose) was
built not to see it. Measured on the reporter's project, PCSX2 software
renderer, `--pad`-driven: upscaler alone clean, extrapolation alone clean, the
pair tearing the frame into cells that disagree; BLSS' own per-corner
reprojection offset peaks at 158 px of a 448 px raster with extrapolation off
and 201 px with it on. **Two theories were disproved by measurement and must not
be re-opened**: it is NOT the two-buffer history degeneration `composite()`
guards (with three buffers the rotation was LOGGED frame by frame — the history
is always the previous RENDERED frame, intact), and it is NOT raster state
leaking across the warp (that is static register state and would wreck a parked
frame too). Dropping the temporal pass reduced but did not remove it, which is
why this is an interlock and not a degradation. **The build REFUSES the
combination now**
(`332f3193`): `blssClashes()` + `blssInterlock()` in `src/templates.cpp` put
`#error` lines into the generated `inc/scene_data.hpp` naming the feature and the
scene, and `generate()` prints the same on the host. Until then the editor's
warning was the whole interlock, and this paragraph's and both docs' claim that
"codegen does not emit them together" was simply false. Each condition mirrors
what the generated game does — DoF per scene *and* the `Set Depth Of Field` flow
node, portals only when **linked**, split view only with a **second `Player`
object**, frame extrapolation whenever the project preference is on and a scene
resolves the upscaler on — so it refuses nothing that would have worked; the
preferences dialog asks the same five questions live, and *Scene > Scene
Preferences* asks the extrapolation one too, because a per-scene override is the
one way to create that pair without opening Preferences at all. Env maps, camera feeds and projected shadows
are **no longer on that list** — they nest now. The HUD, 2D and every post effect
still draw at full resolution, after the composite, which is the one property an
upscaler must not spoil.

**That refusal is per SCENE since the setting became one**: `blssClashes()`
asks each scene that RESOLVES the upscaler on, so a portal in one scene
refuses that scene and leaves the others upscaled.

**Out-of-distribution numbers: use `tyrax-editor --blss-eval --cv`**
(leave-one-shot-out cross-validation), never a plain `--blss-eval`'s held-out
columns. A single split is a sample of size one, this feature quoted one five
times, and the ±0.4 dB it blamed on the training seed was **which shot got held
out** (per-seed fold-mean sd: 0.04 dB against 0.35 fold to fold). The current
answer on the built-in corpus is **+0.42 dB over plain bilinear**, 39 fold-runs,
3 of them below bilinear, 1.80 mean passes, **at jitter ON** — which is the
bestiary's own sampler and not what any example project ships.

**That net is still not the one to ship into a game, and the reason is now
measured over seven projects rather than one**: a bestiary-trained net is a
lottery, −0.34 dB on average and **−1.09 dB at worst**, because `texDetail` —
the channel its temporal gate leans on hardest — is identically zero on five of
those seven. What DOES ship as one net is a corpus that is **the bestiary AND
real projects together**: leave-one-**project**-out it scores +0.29 dB on a
project it has never seen against that project's own net's +0.31 (fold sds 0.37
and 0.34). `--blss-train <a> <b> bestiary --all-shots` builds it;
`--blss-eval <a> <b> bestiary --cv --cv-groups` is what measured it.
`--blss-train <projectDir>` still fits one project and still reaches the highest
number of all in distribution (+0.41 dB), and the editor's BLSS window defaults
its corpus switch to exactly that. Full account: docs/neural-upscaler.md,
"Can one net ship for every project?".

Both verbs take **`--threads N`** (0 = every core, clamped to 32) for the corpus
render and the oracle. It moves the wall clock and nothing else: the same seed
writes a byte-identical `blss.net` at any thread count **and matches the binary
from before the corpus was parallelised**, which is what keeps the fold tables
above measurements of the current code. `--threads 1` against every core, then
`md5sum`, is the check.

## Before you hand-edit a `.vclpp`: run it on the host first

The editor carries a **VU1 simulator and a microprogram generator**
(`src/vuir|vuasm|vusim|vugen`, `docs/vu-framework.md`). It changes the loop for
any VU work: instead of a Docker build plus a PCSX2 boot to find out what a
program does, run it here.

```bash
tyrax-editor --vu-list <file.vclpp>   # expand the vclpp layer + disassemble
tyrax-editor --vu-check               # parse ALL of them, simulate, diff, budget
```

- **All 25 `.vclpp` files the engine ships parse and run**, including the clip
  family and the VU0 raytracer kernel. `--vu-list` shows what the program looks
  like AFTER macro expansion, which is the first thing to check when it does
  something you did not write.
- The simulator reports two things `vcl` and `dvp-as` will not: a **Q clobber**
  (a `div`/`rsqrt` whose result is overwritten before anything read it - the
  gotcha `CalculateTyraEnvStq` documents) and a **quadword address outside VU1
  data memory**, which the hardware silently wraps.
- What it does NOT model, deliberately: cycle timing, dual-issue pairing, branch
  delay slots (all `vcl`'s job, applied after this level) and the MAC/STATUS flag
  registers (`fsand`/`fmand` yield 0 and warn - a program that BRANCHES on them
  is not authoritatively simulated).
- **All fifteen StaPip programs** (five `as_is` + five `cull` + the five
  Sutherland-Hodgman `clip`) also have C++ descriptions that generate them; a
  generated program is proven **bit-identical** to the handwritten one by
  simulating both on randomized input, and each emits the same instruction COUNT
  as its handwritten file. If you change one of those fifteen by hand,
  `--vu-check` starts failing - update the description in `vugen.cpp` too, or the
  two have genuinely diverged and you should say which is right. (Only the
  `as_is` five are *adopted* so far: the files in `vendor/tyra` ARE the generated
  ones. The `cull` and `clip` families are still the handwritten originals.)
- **`--vu-replay <projectDir>` re-runs a REAL console capture on the host** and
  diffs it against what the hardware produced (`examples/vu-lab` is the fixture;
  36/36 GS vertices bit-identical). Two limits: only the LAST mesh of a chain can
  be replayed, so you need the Debugger's flush picker to get a single-mesh
  flush, and a project with no flow-graph node compiles the devkit layer away
  entirely - the capture button then does nothing.
- **The simulator rounds toward zero, because VU1 does.** An x86 rounds to
  nearest-even; that difference is invisible on screen X/Y and shows up as one or
  two units in the last place of the 24-bit Z (the coordinate scaled by
  8388607.5). It was found by replaying a console capture, not reasoned about. If
  you add arithmetic to `vusim`, do not compute outside `run()`'s rounding scope.
- **Nothing in `vendor/tyra` is generated yet.** `--vu-emit` writes to a
  directory you name on purpose: adopting generated microcode needs the full
  Docker + hardware pass, not a host check.

## The VU1 packet tap (`src/renderer/3d/pipeline/static/core/stapip_vu_tap.*`)

TyraX addition: the editor can ask for one VU1 DMA chain and decode it
(docs/devkit.md, PROGRESS 195). The engine side is deliberately tiny — a **null
function pointer** (`Tyra::g_vuPacketHook`) and the branch that tests it in
`StaPipQBufferRenderer::sendPacket()`, i.e. once per bag flush, never per vertex.
The capture itself lives in the generated game's devkit TU, so a release build
links none of it.

**The second hook** (`g_vuMemHook`) is called after the send, once VIF1 and the
microprogram are idle, with all of VU1 data memory — that is how the editor reads
what the program PRODUCED (the GIF packets it staged for XGKICK). It costs a
pipeline stall, so the devkit installs it for one frame and uninstalls itself from
inside the callback. Two things to know before using it for verification: VU1
memory holds only the **last** MVP uploaded, and **one flush carries several
meshes in one chain** — so pairing an input block with an output packet needs a
single-bag flush plus the object-data chain, which is not done yet (PROGRESS 196).

**The rule that bites**: the pipeline sends vertex arrays **by reference** — a
`ref`/`refs`/`refe` DMA tag whose `qwc` counts quadwords at *another* address,
with the tag itself being ONE quadword. So (1) any chain walker advances by 1 for
those tags and `1 + qwc` only for inline `cnt`/`next`, or it decodes data as tags;
and (2) anything that wants the geometry must dereference **on the EE**, while
those addresses are live — the hook copies each referenced block along.

## The EE crash handler (`src/debug/crash_handler.cpp`) — and its vector traps

TyraX addition (docs/devkit.md in the editor repo): turns a real CPU exception
into a report instead of a silent freeze. Built on ps2sdk's **libeedebug**
(`ee_dbg_install` + `ee_dbg_set_level1/2_handler`), which hands a C handler the
whole `EE_RegFrame` — so there is no hand-written exception stub to maintain.
Three properties to preserve if you touch it:

- **It lives in its own TU and is only linked when someone calls
  `CrashHandler::install()`.** `libtyra.a` is an archive, so a game that never
  installs it (any release build — the editor's devkit layer is what installs)
  carries zero bytes. Do not add a global constructor or a reference from other
  engine code, or that property dies quietly.
- **The handler does the minimum and gets OUT of exception context**: copy the
  frame, scan the stack for plausible return addresses, then set `frame->epc` to
  a trampoline and return. The report is written from the trampoline, where stdio
  and the IOP-served host: filesystem work again. File I/O inside the exception
  context is the classic way to turn a crash into a hang.
- **Install LEVEL 1 ONLY.** `ee_dbg_install(2)` never returns: it drops
  interrupts and rewrites the error-level vector at 0x80000100 under the running
  machine. That froze every debug build that turned the feature on, on hardware
  and in PCSX2 alike, with nothing in the log. `ee_dbg_install(1)` returns fine
  on the same boot. Level 2 is only NMI / cache error, so nothing is lost.
- **What the install hooks is not up to your handler table**, and a hooked cause
  with no handler is an infinite exception loop. From disassembling
  `libeedebug.a` (the image ships no sources): `ee_dbg_install(1)` routes causes
  **1..3** through `SetVTLBRefillHandler` and **4..7 + 10..13** through
  `SetVCommonHandler` regardless of what you register, and its vector **always
  ERETs** — it never chains to the kernel handler it saved (that copy is only for
  `ee_dbg_remove`). So an unhandled hooked cause returns to the faulting
  instruction with nothing serviced; fatal for a TLB refill, which is how a
  mapped access is *completed*. The handler therefore hands causes 1..3 straight
  back with `SetVTLBRefillHandler` after installing. Causes 0 (Interrupt), 8
  (Syscall), 9, 14 and 15 are never routed by the install at all — which is why
  narrowing the registered list alone never fixed anything.
  (`ee_dbg_set_level2_handler` also bounds-checks `cause < 4`.)
- **A crash takes the screen** (`init_scr`/`scr_printf`) before idling. An
  assertion may halt quietly and let the editor surface it; an exception may not,
  because a frozen last frame is indistinguishable from a hang.

**Verified on hardware** (2026-07-29): a forced signed-overflow `add` produced
`CRASH: Arithmetic overflow`, `bin/crash.txt` and a `--symbolize` hit on the
exact source line. **PCSX2 cannot *produce* an EE exception at all** — a forced
overflow, an illegal opcode, a write to address 0 (main RAM starts there) and a
misaligned load all pass through unharmed — so catching is a hardware-only test,
even though the old install hang reproduced in both.

Related: the engine's error blocks now print `==============  TYRAX  =============`
(`inc/debug/debug.hpp`, two places); the editor parses that and the old TYRA
banner both, so a previously built ELF still reports.

## Hard-won pitfalls (dead ends already explored — don't repeat them)

**Rendering**
- **A texture's wrap mode does nothing in 3D.** `Texture::setWrapSettings`
  reaches the GS only through `path3` (2D sprites) and the post-fx blits;
  NOTHING in the static or dynamic 3D pipeline ever emits `GS_REG_CLAMP`, so a
  3D mesh samples with whatever the last 2D draw or post-fx pass left in that
  register - which is global state you do not control. If a 3D mesh's texture
  coordinates can leave 0..1, clamp them where you BUILD them, on the EE, and
  do not reason about wrap modes at all. (Found via the projected shadows: the
  receiver patch's STs come out of a light projection and ran -0.38..1.39, so
  the silhouette was sampled a second time and left thin dark streaks at the
  patch edges. Writing `GS_SET_CLAMP` from the shadow pass changed nothing
  measurable; clamping the STs fixed it exactly.)
- **Never submit bags with `frustumCulling = None`.** Off-screen geometry wraps
  the GS 4096-px raster window → "objects render twice / giant smeared
  polygons". PCSX2's HW renderer often *masks* this; the SW renderer and real
  hardware show it. This was the root cause of a long-standing corruption bug —
  not the clipper patches (all were bisected; even pure upstream reproduced it).
- **Nothing backface-culls — never emit two exactly coplanar faces.** Neither
  the StaPip VU1 programs nor the GS reject back faces (the "cull" program
  family is about the frustum, not winding), so a double-sided surface whose
  front and back share one plane dither-fights itself across the whole surface
  (dashed dark/light bands, worse with distance). The Plane primitive hit this
  (its darker underside vs the lit top); the fix is a small offset between the
  two faces (see `addPlane` in templates.cpp / `unitPlane` in primmesh.cpp,
  0.01 local units). Same rule for any hand-built double-sided geometry.
- **Coplanar passes over ONE vertex array must be pinned to ONE package
  size.** `StaPipVU1Program::getMaxVertCount` derives the package size from
  the bag's PROGRAM CLASS — how many verts of (position [+ ST] [+ normal] +
  color) fit in half a VU1 double buffer. With the shipping buffer that is
  **108** for untextured + per-vertex colors, **72** for textured, **90** for
  textured + a single color, **144** for untextured + a single color. So an
  object whose base pass is untextured and whose companion pass is textured
  (a reflective sphere with no `map_Kd`, anything under the baked lightmap,
  an untextured terrain chunk under its layer passes) splits the SAME array at
  different boundaries. `StaPipCore::render` then classifies each package
  against the frustum, and with `fullClipChecks` a fully-inside package takes
  the perspective divide on VU1 while a straddling one is clipped on the EE
  and drawn `as_is` — two routes over one triangle, differing in the last bits
  of z and, at the frustum edge, in coverage. Coplanar passes then dither-fight
  exactly like the double-sided case above: grey wedges bleeding from under a
  reflective object, baked shadows fighting z-index with the ground. The fix is
  `StaPipBag::packageSize` (0 = derive), which the generated game sets to the
  **MINIMUM** over an object's passes (`pinPackageSize` in templates.cpp).
  The minimum is not a preference — pinning a class above its own derived size
  overflows its VU1 buffer. Two things this also settles: the frustum-bbox
  cache is keyed by `(maxVertCount, vertex pointer)`, so differing sizes made
  each pass recompute the boxes the previous one had just built — pinning
  measured **+4–6 %** frame rate (126 → 133 FPS, vsync-off PCSX2) rather than
  the slowdown smaller packages suggest; and `67e2893f`'s switch of the env
  pass to `PipelineZTest_Standard` was a mask for this defect, not its fix
  (keep it, it is still correct).
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
  your math. Three VCL-proper traps (paid for by the VU0 rt kernel):
  symbolic register names that collide with VU special registers OR
  instruction mnemonics (case-insensitively) are rejected — don't name a
  register `r`, `q`, `i`, `p`, `acc`, or anything like `mAx`/`sub` ("can't
  use r as a name" / "invalid name for register"); a broadcast field
  selector is only legal on the SECOND source operand — `max.x m, vf00,
  v[y]` works, `max.x m, v[y], vf00[x]` fails with a misleading "used
  before set" on the bracketed register; and `ERROR: no opt table ..
  something failed making table .. for <label>` means the REGISTER
  ALLOCATOR ran out — too many symbolic registers live across that loop
  (31 VF ceiling), not a syntax problem. Fix by shrinking the live set:
  reload fixed-address parameters at their use sites instead of pinning
  them in registers for the whole program (`lq` is cheap), and lane-pack
  related scalar fold state into one register's x/y/z fields (assemble
  candidates with `add.x/y/z reg, vf00, src[x]`, fold once as a vector).
- **The spot-light cone term's magnitude scales with distance²** — only its
  SIGN is the (exact, distance-independent) angular cutoff. Size `invSoft`
  in `buildSpotForBag` off a FRACTION of the range, never the full range:
  the original `softness / (objRange2 * (1 - cosCut2))` made the flashlight
  ramp up over its whole reach — black on anything close, full brightness
  only near the far end, i.e. "it doesn't light what I'm aiming at".
- **Clamp vertex colors BEFORE the VU1 clipper interpolates them.** The cull
  programs run `FixColor` (mini 255 / max 0) per vertex right after the spot
  light; the clip programs feed Sutherland-Hodgman and only clamp in the
  emitter — after the lerp. An unclamped saturated color (a bright dynamic
  light easily pushes past 255) then interpolates from its raw value, so a
  lit surface visibly BRIGHTENS the moment it touches a screen edge and
  starts being clipped. `stapip_clip_{c,tc}_vu1.vclpp` now clamp before
  storing into the scratch polygon (float clamp only — `ftoi0` belongs in
  the emitter). **Budget note:** the ceiling alone (`loi 255` + `mini.xyz`
  per vertex, 6 instructions) is all that fits — adding the matching
  `max.xyz … vf00[x]` floor too (9 per program) tripped the real
  `VU1 pipeline programs overflow into the draw-finish program` assert
  (path1.cpp:145) on the boot logo. The clip family has ~no micro-memory
  headroom; measure with
  `mips64r5900el-ps2-elf-size obj/.../clip/*.o` (bytes / 8 = instructions)
  after ANY edit there. **And the clip family now has a C++ description**
  (`buildClipBody` in `src/vugen.cpp`), so an edit to one of those five files
  has to be made in BOTH places or `--vu-check` fails — which is the point:
  the ceiling above is exactly the kind of change that used to be applied to
  `clip_c` and forgotten on `clip_tc`.
- **A GIF A+D giftag whose NLOOP undercounts its register writes stalls the
  GIF forever** — the stray qword parses as a new giftag with a garbage
  NLOOP. Symptom: the game hangs on the loading screen (spinning in
  `draw_wait_finish()` / a FINISH handshake that never arrives), no assert,
  clean log. Count the qwords after every PACK_GIFTAG edit.
- **ps2sdk's `ATEST_KEEP_*` constants name what is PRESERVED, not what is
  written** (`ps2sdk/ee/include/draw_tests.h`): `ATEST_KEEP_ZBUFFER` = 1 =
  GS FB_ONLY (colour written, z untouched), `ATEST_KEEP_FRAMEBUFFER` = 2 =
  ZB_ONLY (z written, colour untouched), `ATEST_KEEP_ALL` = 0 = write
  nothing. Reading them the other way round is easy and expensive: upstream's
  alpha test passed `ATEST_KEEP_FRAMEBUFFER` on the standard path, so every
  fully transparent texel of a cutout texture stamped the z buffer while
  drawing no colour — foliage, grates and decals silently occluded whatever
  was drawn behind them later, which looks like a sorting or texture bug
  rather than an AFAIL one. Cutout wants `ATEST_KEEP_ALL` (both pipelines
  now use it; `PipelineZTest_TestOnly` deliberately keeps FB_ONLY for its
  depth-tested-no-z-write trick). Symptom to recognise: transparency itself
  works, but geometry behind a transparent area is missing, cut along the
  straight edges of the quad in front of it.
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
  buffer line) and works. That crash is 1080i-specific: SDTV interlaced
  FRAME (the `InterlacedField` half-height mode below) is fine.
- **True field rendering (`DisplayMode::InterlacedField`)**: 480i/576i with
  half-height 512x224 frame/z buffers scanned at SMODE2.FFMD=FRAME — a
  fresh image per field, half the fill/VRAM of stock interlaced. The
  DISPLAY window must stay IDENTICAL to the stock FIELD mode (full 448
  frame-line window; ps2sdk's `graph_set_screen` mis-programs DY/DH for
  the interlaced+FRAME case — `programDisplay` writes the registers via
  `setDtvDisplay` instead), no flicker filter. The game-facing coordinate
  space stays 512x448: the projection is built at
  `RendererSettings::getRenderHeightF()` (raster scale only — frustum
  planes come from fov+aspect and are untouched), `RendererCore2D` halves
  sprite y, and clears / post-fx / env-map restores all use the render
  height. Anything that sizes or addresses the PHYSICAL framebuffer must
  use `getRenderHeightF()`, game-facing layout keeps `getHeight()` — miss
  that split and one mode or the other draws half-off-screen. Per-field
  half-line alignment lives in `flipBuffers` (CSR.FIELD read after vsync,
  inverted — the frame being rendered displays one field later — then an
  XYOFFSET +8 on odd fields appended to the flip packet).
- **Full-height PAL (`DisplayMode::Pal576i`)**: the "true PAL" 512-line
  frame - a 512x512 framebuffer scanned through the SAME stock interlaced
  FIELD path (`programDisplay`'s default case with the signal pinned to
  GRAPH_MODE_PAL, flicker filter kept); 512 lines is ps2sdk's own full PAL
  frame height, so `graph_set_screen` handles the window - no setDtvDisplay
  needed. Always 50 Hz regardless of VideoMode/region (getRefreshRate
  special-cases it, like the DTV modes' 60). Costs ~380 KB more GS VRAM
  (three 512-line buffers), leaving ~1 MB for textures.
- **Runtime display switching**: `RendererCore::setDisplayOutput(mode, ws)`
  (TyraX fork) switches the scan mode / widescreen between frames.
  A mode change resets the whole VRAM allocator (`vram.reset()`),
  rebuilds frame/z buffers + post fx, and `texture.evictAll()` drops every
  texture allocation (they lazily re-upload) — never call it mid-frame.
  It is also the ONLY caller allowed to `allocateBuffer()` after init.
  The projection aspect lives in `RendererSettings::updateGeometry`
  (fixed 4:3-baseline look; widescreen scales it anamorphically).
  **That table has a host twin**: `App::ps2ViewportOutput` (app.cpp) resolves
  the same per-mode framebuffer size + aspect for the editor's PS2 output
  viewport, and the flicker-filter choice in `presentFrameBuffer` with it
  (docs/ps2-viewport.md). Adding or resizing a `DisplayMode` means editing
  both, or the editor draws a picture the console does not.
- **GS VRAM is two regions, and `free()` is order-independent** (TyraX fork —
  full write-up in [docs/gs-vram.md](../../../docs/gs-vram.md)).
  `allocateBuffer()` (page-aligned) fills a **permanent** bump region at the
  bottom — both frame buffers, z, post-fx scratch, noise, the env-map and
  camera-feed targets — that is never released; `allocate()` (block-aligned)
  serves textures from a **coalescing best-fit free list** above it. `free()`
  ignores addresses the heap never handed out, which is what protects the
  permanent region — do not "fix" that into an assert. Upstream's `free()` was
  `pointer = address` (a stack pop), so freeing anything but the newest
  allocation handed out the memory of still-live textures: streaming-layer
  unloads reproduced it as surviving objects rendering another object's
  texture. Budget after the init buffers is **~1.08 MB** (~1 MB in
  `Pal576i`), and every allocation costs ~8 KB of padding on top of its
  pixels — a 256×256 32bpp texture is 24% of the heap, a 512×512 is 93%.
  When a texture does not fit, `RendererCoreTexture::makeRoomFor()` evicts
  coldest-first (`pickVictim`: stale entries by LRU; when the whole resident
  set is in this frame's working set, the MOST recently bound one — plain LRU
  makes a per-frame texture scan cycle its entire set). Textures that must
  never be evicted are not "pinned" — they are not in the list at all: give
  them their own `texbuffer_t` through `Texture::vramResident`, the way
  `RendererCoreEnvMap` does. To see what a scene actually does, build with the
  **debug** profile and grep the game's `bin/log.txt` for `VRAMSTAT`
  (binds/hits/uploads/**re-uploads**/evictions/resident/free MB/largest free
  block, per frame); `reup` per frame is the number that matters, each one is
  a full PATH3 transfer. `examples/showcase` sits at 6 allocations and
  0.87 MB free and never evicts anything — if you are chasing a VRAM problem
  in a palettized project, measure before assuming there is one.
- **"Restoring" a GS register nothing else writes is a GUESS, not a restore.**
  `TEXA` and `COLCLAMP` are written by exactly one place in this engine (the
  BLSS composite) - so a second pass that sets them has no previous value to put
  back and can only assert what it believes the GS reset value to be. The frame
  warp did that and got it wrong: every blend AFTER the pass inherited it, and
  `examples/showcase` (bloom + film grain) came back with the whole picture
  hue-shifted - orange sky olive, green grass orange, a cyan crate magenta -
  while the geometry stayed perfect. It passed every earlier test because those
  fixtures had **no post fx**, and with nothing blending a broken blend state is
  invisible. The cure is not a better value: a pass that does not blend must not
  write those registers at all. **Whenever a full-screen pass looks right on a
  bare fixture and wrong in a real scene, diff the two on POST FX before
  suspecting anything else** - and check what global state the pass leaves
  behind, not just what it draws.
- **Frame extrapolation: the IDENTITY warp is the test** (`renderer/core/warp/`,
  `docs/frame-extrapolation.md`). `RendererCoreWarp` re-draws the last finished
  frame under a newer camera as a textured grid, so a game can render its world
  at half the field rate. When `from == to` the result must reproduce its source
  EXACTLY, which is what makes the whole GS side falsifiable in one screenshot:
  a wrong TEX0 binding, region clamp, UV encoding or strip vertex order all show
  as tearing or garbage instead of a clean picture. Check that before checking
  anything else. Two traps paid for here: **an overcounting NLOOP is as fatal as
  an undercounting one** (the restore block claimed 4 registers and wrote 3, so
  the GS read the next giftag as a register write and the game hung in
  `draw_wait_finish` with no assert and a clean log); and **a degenerate camera
  basis silently becomes an identity copy**, because every corner takes the
  `wPrev < 1e-3` fallback - so "the warp does nothing" and "the caller passed a
  bad basis" look identical. Note also what the tooling could NOT do: real and
  warped frames alternate every field, and a Wayland compositor screencast is
  not frame-accurate enough to isolate one of two images at 50 Hz - ten captures
  of a deliberately marked warp frame came back byte-identical. If you need to
  see a synthesised frame, build a game-side A/B rather than trusting a
  screenshot.
- **Triple buffering: "does it fit" is the WRONG question, and asking it that
  way is a boot crash** (TyraX fork, `docs/frame-pacing.md`). The third display
  buffer is a full one - 229 376 words at 512x448x32, 262 144 in `Pal576i` - and
  at 512x512 three buffers plus z are **exactly** the whole 4 MB: the allocation
  succeeds and the NEXT one fails, which is a live `Out of VRAM for post fx
  buffers` assertion before the first frame (measured, that is how the guard was
  written). What must survive the third buffer is everything `RendererCore::init`
  still allocates AFTER `gs.init` - post fx ~12 288 words, the env-map target +
  its z 32 768, the camera feed + its z 32 768, the projected-shadow slots a game
  may claim later ~20 480 - plus a texture heap worth having, so
  `allocateVramBuffers` checks for headroom (`kThirdBufferReserveWords +
  kThirdBufferMinTextureWords`) and REFUSES rather than warns. The same
  arithmetic is why the feature is in practice an `InterlacedField` one: the
  full-height 32bpp modes have no room at all. **Any future permanent
  allocation added to renderer init has to be added to that reserve**, or it
  becomes the one that gets -1. **The upscaler's low-res target was exactly
  that miss**: `blss.allocate()` runs LATER than this check (configure -> the
  VRAM rebuild -> allocate), so it is in neither `getHeapWords()` nor the
  reserve, and the guard was handing out the third buffer against 114 688 words
  the scene was about to claim - a 128 KB texture heap instead of 576 KB at
  512x448 1x2. It is subtracted explicitly now, from the live raster scale, and
  so is its host twin `project::tripleBufferingFit`. **The rule generalises:
  the reserve is a list of what is allocated after this line, and anything
  allocated after it in a REBUILD counts too.**
- **Frame extrapolation x the neural upscaler: the history tap needs TWO
  guards, not one** (`docs/frame-extrapolation.md`). BLSS has no history buffer
  - the other display buffer IS its temporal history - so a synthesised frame
  landing there feeds an accumulator its own warped output. Guard one is
  `getPreviousRealFrameBuffer()`: `flipBuffers` records `lastRealBuffer` and a
  `synthetic` flip does not claim it, so both the warp and BLSS ask for the last
  frame the SCENE drew. Guard two is the one that is easy to miss, because it is
  the first guard working correctly and producing a useless answer: with **two**
  display buffers, two flips per loop return `context` to where it started, so
  every rendered frame is composited into the buffer that holds the previous
  rendered frame - the history IS the render target. `composite()` compares the
  addresses and drops the temporal pass with a one-shot warning; the other four
  passes read the low-res target and are unaffected. With three buffers the
  indices are distinct by construction and neither guard fires. **Any future
  feature that adds a present without a render inherits both.**
  **BOTH GUARDS ARE CORRECT AND BOTH ARE NOW UNREACHABLE FROM A BUILD** - the
  pair is refused (see the BLSS section above), because the real interaction was
  never the buffer bookkeeping. Verified by LOGGING the three-buffer rotation
  frame by frame rather than reasoning about it: `hist` is always the previous
  RENDERED frame, intact, and `histIsTarget` never fires. What actually breaks
  is that extrapolation halves the world rate, so the camera delta BLSS'
  temporal pass reprojects across DOUBLES - and it only shows in MOTION. Keep
  the guards for the next feature that presents without rendering; do not spend
  another session looking for the bug here.
- **The vblank handler owns `DISPFB`, and the queue is lock-free ON PURPOSE.**
  `RendererCoreGS::onVblank` runs in interrupt context and everything it touches
  must stay interrupt-safe - `presentFrameBuffer` is GS privileged-register
  stores and nothing else, so do not grow it into anything that allocates, DMAs
  or logs. The three-slot queue needs no `DIntr()` because the handler only acts
  when `pendingBuffer >= 0`: while the main thread has waited for -1 the handler
  is inert and `displayedBuffer` cannot move under it, which is why
  `flipBuffers` writes `context` FIRST and `pendingBuffer` LAST. The other
  ordering rule is the `draw_finish` handshake before queueing - the GIF is
  in-order, so FINISH is what proves the frame is fully rasterised; queueing
  first puts a half-drawn frame on screen. And any path that moves buffer
  addresses (`reinit`, `reallocateBuffers`) must `resetDisplayQueue()` first, or
  a queued frame reaches DISPFB naming the old layout.
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

- **`Color`'s default constructor does not initialise anything** ("Initialize
  Color without setting default values" - it is a vector type used in hot
  paths), so any `Color` MEMBER is garbage until something assigns it. That is
  fine for a value that is always written before it is read, and it was not:
  `RendererCore::bgColor` is the clear colour, and `Engine::init` calls
  `banner.show()` immediately after `renderer.init()` - the logo hold clears the
  framebuffer with it, every frame, for two seconds, long before any game code
  can call `setClearScreenColor`. The boot logo therefore came up on whatever
  was in that memory: black on one build, BLUE on the next, with nothing in the
  game changed to explain it (reported from the console exactly that way, and
  any change to the binary or heap layout can move it). Initialised in the
  constructor now. The general rule this leaves: anything read before the first
  game frame - a clear colour, a mode, a flag - must be initialised where it is
  DECLARED or in its owner's constructor, because "the game sets it at startup"
  is not true of the engine's own boot screens.

**Audio**
- audsrv streams PCM only; ADPCM is for one-shots (`adpcm.tryPlay`), and an
  ADPCM voice cannot be STOPPED - only started, or started over.
  **The channel budget of a generated game**, per bus (every new sound goes to
  the CURRENT room's bus, so this is what is available at once): 0-15 for Play
  Sound, 16-23 for the sound emitters. The auto cycle runs 16, not 24 — it used
  to run the full 24 and walked into the emitters' slots, so one auto play in
  three stole an emitter's channel or bounced off it as "busy". Who gets one
  when they are all busy is **docs/sound.md** (priority, then loudness); the
  engine's part of it is `forcePlay` and `endedMask` below.
- **`AudioAdpcm::forcePlay` plays over a busy channel** - `AUDSRV_ADPCM_FORCE`,
  a flag bit ORed into the channel number, which the fork masks off and uses to
  skip its own ENDX busy check. The refusal was always a software check in
  `audsrv_ch_play_adpcm`, not a hardware limit: KON on a sounding voice
  restarts it. The bit rides in the channel because that number already travels
  EE -> RPC -> IOP untouched, so this cost no new export and no signature
  change (adding an IRX export means touching the import list of everything
  that links it). **Whether a forced restart CLICKS is a hardware question and
  is not settled** - it depends where in the waveform the victim was, and PCSX2
  is not a witness worth trusting on the SPU2. If it turns out to click, the
  fixes in order are: drop the victim's volume and play on the next frame, or
  KOFF first and let the ADSR release run (both cost a frame of latency).
- **`AudioAdpcm::endedMask(core)` is how to ask what is still playing** - the
  SPU2's own ENDX register, one bit per voice, one IOP RPC. Ask it per PLAY
  REQUEST, never per frame (a play already costs two or three RPCs; a per-frame
  poll is what the emitter loop's whole quantization exists to avoid).
- **An EE buffer handed to a SIF DMA must be written back to main memory
  first** (`SifWriteBackDCache(ptr, size)`), and `audsrv_load_adpcm` did not do
  it. The EE's data cache is write-back, the DMA reads RAM, so a sample just
  read with `fread()` reaches the IOP as *whatever was in that memory before* —
  for whichever loads happen to still be cached, which in practice is never the
  first one. **PCSX2 emulates no EE cache, so every load looks perfect there**;
  on a console the second sound of a project was silent (the SPU2 dutifully
  played the garbage, which is usually zeros) while the first worked. Fixed
  2026-08-06 in the vendored fork. Two things to take from it: the same rule
  applies to ANY EE→IOP transfer you add, and the tell is cheap — audsrv
  reports the sample's header back, so a **nonsense `pitch` for a file whose
  earlier load reported a sane one** identifies a corrupt upload in one log
  line (1881 vs 0x41C00000 was the actual pair).
- **`AudioAdpcm` logs why a sound did not play** (debug builds): once per
  channel per reason - no sample, channel busy, audsrv error - plus a one-line
  SPU2 dump (per-voice volume/ADSR/start address, the core's VMIX masks, MMIX,
  master and reverb volume) at each channel's first successful play. That dump
  is the instrument for "it plays but I hear nothing": diff a channel that
  works against one that does not.
- **`audsrv_ch_play_adpcm` reports "I do not know this sample" as a POSITIVE
  `AUDSRV_ERR_ARGS` (5)**, which no sign test can tell from a channel number.
  `tryPlay` now demands that an explicit channel comes back as itself; before
  that, playing a freed/never-loaded sample looked like success and simply made
  no sound.
- WAV files: 8-bit PCM is unsigned (0x80 = silence) but audsrv mixes signed —
  convert (XOR 0x80) or it wraps at every zero crossing (loud crackle at
  correct pitch — that exact symptom happened).
- Mono/low-rate streams need smaller chunk size + fill threshold or audsrv's
  ring buffer starves.
- **The SPU2's hardware reverb is reachable, and only through a second RPC
  server** (`AudioReverb`, `audio/audio_reverb.*`, docs/reverb.md). audsrv
  exposes playback and nothing else, so the registers come from PS2SDK's
  **`ps2snd.irx` + `libps2snd`** - an EE-side RPC client over the `libsd` the
  engine already embeds. Both are stock PS2SDK (AFL 2.0, unlike audsrv itself,
  which is LGPL v2 per every file header), so this cost one `.irx-em`, one
  loader call and `-lps2snd` in `Makefile.base`. audsrv keeps talking to libsd
  directly on the IOP; the two are ordinary co-clients of one driver.
- **audsrv is a SOURCE fork, not a blob** (`vendor/tyra/audsrv/`): the IOP and
  EE sources are in-tree at ps2sdk `e78a9cb2`, and `build.sh`/`build.ps1`
  rebuild the three artifacts in `bin/` that `src/runner.cpp` overlays into the
  build container. Change the sources and you must re-run that script and commit
  `bin/` in the same commit - nothing in the game build compiles audsrv.
  `./build.sh --check` diffs a fresh build against the committed artifacts;
  `audsrv.irx` is byte-identical while `libaudsrv.a` never is (ar stamps its
  members, gcc's LTO section names carry a random per-compilation id), so the
  member SIZES are what that check compares.
  The ps2sdk that script fetches (a build TREE - these Makefiles include
  `$(PS2SDKSRC)/Defs.make`) has a **mirror** fallback like every entry in
  `deps.sh`: `doctorspider42/tyrax-vendor-ps2sdk`. Losing upstream costs the
  ability to REBUILD the module, not the module (its sources are in-tree) and
  not game builds (those use the SDK installed in the `h4570/tyra` image).
- **`sceSdInit()` clears libsd's transfer callbacks - the ones audsrv's
  streaming ring installs.** So the reverb's RPC bind runs BEFORE
  `audsrv_init()` and the effect-enable bit AFTER it (audsrv's own
  `sceSdInit(COLD)` resets the core attributes). Get that order wrong and the
  MUSIC goes silent with no error anywhere while the sfx keep working - which
  points the investigation at completely the wrong subsystem.
- **libsd's defaults send EVERYTHING to the effect bus**, music included:
  `VMIXEL`/`VMIXER` come up with all 24 voices set, and `MMIX` bits 4/5 route
  the core input (the streamed song) into the reverb. So a per-voice send
  normally REMOVES a voice, and keeping the music dry is an explicit write.
  (Those bit meanings were confirmed against libsd's own block-transfer
  handler, which clears bits 6/7 - the dry pair - when a stream ends.)
- **`sceSdSetEffectAttr` only zeroes the work area if effects were ALREADY
  enabled** (`effects_disabled && clearram` in libsd's effect.c). A first
  preset set with the core's effect bit still off leaves whatever was in SPU2
  RAM circulating as noise. Enable, then set.
- **Reverb RPCs cost what audsrv's do**: synchronous SIF calls sharing the bus
  with the music stream, so the generated game quantizes the wet depth to 64
  steps and sends only real changes - the discipline `updateSoundEmitters`
  already follows for volume/pan. A per-frame RPC is measurable in frame rate.
- Reverb presets occupy 8-96 KB of SPU2 RAM at the TOP of the 2 MB while audsrv
  loads ADPCM samples from 0x5010 upward, so they collide only past ~1.9 MB of
  effects. Changing preset zeroes that area, which is why the game only does it
  at zero wet level and never per frame.
- **The audsrv fork plays voices on BOTH cores**: channels 0-23 are core 1
  (unchanged, so old callers cannot tell), 24-47 are core 0. Upstream muted
  core 0's master outright, which is what made the SECOND reverb unit
  unreachable - a reverb is per core and only that core's voices feed it.
  Unmuting is the whole routing change: core 1's `AVOL` (the core-0-into-core-1
  volume) was already pinned at 0x7fff, and `cdrom.c` had always raised core 0's
  master for CDDA. `AudioReverb` exposes the two units as `BusA` (core 1) /
  `BusB` (core 0), and the generated game cross-fades rooms across them - a
  room owns a bus, the incoming one takes the free unit while it is silent, and
  the depths ramp past each other. **The consequence to keep in mind when
  touching anything that PLAYS a sound: a voice is committed to a bus when it
  starts**, so every play site must offset its channel by
  `ScriptContext::reverbBusBase` (0 or 24) or the sound lands in the room the
  listener has left.

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
test. Known next target (from `docs/backlog.md`): retire the EE clipper —
flip `"clipping"` to vu1 by default (M4 in docs/vu1-clipping-plan.md, gated
on a real-PS2 pass).
Measure with PCSX2's FPS display on the software renderer, 3+ samples, before
and after; pixel-compare screenshots to prove output is unchanged.
