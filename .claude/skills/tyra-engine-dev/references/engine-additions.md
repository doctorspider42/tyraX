# TyraX additions to the engine: catalogue (non-renderer-pipeline)

Read when: you touch input (pad, USB kbd/mouse, replay), 2D sprites/aspect, asserts/soft errors, collision, object ids, boot-time state, or need to know whether something is a fork addition. Renderer features (post-fx, shadows, env map, portals, split view, VU0 tracer, BLSS) are in renderer-features.md and blss.md; loaders in loaders-files.md.

Layout: `vendor/tyra/engine/{inc,src}/` mirror each other: `audio`, `debug`, `file`, `info`, `irx`, `loaders`, `math`, `pad`, `physics`, `renderer` (2D, 3D static/dynamic pipelines, core GS/VU1), `thread`, `time`; plus `engine.hpp`/`game.hpp`; VU sources (`*.i`, `*.vclpp`) under renderer.

## 2D
- **`Sprite::additive`**: additive `Cs*As + Cd` (lens-flare ghosts, glows); `Renderer2D` pins alpha-over per sprite otherwise.
- **`Sprite::drawSize`**: per-axis destination size (0 = stock `size * scale`). Exists because the framebuffer SHAPE differs per mode (512x448 interlaced, 448x448 480p, 448x540 1080i, 512x512 Pal576i); `scale` is one float and in `MODE_REPEAT` `size` is the SOURCE rect. Used by the generated menu compositor (docs/menu-styles.md "Resolutions").
- **`RendererSettings::getWindowAspect()`**: physical display-window shape (4:3; 16:9 widescreen; pillarboxed 1792/1920 in widescreen 1080i). Widescreen is ANAMORPHIC — anything keeping authored proportions divides horizontal scale by this over 4:3 (menu compositor, cutscene letterbox).
- 2D origin per mode: display-framebuffers.md.

## Scene dynamic lights
`RendererCore::dynLights[8]` + `clearDynLights`/`addDynPointLight`/`pickDynLight`. Colour VU1 programs have ONE spot slot per mesh; `StaPipCore::render` picks per bag on its world bounding sphere: a placed light reaching it properly (att^2 >= 0.1 at the nearest point, a spot only with the bag in its cone) wins over the flashlight (`sceneLightsKeepMeshes`, default true since 2.0.0 - the torch reaches aimed-at props per pixel through the game's receiver pass; a bag that pass cannot draw sets `PipelineInfoBag::torchPerPixel = false` and keeps strongest-wins); otherwise the strongest contributor → `StaPipQBufferRenderer::setBagLight`. A point light uses the SAME spot constants (zero direction, `cosCut2 = -1`, saturated `invSoft`) — no VU1 change. Levers (don't conflate):
- `PipelineInfoBag::dynLightPick = false` — no scene light (generated games: terrain chunks — neighbours picking different lights truncate a pool at the chunk border; the sky dome — camera-centred).
- `PipelineInfoBag::dynLightSkipSlot` — a `dynLights` index the pick must skip (spot receiver pass draws that lamp projected).
- `spotLit = false` — no torch. On a bag that still picks, the pick runs without the torch (`pickDynLight(..., withSpot = false)`) and the scene's lights stay; before 2.0.0 it handed every such bag the off light, so a wall in the beam lost the lamp beside it.
- Spot bounds / cone sizing: static-pipeline.md.

