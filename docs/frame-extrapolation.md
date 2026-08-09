# Frame extrapolation

Frame extrapolation synthesises an extra presented frame between two rendered
ones by re-drawing the last finished frame under a newer camera, so a game can
render its world at half the field rate and still hand the television a fresh
picture every field. It is the PlayStation 2 answer to "frame generation", and
the reason it is possible at all is that it **extrapolates rather than
interpolates**.

## Why not interpolation

A desktop frame generator blends frame N and frame N+1. Both halves of that are
unavailable here. Needing N+1 means holding a finished frame back for a whole
field before showing it — 20 ms of added latency on a machine that has none to
spare — and the blend needs per-pixel optical flow, which the GS has no
programmable pixel stage to compute or apply.

Extrapolation is causal: it takes the newest camera the game knows about and
re-projects the picture it already has. It costs no latency at all, and it
actually *reduces* the camera's: what the player sees tracks the stick a field
sooner than the next real render would deliver it.

## What it does

`RendererCore::presentWarpFrame(from, to)` draws the last finished frame into
the buffer being rendered to, as a grid of textured triangle strips whose
texture coordinates are the reprojection of each grid corner from camera `to`
back into camera `from`, and presents it. It clears nothing (the grid covers
every pixel) and runs no post fx (bloom, grain and grading are already in the
source image; running them again would compound them frame after frame).

The reprojection, per grid corner, is the neural upscaler's `buildReproj()` run
forwards, with the per-tile `1/w` replaced by a single plane distance:

```
sX  = (2*px/outW - 1) * to.tanHalfFovX
sY  = (1 - 2*py/outH) * to.tanHalfFovY
dir = to.fwd + to.right*sX + to.up*sY
wp  = to.pos + dir * planeDistance
rel = wp - from.pos
wPrev = dot(rel, from.fwd)                  // behind the old eye -> identity
u = (dot(rel, from.right) / (wPrev * from.tanHalfFovX) * 0.5 + 0.5) * outW
v = (0.5 - dot(rel, from.up) / (wPrev * from.tanHalfFovY) * 0.5) * outH
```

**Rotation is exact.** When `to.pos == from.pos`, `rel` is `dir * planeDistance`
and that factor divides out of both ratios — the mapping is a homography, right
at every scene depth, with no depth buffer anywhere.

**Translation uses real per-tile depth when the neural upscaler is on.** BLSS
already describes every 32-pixel tile with a mean `1/w` in order to fetch its own
temporal history (`docs/blss-reconstruction.md` §3), and both grids are the same
`kTile = 32` grid over the same raster, so the warp reads those numbers directly
— measured on `examples/showcase`: grids matching at 16x14, 176 of 224 tiles
covered, `1/w` spanning 0.015 to 10.0, i.e. depths from about 0.1 to 66 world
units in one frame. Near ground then moves a lot, distant hills little, and the
48 uncovered tiles — the sky — carry `1/w = 0` and do not move at all. That is
parallax, and it needs no depth buffer read: the numbers were already being
computed for something else.

The arithmetic that makes it fall out cleanly is dividing rather than
multiplying. `rel = dir + (to.pos − from.pos) * invW` is the same projection as
before (the result is a ratio, so scaling `rel` changes nothing), but now
`invW = 0` means "infinitely far" with no special case — which is exactly what
an uncovered tile already yields.

**Without that depth it falls back to a single plane**, `planeDistance`, which
is 0 by default and therefore rotation only. A single plane makes every pixel move as if it were the same
distance away, which scales the whole picture uniformly and reads as a **lens
zoom** — worse than a frame that simply did not move. Walking forward is where
it shows worst, not least: at the 12-unit distance this originally shipped with,
a half-unit step zoomed about 4% per synthesised frame.

With translation off, walking in a straight line makes the warp an exact
identity — the synthesised frame is a pixel copy of the one before it, so the
motion reads as ordinary judder instead of distortion, and the HUD does not
double either. Turning still reprojects exactly. `setPlaneDistance(d)` folds
translation back in for a scene that really is planar at a known distance.

## When it helps, and when it COSTS you

Presenting twice per loop means the loop can hand the display at most one frame
per field, so **the world is hard-capped at half the field rate** — 25 Hz on
PAL, 30 on NTSC. That is the whole bargain, and it only pays when the game was
already at or below that. Measured on `examples/showcase` (BLSS on, PAL):

