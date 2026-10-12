# troubleshoot/tyra-engine-dev-display.md

Scan modes (DTV, true-field, full-height PAL), runtime display switching, the
frame-rate counters, the vblank/display queue, split view and frame pacing.
Symptom first.

## DTV display modes scan garbage past the buffer's right edge

**Cause:** ps2sdk's `graph_set_screen` always programs the mode's full VCK width
into DISPLAY.DW, and no 64-aligned framebuffer width divides the 1440/1920-VCK DTV
rasters.
**Fix:** `RendererCoreGS::setDtvDisplay` programs DISPLAY1/2 directly. Also: the
gsKit/OPL 1080i recipe (interlaced FRAME mode + MagV--) **hard-crashes PCSX2
v2.3.205** (the process dies seconds after SetGsCrt, no crash dialog); 1080i in
FIELD mode with MAGV=2x is visually equivalent and works. That crash is
1080i-specific: SDTV interlaced FRAME (the `InterlacedField` half-height mode) is
fine.

## True field rendering (`DisplayMode::InterlacedField`)

**Cause:** 480i/576i with half-height 512x224 frame/z buffers scanned at
SMODE2.FFMD=FRAME gives a fresh image per field, half the fill/VRAM of stock
interlaced.
**Fix:** the DISPLAY window must stay IDENTICAL to the stock FIELD mode (full 448
frame-line window; ps2sdk's `graph_set_screen` mis-programs DY/DH for the
interlaced+FRAME case — `programDisplay` writes the registers via `setDtvDisplay`
instead), no flicker filter. The game-facing coordinate space stays 512x448: the
projection is built at `RendererSettings::getRenderHeightF()` (raster scale only —
frustum planes come from fov+aspect and are untouched), `RendererCore2D` halves
sprite y, and clears / post-fx / env-map restores all use the render height.
**Anything that sizes or addresses the PHYSICAL framebuffer must use
`getRenderHeightF()`, game-facing layout keeps `getHeight()`** — miss that split and
one mode or the other draws half-off-screen. Per-field half-line alignment lives in
`flipBuffers` (CSR.FIELD read after vsync, inverted — the frame being rendered
displays one field later — then an XYOFFSET +8 on odd fields appended to the flip
packet).

## Full-height PAL (`DisplayMode::Pal576i`)

