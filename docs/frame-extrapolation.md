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

**The generated game now makes that decision itself**, every frame
(`TerrainGame::extrapolationWorthIt`), so the switch means "use it where it
helps" rather than "use it always". It compares the loop's WORK against a
threshold that depends on the buffering:

| buffering | threshold | why |
|---|---|---|
| double | 1 field | the loop is quantised to whole fields anyway, so any overrun already buys a second, mostly idle field - the warp fits in it |
| triple | 2 fields | no quantisation, so the loop is work-bound and a second present costs a real field unless the work already fills two |

Work is the loop PERIOD minus everything the renderer spent stalled
(`RendererCore::takeStallTicks`). Measuring the renderer's own span instead
would miss a game that is slow in its scripts - they run outside
beginFrame/endFrame, and a 25 ms script went unnoticed by exactly that bug while
this was being written.

The hysteresis is not decoration: switching the extra present on and off changes
the very rate the decision is read from, so a single threshold oscillates. It
takes 15% over to switch on, 5% under to switch off, and eight frames of
agreement either way. Every flip logs a line - "the feature is on but nothing
happens" and "the gate declined" are otherwise the same picture:

```
Frame extrapolation ON - frame work 13623251 EE ticks, threshold 11796480, field 5898240
```

Measured in both directions: `examples/showcase`, which renders faster than the
gate's threshold, went back to **46.68 Hz** of world (from the 24.25 the
ungated version cost it), and a fixture loaded to 46.2 ms per loop opened the
gate and ran 19.5 Hz of world for ~39 presented, against 21.6 presented without
it.

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