## Input
**USB keyboard/mouse** (`pad/kbd_mouse.*`): `Engine::kbdMouse` polls `ps2kbd` (raw, 256-bit HID bitmap) and `ps2mouse` (DIFF: per-frame deltas + buttons), loaded by IrxLoader behind `EngineOptions::loadUsbKbdMouse`.
- `Pad::injectVirtual` overlays a virtual pad (held buttons OR in, click edges from the previous overlay, sticks clamped) per **slot** (`Pad::VIRT_SLOTS`, one `virtPrev` each): 0 = kbd/mouse fold, 1 = editor Remote Pad (docs/remote-pad.md). A second source must never reuse slot 0 — each reads the other as releasing everything, every held button re-clicks every frame.
- **`Pad::setState` / `KbdMouse::setState`** (input recorder, docs/input-replay.md) REPLACE polled state (a recording must win over a resting hand). `Pad::setState` re-seeds **every** `virtPrev` slot, else the first frame after replay fabricates click edges. `KbdMouse::forced` (ORed into `isEnabled()`), else replayed keys drop on machines without a USB keyboard.
- **Skipped under stock ps2link**: ps2kbd/ps2mouse import usbd symbols; drivers added to a running ps2link's un-reset IOP never come up (`PS2MouseInit` spins forever → boot freeze on the Tyra logo). `loadUsbKbdMouseUnderPs2Link` (default on) targets the **TyraX ps2link** (`tools/ps2link`, bakes usbd+ps2kbd+ps2mouse into ps2link's boot; the only ps2link the editor deploys, docs/ps2link-setup.md): engine loads NO USB modules, reuses the resident stack. `KbdMouse::init(underPs2Link)` guards `PS2MouseInit` on the keyboard device having opened (stock ps2link → logs instead of hanging).
- Real-hardware traps: `PS2MouseData` must be zero-initialised (no packet on a still frame → garbage constant delta spins the camera; PCSX2 never shows it); read mode set to DIFF explicitly; IrxLoader gives HID a fixed settle delay (USB enumeration async on hardware, instant in PCSX2). **Hardware-unconfirmed** beyond "drivers ready" (test devices didn't speak HID boot protocol).

**Two players** (docs/multiplayer.md): `Pad::initOptional(port, slot)` — `padInit` once-global; optional pad never blocks/asserts on a missing controller (upstream `update()` busy-waited forever on DISCONN), keeps polling for hot-join, sticks centred while disconnected. Split screen: renderer-features.md.

**Rumble**: `Pad::setActuators` (act-direct DualShock: on/off small motor + 0-255 heavy motor) behind the Vibrate Pad node / `padVibrate()`.

## Asserts and errors
- **Quiet-halt assert** (`debug/debug.hpp`, `TyraDebug::trap`): failed `TYRA_ASSERT`/`TYRA_TRAP` prints the dump to console / host `log.txt` with stable delimiters, then `for(;;) SleepThread()` — last frame stays, editor's error dialog surfaces it. Upstream's `init_scr()` + infinite `scr_printf` only behind `Tyra::Info::drawAssertScreen` (default **false**, `info.{hpp,cpp}`; nothing wires it on).
- Delimiters now `==============  TYRAX  =============` (`inc/debug/debug.hpp`, two places); the editor parses that and the old `======= TYRA =======` … `================` so old ELFs still report.
- **`TYRA_SOFT_ERROR`**: same delimited block, header `Non-fatal error (game keeps running)!`, **no halt**. Used by `png_loader.cpp` (8x8 magenta `makePlaceholderTexture`) and `audio_song.cpp` (missing WAV: `load()` returns early, `play()` no-ops). New asset loaders: soft-error + safe fallback, never `TYRA_ASSERT` on a missing file (loaders-files.md).
- A guard in `debug.cpp` stops `TYRA_LOG` opening `cdrom0:LOG.TXT` for write (that wedged every ISO boot).
- EE exceptions: devkit-debugging.md (crash handler).

## Object ids — unique, not random
`Sprite`/`Mesh`/`MeshFrame`/`MeshMaterial`/`MeshMaterialFrame` share ONE lookup namespace in `TextureRepository` (`addLink(id)`; `getBySpriteId`/`getByMeshMaterialId` return the FIRST texture whose links contain the id). Upstream `rand() % 1000000` (never seeded) collided → wrong texture on a sprite → garbled/black HUD sprites, worst right after opening a menu. Use `renderer/models/unique_id.hpp` `generateUniqueId()` for any id-bearing render object. (`audio_song.cpp` deliberately keeps `rand()` — separate namespace, off-thread.) Id lookup cache: textures-vram.md.

## Physics
`physics/CollisionMesh` (3D-BVH triangle collider): `raycast(..., outNormal)` returns the hit triangle's mesh-local normal (rigid-body per-corner contacts, docs/physics.md). Both queries test the mesh's own box FIRST (the grid clamps outside queries onto edge cells, else every border triangle is paid) — keep the early-out exact: sphere reach = radius + the 0.6 crossing band. Plus `Ray::intersectTriangle`.

`physics/BroadPhase` (`broad_phase.{hpp,cpp}`, docs/collision-boxes.md "The broad phase"): which objects may touch an area or a segment, two backends behind one interface - a height-aware XZ grid (rebuilt per commit) and a loose octree (split past 8 objects, per-node content bounds, kept across commits). Caller hands boxes `begin`/`set(i, box, moving)`/`commit`; `queryBox`/`querySegment` return a SUPERSET, ascending. Host-compilable (only `<vector>`): fuzz it against brute force from a scratch harness before changing it - keep it never-fewer and ascending (the walker's pushes depend on order).

## Boot-time state
**`Color`'s default constructor initialises nothing** (hot-path vector type). `RendererCore::bgColor` is the clear colour and `Engine::init` runs `banner.show()` right after `renderer.init()` — the ~2 s logo hold cleared with garbage, long before game code can call `setClearScreenColor` (black one build, BLUE the next). Initialised in the constructor now. Rule: anything read before the first game frame (clear colour, mode, flag) is initialised at declaration or in its owner's constructor.

## Skinning reuse (1.74.0)
`SkelInstance::PartLod::skinSource` points to the first corner with bit-identical position, normal, joints and weights (UV differences may share; hard normals cannot). Skin the first, copy its output with VU0 loads/stores, without helpers that might clobber the running VU0 AABB. All render vertices remain (4-byte index per corner); the hash table exists only during load. `TYRA_SKEL_PROFILE` (skel_instance.hpp) = 1 → per-instance COP0 pose/skin timings every 100 skins; ship 0.

## Signed RGB SH (1.74.0)
`PipelineDirLightsBag::signedSH` (default false). Both packet writers set ambient.w = 0 (classic) or -1 (SH); output alpha unchanged. The macro loads ambient.xyzw, uses w as the dot-product floor, clamps the RGB sum before clipping. SH = identity directions + signed RGB axis coefficients. Keep `vugen.cpp`, expanded shared clip C and generated as-is D/TD in sync; run `--vu-check` (incl. its RGB numeric oracle), then rebuild. No extra packet qwords. DMA-lifetime bug it caused: submission-dma.md.

## Optional boot logo (2.0)
`EngineOptions::showBanner` (default true): `Engine::init` calls `banner.show()` only when set. Generated `main.cpp` writes it from `ProjectSettings::showEngineLogo` (Loading Screens > First power-on, format 115). Off = the game's first picture is its own splash or loading screen; nothing else about init moves.


### Qualified collision runtime (2.1.2)

CollisionMesh uses an exact 3D triangle BVH with `.tmdl.tcol` host baking and
runtime fallback. BroadPhase indexes moving and sleeping boxes; WorldBroadPhase
retains unchanged statics and refreshes compact movers. Body pairs use swept
bounds, convex SAT/linear CCD share projections, and OBB projections round
outwards. separating_axis_cache.hpp stores hints only: every rejection proves
current separation again. Geometry/shape revisions invalidate transformed data.
The scalar six/three-pass solver remains; no VU0, warm-start or island backend.
Contacts and car-prop candidate lists grow instead of silently truncating.
