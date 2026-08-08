# upscaler-lab — a scene built to be GS fill-bound, so the neural upscaler has something to win

Every other example in this repo is built to look right. This one is built to be
**measured**: a deliberately overdraw-heavy PAL scene for the
[neural upscaler](../../docs/neural-upscaler.md), because BLSS trades GS fill for
EE work and until now it had never been pointed at content where that trade can
pay.

The arithmetic it exists to test: the GS draws 8 textured pixels per clock at
147.456 MHz, so one full-screen 512x448 pass costs ~194 us — **0.97 % of a PAL
frame**. Rendering at 256x224 saves `145.8 * D` us for overdraw `D`, and the
composite costs `194.4 * P` us at `P` passes. Break-even is **D >= 2.5 on the GS
alone, and D >= 9.4 once BLSS's own ~1 ms of EE is charged**. So overdraw is the
target, not triangles — and a triangle has to exceed ~288 px before its halved
version is still fill-bound at all.

Open `upscaler-lab.tyra` and Build & Run (`F5`), or headless:
`tyrax-editor --build <this folder> --run`.

## What is in the scene

A 96x96 cobbled yard between two cottages, under a haze bank thick enough to
matter.

| Piece | Count | Why |
|---|---|---|
| Cottage (`Cottage_FREE.obj`, 4 281 tris, 512² diffuse) | 2 | The textured static landmark, and **the content the BLSS corpus can actually see** — `blssscene` walks primitives, static `.obj` and terrain chunks, nothing else. Its diffuse is what makes the `texDetail` feature non-zero; that channel being identically zero is the documented mechanism behind the −0.40 dB disaster on `examples/procedural`. |
| Static boxes (wall, fence posts + rails, crates, two slabs) | 26 | Cheap, static-batched (`26 objects in 4 batches`), textured. Edge density and proxy count for almost no EE. |
| Spider (`spider.fbx`, clip `spider.walk`) | 2 | 1 092 vertices each, LOD tiers 480/180 — read off the baked `.tskl`, not guessed. ~2 ms of EE per 1 000-vertex instance on real hardware, so two is the whole animated budget. EE is the half BLSS does **not** reduce. |
| **Custom haze emitters** | **12 x 256** | **The fill.** `kind: "custom"`, `size 9.0`, `opacity 0.8`, `life 8`, `gravity -0.15` (buoyant), `grow 1.6`, spread over `[26, 1, 26]`, one shared 128² soft-puff material. |
| Campfire (`fire` 48 + `smoke` 40) | 2 | Two more emitter kinds, and something to look at. |
| Rain (`kind rain`, 200, `followPlayer`) | 1 | The many-small-quads regime beside the few-huge-quads one. |
| Cutscene Director sequence, 20 s | 1 | Two jobs — see below. |
| Flow graph on an Empty | 1 | **Square** turns every emitter off, **Circle** turns them back on: an in-run particle A/B with zero camera variance. |

**Why `kind: "custom"` (5) and not `fog` (2)**, which is what a haze bank looks
like it wants: `fog` peaks at alpha 36 while `custom` reaches 128
(`templates.cpp:7860-7889`), and **`opacity` is only serialised for
`kind == "custom"`** (`project.cpp:690`) — a `fog` emitter's opacity is silently
dropped on save. That second one is a real data-loss bug and is filed in
`docs/backlog.md`.

**Why the quads are enormous.** A particle quad spans `2 * size` world units. At
a 60° vertical FOV the visible height at distance `d` is `1.155 * d`, so a
`size 9` puff at 10 m is about 1.6 screen heights: **one full-screen
alpha-blended textured layer for one EE vertex**. Near-zero EE, near-zero VU1,
enormous GS fill. That is the lever, and the tuning below is what set the number.

## The tour

`On Start` plays a 20-second Cutscene Director sequence with `cameraEnabled` and
`skippable`: establishing shot of the yard, a push-in, **a four-second hold at
the worst-case vantage inside the haze**, a lateral across both cottages, then a
crane out. It does two jobs at once. It is a deterministic, pad-free A/B — the
same twenty seconds every boot — **and** it is a training shot:
`blssscene::authoredShots` copies a sequence's camera track straight into the
training corpus, so the net is fitted on exactly the frames the demo shows. It
carries no bars and no fades on purpose: both are full-screen 2D passes drawn
*after* the composite, so they can only dilute the comparison.

Afterwards you are standing at the south edge of the yard. Walk north into the
bank.

## Things worth trying

- **Square / Circle** — the particle A/B, from the same camera, in the same run.
  It is the honest discriminator for "how much of this frame is fill".
- Walk right up into a puff and watch it **vanish**. That is not a bug in the
  scene: the VU1 billboard program ADCs a quad whose corner leaves the GS ±2048
  raster window (`stapip_billboard_t_vu1.vclpp:21-24`), and at `size 9` that is
  anything closer than ~2 m. Every bank's centre is kept above eye height for
  exactly this reason.
