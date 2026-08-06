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
docs/menu-styles.md "Resolutions"), the **scene dynamic lights** registry
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