**Cause:** the "true PAL" 512-line frame needs the SAME stock interlaced FIELD path
with the signal pinned to GRAPH_MODE_PAL and the flicker filter kept.
**Fix:** `programDisplay`'s default case handles it; 512 lines is ps2sdk's own full
PAL frame height, so `graph_set_screen` handles the window — no setDtvDisplay
needed. Always 50 Hz regardless of VideoMode/region (`getRefreshRate` special-cases
it, like the DTV modes' 60). Costs ~380 KB more GS VRAM (three 512-line buffers),
leaving ~1 MB for textures.

## Runtime display switching

**Cause:** a mode change must reset the whole VRAM allocator.
**Fix:** `RendererCore::setDisplayOutput(mode, ws)` (TyraX fork) switches the scan
mode / widescreen between frames. A mode change resets the whole VRAM allocator
(`vram.reset()`), rebuilds frame/z buffers + post fx, and `texture.evictAll()` drops
every texture allocation (they lazily re-upload) — **never call it mid-frame**. It is
also the ONLY caller allowed to `allocateBuffer()` after init. The projection aspect
lives in `RendererSettings::updateGeometry` (fixed 4:3-baseline look; widescreen
scales it anamorphically). **That table has a host twin**: `App::ps2ViewportOutput`
(app.cpp) resolves the same per-mode framebuffer size + aspect for the editor's PS2
output viewport, and the flicker-filter choice in `presentFrameBuffer` with it
(docs/ps2-viewport.md). Adding or resizing a `DisplayMode` means editing both.

## `Info::getFps()` read half the frame rate on progressive modes

**Cause:** it divided a hardcoded `15625.0` into a single frame's delta of **EE
Timer 3**, which the kernel clocks from **H-BLNK** — it counts SCANLINES, so its
rate is a property of the VIDEO MODE while the constant is PAL 576i's line rate.
PAL 576i reads **15 626 Hz** (exact); progressive 480p reads **31 470 Hz** and it
printed **29.76 against a true 59.94**.
**Fix:** it reads **COP0 `Count`** now (294.912 MHz, mode-independent) over a stated
0.5 s window, returns a float, and has a twin — `getPresentedFps()`, counting buffer
flips via `Info::countPresentedFrame()` in `RendererCoreGS::flipBuffers` — because
frame extrapolation presents twice per loop. **Anything derived from T3 inherits the
video mode; anything derived from COP0 does not.** docs/profiling.md, "The three
frame rate counters".

## `context ^ 1` presented BLACK frames (no HUD either)

**Cause:** FIXED 1.20.1; docs/frame-pacing.md, "The black-frame defect".
`RendererCoreGS::allocateVramBuffers()` armed the display queue with
`displayedBuffer = context ^ 1`, and that function runs a SECOND time in any game
that re-lays the permanent region after boot (every neural-upscaler game). By then
`context` is wherever the three-buffer rotation left it; at 2, `context ^ 1` is
**3** — one past `frameBuffers[]` — and `flipBuffers`' `3 - shown - finished` wraps
in its `u8` cast, so the rotation drew and PRESENTED through `framebuffer_t`s read
past the array. Needed BOTH features by construction: triple buffering to make 2
reachable, the upscaler to re-arm the queue after boot.
**Fix:** **any expression of the form "the other buffer" is a two-buffer
expression** (a rebuild can also come back with FEWER buffers — clamped now), and
`reallocateBuffers()` must re-present, since skipping `programDisplay()` skips the
only write to `DISPFB` while the third buffer's address moves under it. The
instrument is the boot line `GS buffers: … x3, z …, drawing into N, showing M`,
printed once per layout — a game that re-lays prints it twice and the second is the
one that matters.

## The vblank handler grew into something that allocates / a queued frame named the old layout

**Cause:** `RendererCoreGS::onVblank` runs in interrupt context and everything it
touches must stay interrupt-safe.
**Fix:** `presentFrameBuffer` is GS privileged-register stores and nothing else — do
not grow it. The three-slot queue needs no `DIntr()` because the handler only acts
when `pendingBuffer >= 0`: while the main thread has waited for -1 the handler is
inert and `displayedBuffer` cannot move under it, which is why `flipBuffers` writes
`context` FIRST and `pendingBuffer` LAST. The other ordering rule is the
`draw_finish` handshake before queueing — the GIF is in-order, so FINISH proves the
frame is fully rasterised; queueing first puts a half-drawn frame on screen. Any
path that moves buffer addresses (`reinit`, `reallocateBuffers`) must
`resetDisplayQueue()` first.

## A no-draw busy-wait finished in a fraction of the intended time

**Cause:** `endFrame` only throttles when it renders. It calls `graph_wait_vsync()`
(gated by `isFrameLimitOn`, default true) then flips buffers — a loop that presents
each iteration is paced to 50/60 Hz, but a loop that draws *nothing* is not, and
PCSX2 races the EE ahead of the display. A COP0-`Count`-based wait (294.912 MHz)
measures wall time correctly **only while frames are being drawn**.
**Fix:** the boot Tyra splash (`info/banner.cpp`) holds ~2s by re-rendering the logo
in a COP0-timed loop rather than sleeping — re-drawing is also required because
`beginFrame` clears the framebuffer. The generated games' first `loadScene` runs
from the loop (not `init()`) so its loading-screen progress is vsync-paced.

## The boot logo came up on garbage (black on one build, BLUE on the next)

**Cause:** `Color`'s default constructor does not initialise anything ("Initialize
Color without setting default values" — it is a vector type used in hot paths), so
any `Color` MEMBER is garbage until something assigns it. `RendererCore::bgColor` is
the clear colour, and `Engine::init` calls `banner.show()` immediately after
`renderer.init()` — the logo hold clears the framebuffer with it, every frame, for
two seconds, long before any game code can call `setClearScreenColor`.
**Fix:** initialised in the constructor now. Anything read before the first game
frame — a clear colour, a mode, a flag — must be initialised where it is DECLARED
or in its owner's constructor, because "the game sets it at startup" is not true of
the engine's own boot screens.

## Split view (`RendererCoreSplitView`)

**Cause:** the split-screen raster bracket is modeled on the env-map redirect.
**Fix:** two-player support (docs/multiplayer.md): `Pad::initOptional(port, slot)`
(padInit is now once-global; an optional pad never blocks or asserts on a missing
controller — upstream `update()` busy-waited forever on DISCONN — and keeps polling
for a hot-join; sticks are centered while disconnected) plus `RendererCoreSplitView`
(`renderer/core/splitview/`, public `RendererCore::splitView`): per half it drains
PATH1 and shifts XYOFFSET so the CENTRAL h/2 rows of the *unchanged* full-screen
projection land on that half (a vertical crop; no projection change, proportions
stay exact), scissored to the half. Deliberately NO per-half clear: beginFrame's
full-screen clear covers both halves and the scissor clips every raster write (z
included) — the first version copied the env map's clear sprite and paid two
half-screen GS fills + FINISH stalls per frame for nothing. The game swaps cameras
with `renderer3D.update(cam2)` and must run animation advance/skinning only in the
FIRST half (see `splitSecondPass`). Any bracket that restores a full-screen raster
(env map!) must not run inside a split half.

## A 2D sprite cannot express a per-mode destination size

**Cause:** the framebuffer is a different SHAPE per scan mode — 512x448 interlaced,
448x448 in 480p, 448x540 in 1080i, 512x512 in full PAL — so UI authored in one
logical space must be drawn into a differently proportioned rect per mode.
**Fix:** **`Sprite::drawSize`** is a per-axis destination size, 0 = the stock
`size * scale`. Neither existing field could express it: `scale` is one float, and
in `MODE_REPEAT` `size` is the SOURCE rect. **`RendererSettings::getWindowAspect()`**
is the PHYSICAL shape of the display window on the TV (4:3, 16:9 when widescreen is
on, the pillarboxed 1792/1920 window in widescreen 1080i). Widescreen is
ANAMORPHIC, so a sprite gets stretched a third wider with nothing to widen it back
— anything that must keep its authored proportions divides its horizontal scale by
this over 4:3. Also **`Sprite::additive`** (2D sprites can opt into the additive
blend equation `Cs*As + Cd` — lens-flare ghosts, glows; `Renderer2D` pins alpha-over
per sprite otherwise).