- Turn the upscaler off in *Project > Preferences > Neural upscaler* and rebuild.
  On this scene that is currently the **better-looking** build — see the defect
  below.
- Re-run the training and watch the ceiling move:
  `tyrax-editor --blss-eval <this folder>` prints the oracle row before any net
  exists.

## Measured

PCSX2 2.6.3, software renderer, PAL, debug profile with the frame profiler on.
The three-step tuning loop, all at the post-sequence spawn vantage:

| Step | FPS | FRAME | SCENE | PART |
|---|---|---|---|---|
| 1. Particles **off** (Square), BLSS off | **50** | 20.00 ms | **9.34 ms** | — |
| 2. Particles **on**, BLSS off | **25** | 33.35 ms | 12.84 ms | 1.31 ms |
| 3. Particles on, **BLSS on** | **25** | 40.00 ms | 13.60 ms | 1.33 ms |

Step 1 clears its bar (comfortably 50 FPS, SCENE well under 12 ms), so the scene
is not EE-bound. Step 2 halves the frame rate with **identical EE work** — that
drop is fill, and it is what the example was built to produce. Step 3 is not a
result: see the defect below.

**The frame cost is strongly vantage-dependent, which is the whole point.** Step
2's numbers are the worst case, standing where most of a bank is in frame. A
matched pair taken from a lighter vantage in the same run, particles toggled
with Square and nothing else changed, reads **FRAME 23.87 / SCENE 10.37 on** and
**FRAME 20.00 / SCENE 9.35 off**. So depending on where you stand the haze costs
between ~1 ms and ~13 ms of frame time, and only the second of those is a
half-resolution render worth having. A scene that is uniformly fill-bound from
every angle would be a nicer benchmark and a worse demonstration.

**Read `PART` with care.** A GS-bound frame back-pressures the EE inside
`stapip.core.render`, so `PART` inflates with GS wait rather than EE work. Step 1
(particles off ⇒ EE only) is the honest discriminator, not `PART`.

**The fill had to be raised four times.** The design started at 3 banks x 128 at
`size 5.0`; that measured **49-50 FPS with BLSS off**, i.e. not fill-bound at
all, with the profiler's GS-overhang fence reading 0.04 ms. That is the cost
model restating itself: at 194 us per full-screen pass a PAL frame swallows
about a hundred of them before the GS is the limit. 12 x 256 at `size 9.0` is
roughly 8x that fill and is what finally moved the frame.

### The network

Trained on this project with `--all-shots` and shipped as `blss.net` (codegen
reads it straight out of the project directory, so `--blss-emit` is not needed):

```
half-res + bilinear                 26.413 dB   1.00 passes
half-res + BLSS (trained)           27.20  dB   1.76 passes   +0.79 dB
half-res + oracle (upper bound)     28.141 dB   1.46 passes   +1.73 dB
native full-res                     28.551 dB
```

**The oracle row is the number that matters: +1.73 dB is this scene's ceiling**,
against +0.77 dB on `examples/procedural` and **+0.00 dB on
`examples/showcase`** — the scene really does have something to win, which is
the first thing this example set out to establish. The trained net captures 45 %
of it. Per `docs/neural-upscaler.md`, the held-out decibel of a *project* corpus
is not quotable (six camera moves of one scene do not generalise) and is
deliberately not quoted here.

Not measured on real hardware. PCSX2's software GS is not a fill-rate model of
the console's, so treat every frame time above as indicative and the ratios as
the result.

## Two defects this scene found, neither of them fixed here

**1. With BLSS on, every textured primitive and the textured terrain disappear.**
Reproduced in a minimal control (a fresh `--new` fpp project, one plain-coloured
box, one box with a `map_Kd` material, terrain with and without a terrain
material):

| | BLSS off | BLSS on |
|---|---|---|
| plain-coloured box | draws | **draws** |
| box with a `map_Kd` material | draws | **gone** |
| textured terrain | draws | **gone** |
| untextured terrain | draws | draws |
| textured *model* (`.tmdl` / `.tskl`) | draws | draws |

Independent of `textureQuant` (`4bit` and `none` both fail), of static batching,
of baked AO, of fog and of the temporal pass. `VRAMSTAT` shows the textures
resident and **bound thousands of times per frame** with no evictions, so the
pass is submitted and its texture is bound and nothing reaches the low-res
target — which is what a GS alpha test rejecting every fragment looks like
(StaPip draws with the "pass only when alpha != 0" cutout rule). That is why the
step-3 row above is not a result, and why the screenshots with BLSS on show two
cottages floating over an empty sky.