| | world (real frames) | presented |
|---|---|---|
| extrapolation off | **44.71 Hz** | 44.7 |
| on, double buffered | 24.25 Hz | ~48 |
| on, triple buffered | 24.97 Hz | ~50 |

That scene renders 44.7 real frames a second on its own, so turning this on
**takes 20 of them away** and hands back synthesised frames that carry only
camera motion. Animation, moving objects and the HUD all drop from 44.7 to 25.
It reads exactly as "half the frame rate", and it is: the picture is smoother
only if you were below 25 to begin with.

So the rule is: **turn it on for a game that already cannot reach half the field
rate, and leave it off for one that can.** A scene at 25 Hz keeps its world rate
(measured 25 -> 24) and doubles what the television sees; a scene at 45 loses
nearly half its world updates to buy motion it already had. Triple buffering
changes this barely at all (24.25 -> 24.97) — the cap is the two presents, not
the pacing.

Making that decision automatically, per frame, is the obvious next step and is
not done: see [backlog](backlog.md).

## The measurements

Fixture: the fpp preset, interlaced-field, PAL, triple buffering on — a scene
light enough that the world rate is the field rate, so the doubling is visible
without the trade above muddying it. Measured twice: once with a script calling
`presentWarpFrame` by hand, then through the generated `FRAME_EXTRAPOLATION`
hook, which reached 25.31 Hz for the same 50.4 presented.

| | before | with extrapolation |
|---|---|---|
| game loop (world + real render) | 50.4 Hz | **25.2 Hz** |
| frames presented | 50.4 /s | **50.4 /s** |

Read from three independent places that agree: the loop rate off the EE's own
COP0 cycle counter, the game's FPS HUD (`FPS 25`), and PCSX2's own status bar
(`FPS: 50  VPS: 50  Speed: 100%`). The world is simulated and rendered 25 times
a second; the television gets 50 pictures a second.

**The identity case is the test that proves the machinery.** With `from == to`
the warp must reproduce its source exactly, and it does: the screen is a clean,
correct scene, and any error in the packet, the TEX0 binding, the region clamp,
the UV encoding or the strip vertex order would show as tearing or garbage
instead. The reprojection arithmetic was then checked on the console against its
closed form: at a 10 deg yaw with fov 60 and `tanHalfFovX` 0.659 the centre
corner's U came out at 5191/16 = 324.4 px against an identity 256 px, i.e. a
68.4 px shift where `tan(10 deg)/0.659 * 256` predicts 68; at 45 deg it reached
644 px, correctly past the buffer edge and into the clamp.

**What is NOT verified, and why.** That a *non-identity* warped frame looks
right on screen. Real and warped frames alternate every field, and the capture
tools available here (a Wayland compositor screencast) are not frame-accurate
enough to isolate one of two images alternating at 50 Hz — ten consecutive
captures of a deliberately marked warp frame returned byte-identical results,
which says the instrument is sampling one latched surface, not that the picture
is static. What the marker DID establish is that the warped buffer is scanned
out at all. Verifying the warped image itself wants either a frame-accurate
capture or a game-side A/B (render camera B for real, warp A to B, compare on
the EE); until then this page does not claim it.

Nothing here has been on real hardware.

## Turning it on

*Project > Preferences > Build > **Frame extrapolation (experimental)*** - off
by default, saved in the `.tyra` only when on, so an existing project's file and
its generated code are byte-identical until you tick it.

The generated game then presents one synthesised frame after each rendered one
(`TerrainGame::presentExtrapolatedFrame`, guarded by the `FRAME_EXTRAPOLATION`
constant, so it compiles away entirely when off). Since there is no newer pad
reading at that point in the loop, it estimates where the camera will be from
the motion it just made, carried **half a step** further - the synthesised frame
is displayed one field later, and one field is half a loop period once the world
is running at half the field rate.

It pairs naturally with [triple buffering](frame-pacing.md), but does not need
it: with two buffers each of the two presents waits for its own vsync, which
reaches the same 25 Hz world / 50 Hz picture (measured both ways).

## Calling it yourself

```cpp
// After endFrame() of a normal frame: sample the pad again, work out where the
// camera is NOW, and present one extra frame from it.
Tyra::WarpCamera from = cameraOf(previousFrameCamera);
Tyra::WarpCamera to   = cameraOf(cameraRightNow);
engine->renderer.core.warp.setPlaneDistance(distanceToWhatYouAreLookingAt);
engine->renderer.core.presentWarpFrame(from, to);
```