> **That HUD reading was taken before the counter was fixed, and it stands —
> this fixture is PAL, where its constant was correct** (docs/profiling.md, "The
> three frame rate counters"; a progressive fixture would have printed half).
> Two things about it have changed since. The HUD now prints **both** rates when
> they differ — `FPS 25.2 SHOWN 50.4` here — so the doubling this page is about
> is legible on screen instead of having to be inferred from a PCSX2 status bar
> that counts something else. And it is an average over a stated window rather
> than one frame's delta, so it no longer jitters. The table above is unchanged
> by either.

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

*Project > Preferences > Display > Frame delivery > **Present a synthesised
frame between rendered ones*** - off
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

## The ground plane

Confirmed on real hardware: the single-plane model looks **better** than
rotation-only, even though it is geometrically wrong. Rotation-only is exact,
and on a straight walk that exactness makes the synthesised frame a pixel-perfect
DUPLICATE, which reads as judder; a wrong motion beat no motion. That result is
what the default now follows.

But a fixed plane has one artefact worth removing for free: it moves the horizon
and the sky, which should not move at all. So the default translation model is
now the **ground plane**, which is analytic rather than guessed. A corner's view
ray meets the floor at

```
w = h / -dir.y          (h = eye height above the floor)
1/w = -dir.y / h
```

so depth grows toward the horizon by itself, and a ray at or above the horizon
never meets the floor — `1/w` is 0 there and that part of the picture only
rotates. Near ground moves, distant ground moves less, sky stays put: parallax
from one number, no depth buffer, a few operations per grid corner.

The eye height comes from the terrain under the camera. In a scene with **no**
terrain `terrainHeightAtScene` answers a deep but finite void, so the eye height
is enormous, `1/w` collapses to ~0 and the warp degrades to rotation only —
which is the right answer when there is no floor.

Precedence: the neural upscaler's real per-tile depth, then the ground plane,
then the fixed distance, then rotation only.

## Getting the fixed plane back on purpose

The fixed-distance plane is still there: turn *Ground plane* off and set
*Translation plane* — **12** is what the first version shipped with. It moves the
whole picture uniformly, sky included, which is what the ground plane exists to
avoid; keep it for a scene with no meaningful floor, or to A/B the two.

**Always synthesise (ignore the gate)** is next to it, and exists because the
gate measures EE work: a scene held back by the GS rather than the EE keeps it
shut. `examples/raytraced-mirror` sits at 26.36 Hz with the gate never opening
once. Forced, with the plane at 12, it runs 23.03 Hz of world for ~46 presented
— which is the configuration to judge on hardware.

## Steering it from the game

The project preference is what **compiles the feature in**; the **Set Frame
Extrapolation** flow node steers it while the game runs, through
`ScriptContext::frameExtrapolation` (the same shape every other runtime request
uses - a node cannot call the game, so it writes a request and the game latches
it):

| Mode | Meaning |
|---|---|
| 0 | off |
| 1 | on, subject to the per-frame gate - what the preference alone does |
| 2 | on, gate ignored |

**Mode 2 is the cutscene case, and it is why the mode is not a bool.** During a
cinematic the camera is doing the moving and the world can afford to run slower,
so the smoother picture is worth it - but the gate measures only whether the
frame overruns a field, so on a scene that is already fast it would decline
exactly when you want it. Fire mode 2 at the start of the cutscene and mode 1
(or 0) at the end.

Per scene rather than per cutscene needs no new mechanism either: fire the node
from an `On Start` graph in that scene.

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

## The neural upscaler, and the two ways this feeds it its own output

BLSS samples the previous **presented** frame as its temporal history and
reprojects it as if the scene had drawn it (`docs/blss-reconstruction.md` §6):
it allocates no history buffer of its own, the other display buffer *is* the
history. Extrapolation puts a frame in that buffer which the scene did not
draw, and an accumulator handed its own displaced output compounds. There are
two distinct failures here and both are guarded, in two different places,
because they have different shapes.

**1. The history must never be a synthesised frame.** `flipBuffers` records
`lastRealBuffer` and skips it for a `synthetic` flip, so both the warp and BLSS
ask for `getPreviousRealFrameBuffer()`. The warp asks so that a second warp in
a row cannot warp its own output; BLSS asks so that the reprojection it does
describes a frame the scene really rendered. **Answer: with this in place,
BLSS's history is never a warped frame** — not after one synthesised frame, not
after a run of them.

**2. With TWO display buffers, that answer degenerates into a different
problem.** Extrapolation flips twice per loop, so `context` returns to where it
started: every rendered frame is composited into the *same* buffer, and that
buffer is the one that received the previous rendered frame. The correct
question — "which buffer holds the last real frame" — then has the render
target as its correct answer, and sampling a render target while writing it is
feedback rather than history. With three buffers the three indices are distinct
by construction and this cannot arise.

`RendererCoreBlss::composite` therefore compares the two addresses and **drops
the temporal pass** when they are equal, warning once in the game's log. It is
the honest degradation: passes 1, 2, 4 and 5 read the low-res target and are
unaffected, so the picture stays correct and only the temporal accumulation is
lost. *Project > Preferences > Display* says the same thing in the dialog when a
project has both features on without room for a third buffer.

Note this is usually latent rather than visible: the pass only runs at all when
the network asks for a non-zero temporal weight somewhere, and the shipped
default net asks for none on every project measured so far. A project that
trained its own net is the one that would have seen it.

**The two features share the expensive half of the reprojection and duplicate
the cheap half, deliberately.** BLSS already computes a mean `1/w` per 32-pixel
tile in order to fetch its own history, and the warp reads exactly those
numbers (`getTileInvW`) — that is the depth estimation, the part that would
otherwise need a buffer read. What is duplicated is the per-corner projection
itself, ~289 corners of dot products, and it is duplicated because the two are
answering different questions at different points in the frame: BLSS maps this
frame's pixel back to the previous camera, the warp maps the last frame's pixel
forward to an extrapolated one. Folding them into one helper would save no work
— neither camera pair is available when the other runs — so the sharing stops
where the sharing is worth something.

**No VRAM is at stake between them.** The warp allocates nothing (it samples a
display buffer that already exists) and writes no depth: its state block sets
`ZMSK = 1` and its restore goes through the one shared `emitRasterRestore`,
which puts `zBuffer.mask` back — so the reduced z allocation BLSS makes is never
written outside, in either direction.

## Why not with the upscaler

**The editor will not let you switch both on, and the build refuses the pair if
one arrives anyway.** The dialog half came second and is the one a user meets:
extrapolation and the upscaler sit in the same *Display ▸ Frame delivery* block,
and whichever is already on greys the other out with the reason in line — the
only one of the five upscaler clashes that can be prevented rather than warned
about, because it is the only one that is setting against **setting** rather than
setting against scene content. Only the *tick* is ever blocked, so a project that
already has both (a hand-edited `.tyra`, an older editor, a *Set Frame
Extrapolation* node) can always turn one off. See
[The two settings exclude each other](neural-upscaler.md#the-two-settings-exclude-each-other).

Underneath it the build still refuses (`blssClashes()`, `src/templates.cpp` — the
fifth condition, beside depth of field, portals and split view, and per scene
like the rest), because a `.tyra` can be hand-edited, an older editor never knew
about the pair, and a flow node can turn extrapolation on at runtime. The refusal
is one short `#error` in `src/gen/blss_interlock.gen.cpp` — a TU of its own, so
it is printed once instead of once per includer of `scene_data.hpp`; the
reasoning is [here](neural-upscaler.md#the-refusal-is-one-short-line-in-a-file-of-its-own). This paragraph used to say the opposite: that the combination "is correct
with three buffers and degrades in a named, logged way with two", so no `#error`
was warranted. **That was wrong, and a user found it by walking around.**

Reported as *"turn on extrapolation and BLSS together and the screen completely
falls apart"*, and then, decisively: *"it only breaks when the character
moves."* Reproduced on that project (progressive, **three** display buffers,
neural mode at 2×2, PCSX2 software renderer, player driven by `--pad`). Parked,
all four arms are indistinguishable. Walking:

| arm | parked | in motion |
|---|---|---|
| upscaler alone | clean | clean |
| extrapolation alone | clean | clean |
| **both** | clean | **frame tears into cells that disagree** |

— a second displaced copy of near geometry, hard rectangular seams across the
sky, object silhouettes pasted at 32-pixel granularity. Flat regions stay clean,
which is why a still frame and a flat-shaded fixture both pass.

**The mechanism is not a defect in either feature's bookkeeping.** Both rebuild
a frame by reprojecting the previous one through the camera delta, and both are
approximations whose error grows with that delta. Extrapolation presents twice
per loop, so the world runs at **half** the field rate and the camera moves
**twice as far between two RENDERED frames** — which is exactly the interval
BLSS's temporal pass reprojects across. Turning extrapolation on therefore does
not merely add a second approximation beside the first; it doubles the input to
the first one as well. Measured on that project, BLSS's own per-corner
reprojection offset (`buildReproj`, instrumented) peaks at **158 px of a 448 px
raster with extrapolation off and 201 px with it on**, while the warp's grid is
displaced by the same doubled delta at the same time.

**Two tempting explanations were disproved by measurement — do not re-open
them.**

- *"The temporal pass is sampling a warped frame."* No. With three buffers the
  rotation was **logged frame by frame** (`hist`/`target` VRAM addresses against
  the three buffer bases): rendered frames cycle through all three, warped
  frames take the one that is neither, and the history is always the previous
  **rendered** frame with its content intact. The guard in §2 above is correct
  and simply never fires here.
- *"Raster state leaks across the warp."* No. A leaked `SCISSOR`/`XYOFFSET`/
  `FRAME` is **static register state** and would wreck a parked frame too —
  parked frames are clean, in every arm.

**Refused rather than degraded, because no partial measure fixed it.** Dropping
the temporal pass — the strongest single contributor — visibly reduced the
tearing but left the warp's own grid coming apart under the same doubled delta.
Either feature **alone** is clean, so the honest answer is that a project picks
one. A build that refuses with a sentence is better than one that disintegrates
after the player takes a step.

The editor says the same thing live, at both points of choice: *Project >
Preferences > Frame delivery* (the two controls now sit in one block) and
*Scene > Scene Preferences > Neural upscaler*, which is the one place a
per-scene override can create the refused pair without opening Preferences at
all.

**If you want both anyway**, the shape of a future fix is a temporal pass that
is gated on its own reprojection magnitude rather than on the network's weight
alone — but that is a change to the twin contract in
`docs/blss-reconstruction.md` (the host oracle in `src/blss.cpp` models the same
blend), so it is a retraining question and not a one-line clamp.

## Limits

- **The HUD is kept out of the warp automatically.** Everything drawn through
  the 2D path reports its screen rect (`RendererCore::note2dRect`, fed from
  `Renderer2D::render`), and the warp fades itself out over that region - fully
  still inside it, ramping back to the full warp one tile outside, so there is
  no seam. The HUD therefore stays where it was drawn instead of being carried
  along with the world, which is what made it double and jitter at every turn.
  It is derived, not declared: no project has to describe its own HUD, and a
  full-screen 2D overlay correctly disables the warp entirely. `setKeepHud`
  turns it off. The cost is that the world visible *inside* the HUD region does
  not move on a synthesised frame - unnoticeable behind opaque glyphs in a
  corner, and the reason the region is what 2D touched rather than the glyphs.
- **Dynamic objects still move with the camera.** They are pixels in the source
  image and the warp only knows about the camera, so a moving character is
  carried along and does not advance. A game that cares redraws them; unlike the
  HUD they are 3D, so the 2D-bounds trick does not reach them.
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
- `RendererCoreGS::getPreviousRealFrameBuffer` — the source, and the same call
  the neural upscaler's temporal tap makes. `getPreviousFrameBuffer` (newest
  finished, synthesised or not) is deliberately NOT what either of them uses;
  see the section above.
- `RendererCoreBlss::composite` — the second half of that interlock: it drops
  the temporal pass outright when the history buffer turns out to BE this
  frame's render target.