**2. The picture still bobs.** `docs/neural-upscaler.md` ("The oscillation")
records this as open, and it is. Measured here on a **frozen camera** (a static
fixture, `walkSpeed`/`lookSpeed` 0, no particles, no animation), sampled at an
**odd** stride — 0.34 s is 17 frames at 50 Hz, because an even stride lands every
sample on the same jitter phase and reports a perfectly still picture:

| | mean lag-1 | max lag-1 | mean lag-2 |
|---|---|---|---|
| BLSS **off** | 0.018 % | 0.052 % | 0.034 % |
| BLSS **on** | **1.443 %** | **1.755 %** | 0.603 % |

The picture alternates between exactly two images differing in **1.71-1.76 %** of
pixels, and takes no other value; when the sampling phase happens to align, lag-1
falls to 0.05 % and lag-2 rises to 1.75 % — the two states swapping roles, which
is the definition of period 2. That is ~33x the still-picture floor. The cause is
the sub-pixel jitter (`renderer_core_blss.cpp`), and the `blssJitter` kill switch
has since landed and been tested against exactly this fixture.

**Settled 2026-08-08, by a person rather than a metric.** Three builds of this
scene differing in nothing but two flags were handed to someone and they called
them steady (BLSS off) / **"like an earthquake"** (jitter on) / steady (jitter
off). `blssJitter` now defaults to **false** repo-wide — but **this project sets
it explicitly to `true`**, on purpose: the committed `blss.net` here was fitted
with the jittered sampler, and `--blss-train`/`--blss-eval` read the project's
own flag, so flipping it would leave the example running a net fitted for a
sampler it no longer uses. So this fixture stays the jitter-**on** reference,
and it is the one that shakes. Set `"blssJitter": false` in the `.tyra` to see
the cure (and retrain if you intend to measure decibels afterwards).

Worth noting against the documented case: on **this** scene the trained net puts
**72-78 % of its weight on the temporal pass** and 0 % on point and sharpen — so
the accumulator that fuses the two jitter phases is doing most of the work here,
which is the opposite of the "jitter on, nothing fusing it" configuration
`docs/neural-upscaler.md` warns about. It bobs anyway.

## An honest limitation of the corpus

**The training corpus sees neither the particles nor the animated models.**
`blssscene` walks only primitives, static `.obj` and terrain chunks
(`blssscene.cpp:216-231`), and on the console the particle bags contribute **no
BLSS proxy at all** — `stapip_core.cpp:282-286` gives them no bbox, so the sphere
fallback has radius 0 and is rejected. The net is therefore fitted on the
cottages, the primitives and the terrain, and then run on a frame whose fill is
overwhelmingly haze it has never seen. That is a real gap in the feature, not a
property of this scene; it is filed in `docs/backlog.md`.

## Build & run

```
tyrax-editor --refresh-gen <this folder>     # codegen + the .tskl / .tmdl bakes
tyrax-editor --blss-eval   <this folder>     # the oracle ceiling, before any net
tyrax-editor --blss-train  <this folder> --all-shots -o <this folder>/blss.net
tyrax-editor --build       <this folder> --run
```

`palFullHeight` is deliberately **absent** from the manifest: `project::create`
sets it true and it is written only when true, and left on a PAL BIOS boots
512x512 instead of 512x448 — a different render size and different VRAM
arithmetic between one run and the next, which makes an A/B meaningless. For the
same reason `bloom`, `grain` and `dofAmount` are all 0 (post-fx runs *after* the
composite at full resolution, so BLSS never touches it) and `disableVsync` is
left false.

Depth of field, `Portal` objects, a second `Player` and the `Set Depth Of Field`
node are refused outright: `blssClashes()` emits `#error` into
`inc/scene_data.hpp` and the build fails.

## Assets

Three third-party assets, attributed in `THIRD-PARTY-NOTICES.txt`; the two
textures (`gravel.png`, `puff.png`) are generated. Total committed assets under
`res/`: **1.7 MB**.

The **female NPC was dropped**, and deliberately: the only variant of
`F:/Tyra-Projects/Assets/FemaleCharacter` that carries a skin at all is
`Female.fbx` at **19.2 MB** (13 geometry nodes, 545 deformers, 30 embedded
texture blobs). `Idle (1).fbx` (1.39 MB) and `Walking (1).fbx` (349 KB) are
animation-only — zero geometry, zero deformers — so neither can stand in. The
largest model committed anywhere in `examples/` is 151 KB, so shipping her would
have grown the repo by more than every other example's assets put together. The
spiders carry the animated pass instead.

**`Cottage_FREE.obj`, not `Cottage_FREE.fbx`.** `.fbx` is *always* treated as an
animated model (`project.hpp:797`, `isAnimatedModelPath`), and this cottage has
0 animation stacks and 0 texture references — so the `.fbx` would go down the
skeletal pipeline, pay per-frame EE pose work for a building, and be **invisible
to the BLSS corpus**, which skips animated models by design. The `.obj` is the
single most important asset decision in the example.