`presentWarpFrame` returns false and presents nothing when there is no finished
frame to warp yet (the first frame after boot, or after a display-mode switch
moved every buffer), so the return value can simply be ignored.

It pairs with **triple buffering** ([frame pacing](frame-pacing.md)): the two
presents per loop each block on a free buffer, which is what paces the whole
arrangement to one presented frame per field. It costs **no GS VRAM** — the
source is the display buffer double buffering already keeps.

## The bug this shipped with, and the rule it cost

The first version wrote `TEXA` and `COLCLAMP` into its state block and then
"restored" them afterwards. That is not a restore — **nothing else in the engine
writes either register**, so there was no previous value to put back and the
code was ASSERTING what the GS reset value is. The assertion was wrong, and
every blend after the pass inherited it: on a scene with bloom and film grain
(`examples/showcase`) the whole picture came back hue-shifted — orange sky
rendered olive, green grass orange, a cyan crate magenta — while the geometry
stayed perfect, which reads as a colour-space bug anywhere but where it was.

It survived the original testing because the fixture had **no post fx**. With
nothing blending, a wrong blend-state restore changes nothing at all.

The fix is not a better restore value, it is not writing the registers: this
pass is an opaque `DECAL` copy with `ABE = 0`, so it has nothing to blend and
nothing to clamp. It now touches seven registers (`TEXFLUSH`, `TEX0`, `TEX1`,
`CLAMP`, `FRAME`, `TEST`, `ZBUF`) and puts back exactly one (`CLAMP`) — `FRAME`,
`TEST`, `XYOFFSET` and `ZBUF` come from the shared `emitRasterRestore`, `TEX1`
is written to the engine-wide value, and `TEX0` is re-emitted per textured mesh
by the pipeline anyway (`packet2_utils_gs_add_texbuff_clut`, CLUT fields
included).

**The rule: a full-screen pass may only write global GS state it genuinely
needs, and "restoring" a register that nothing else writes is a guess wearing a
restore's clothes.** `RendererCoreBlss::composite` does the same thing with the
same two registers — it has to set them, since it really does blend — so
whether ITS restore values are right is an open question, and untested.

## Limits

- **Dynamic objects freeze** for the warped frame. They are pixels in the source
  image and the warp only knows about the camera, so a moving character is
  carried along by the camera warp and does not advance. A game that cares
  redraws them; the HUD is the same problem with the same answer.
- **Disocclusion at the frame edge stretches.** The source has no pixels for
  what just came into view, so the outermost cells clamp their last texels
  across the strip — a smear a few pixels wide at ordinary turn rates. This is
  what VR reprojection does. A guard band would fix it, and is not free: the
  framebuffer would have to be wider than the display window, which means
  splitting the physical raster from the displayed one and widening the frustum
  to match — and `M4x4::perspective` takes the raster size as its scale, so the
  widened fov/aspect would break the "frustum planes are independent of the
  raster scale" invariant the neural upscaler's host/console parity rests on.
  That is why it is not done here.
- **A degenerate basis silently becomes a copy.** Corners whose `wPrev` falls
  behind the old eye fall back to the identity sample, so a caller passing a
  zero or non-orthonormal basis gets a clean copy rather than garbage. Safe, but
  it means "the warp does nothing" and "the warp was handed a bad camera" look
  identical — log the basis before suspecting the renderer.
- **Interlaced-field is an approximation.** The source is the other field's
  image, half a scan line away. The per-field `XYOFFSET` bias is applied, but
  the warp does not otherwise model the field offset.
- **A project that OWNS its game sources does not get the generated hook.**
  `presentExtrapolatedFrame` is emitted into `src/terrain_game.cpp` and
  `inc/terrain_game.hpp`, which are user-ownable: delete their marker line and
  they stop being regenerated, switch included. Such a project calls
  `presentWarpFrame` itself - which is what the switch generates anyway.

## Where it lives

- `renderer/core/warp/renderer_core_warp.{hpp,cpp}` — the module: `WarpCamera`,
  the per-corner reprojection, the GS state block and the grid.
- `RendererCore::presentWarpFrame` — draw + flip, no clear, no post fx.
- `RendererCoreGS::getPreviousFrameBuffer` — the source, shared with the neural
  upscaler's temporal tap.
