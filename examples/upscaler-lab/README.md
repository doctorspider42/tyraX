# upscaler-lab — a watchable scene that is GS fill-bound, so the neural upscaler has something to win

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
matter and thin enough to watch.

| Piece | Count | Why |
|---|---|---|
| Cottage (`Cottage_FREE.obj`, 4 281 tris, 512² diffuse) | 2 | The textured static landmark, and **the content the BLSS corpus can actually see** — `blssscene` walks primitives, static `.obj` and terrain chunks, nothing else. Its diffuse is what makes the `texDetail` feature non-zero; that channel being identically zero is the documented mechanism behind the −0.40 dB disaster on `examples/procedural`. |
| Static boxes (wall, fence posts + rails, crates, two slabs) | 26 | Cheap, static-batched (`26 objects in 4 batches`), textured. Edge density and proxy count for almost no EE. |
| Spider (`spider.fbx`, clip `spider.walk`) | 2 | 1 092 vertices each, LOD tiers 480/180 — read off the baked `.tskl`, not guessed. ~2 ms of EE per 1 000-vertex instance on real hardware, so two is the whole animated budget. EE is the half BLSS does **not** reduce. |
| **Custom haze emitters** | **6 x 32** | **The fill.** `kind: "custom"`, `size 9.0`, `opacity 0.8`, `life 8`, `gravity -0.15` (buoyant), `grow 1.6`, spread over `[26, 1, 26]`, one shared 128² soft-puff material. Six banks at x −8…+9, z −18…+15, every centre above eye height. **Tuned against the console, not the emulator** — see [Measured](#measured). |
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
  It is still the **sharper** build: from the parked vantage the BLSS-off frame
  resolves the cobbles crisply, while the BLSS-on frame smears them into
  directional streaks at grazing angles. That is the reconstruction cost of a
  quarter-area raster on a high-frequency ground texture, and the PSNR table
  prices it — 27.25 dB against 28.52 native. What you buy for it is the frame
  rate in [Measured](#measured).
- Re-run the training and watch the ceiling move:
  `tyrax-editor --blss-eval <this folder>` prints the oracle row before any net
  exists.

## Measured

**On a real PlayStation 2**, over ps2link, with the frame-timing rig
(`docs/profiling.md`): `buildProfile debug`, PAL interlaced, Live Link / Live
Debugger / Live Logic / Remote Pad / Time Machine **all off**, the 20 s tour
followed by the parked camera. Samples are the per-frame `FTRAW` ticks of frames
**550–1611**, inside the parked region in both arms. **Two runs per arm, all four
cross-pairings.** Sign: **d = work(off) − work(on)**, so d > 0 means BLSS made
the frame shorter.

| arm | mean | median | p95 | max | FPS |
|---|---|---|---|---|---|
| BLSS **off** | **52.86 ms** | 53.01 | 54.44 | 55.37 | **18.9** |
| BLSS **on** | **32.98 ms** | 32.93 | 33.62 | 34.37 | **30.3** |

**d = +19.88 ms, 95 % CI [+19.81, +19.95], sd 1.12, n = 1024 paired frames** per
pairing; the four cross-pairings span **0.014 ms** against an effect of 19.9.
**A 1.60x speedup, on a scene you can actually watch.**

`drain` reads **0.02 ms in both arms** — see the boxed correction in
`docs/profiling.md`. It is not the EE/GS discriminator and never was.

### Why 6 x 32 and not 12 x 256

The original scene was tuned against **PCSX2's FPS counter**, and PCSX2
under-reports GS fill by **76x**. So the haze was raised until the *emulator*
moved, which put it about two orders of magnitude past what a console can draw:
**530 ms/frame with BLSS off — 1.9 FPS**, and 6.3 FPS with it on. A 3.37x
speedup that nobody can look at is a stress case, not a demo.

Re-tuned against the console, and the two measurements together pin the whole
fill model, because they differ in nothing but particle count:

| | ms per haze particle | intercept at N = 0 |
|---|---|---|
| BLSS off | 0.1658 | **21.2 ms** |
| BLSS on | 0.0429 | **24.8 ms** |

- **BLSS keeps 25.9 % of the fill.** `blssScale 0` is `Scale::X2Y2` — half in
  *each* axis, so a quarter of the pixels. Not half; `docs/profiling.md` said
  half for a week and the break-even it computed was ~70 % too pessimistic.
- **The scene's non-haze floor is 21.2 ms off / 24.8 ms on.** That is why the
  demo lands at 19 → 30 FPS and not higher: with BLSS on this scene cannot beat
  ~40 FPS however thin the haze gets, and **thinning the haze shrinks the win**,
  because the haze is the only thing BLSS is paid to remove. 6 x 32 is the
  compromise: both arms watchable, the win still 1.6x, and the scene still ~75
  full-screen coverages — comfortably above the ~13-coverage break-even.
- `count` was cut rather than `size`, so every puff looks exactly as it did and
  2 880 particles of EE come off **both** arms.

**The Square / Circle toggle is still the honest discriminator** for how much of
the frame is fill: same camera, same run, emitters off and on.

### The network

Trained on this project with `--all-shots` and shipped as `blss.net` (codegen
reads it straight out of the project directory, so `--blss-emit` is not needed):

```
half-res + bilinear                 26.395 dB   1.00 passes
half-res + BLSS (trained)           27.25  dB   1.76 passes   +0.85 dB
half-res + oracle (upper bound)     28.125 dB   1.46 passes   +1.73 dB
native full-res                     28.521 dB
```

**The oracle row is the number that matters: +1.73 dB is this scene's ceiling**,
against +0.77 dB on `examples/procedural` and **+0.00 dB on
`examples/showcase`** — the scene really does have something to win, which is
the first thing this example set out to establish. The trained net captures 49 %
of it. Per `docs/neural-upscaler.md`, the held-out decibel of a *project* corpus
is not quotable (six camera moves of one scene do not generalise) and is
deliberately not quoted here.

**The re-tune did not change the corpus, and that is not an accident**:
`blssscene` walks primitives, static `.obj` and terrain chunks and **never sees
an emitter**, so deleting six haze banks left the training corpus byte-identical
(156 frames, 34 944 tile samples, oracle +1.730 dB before and after). The net was
retrained and recommitted anyway, because **the activation table changed the
forward pass** — and it came out slightly better: **+0.85 dB against the previous
+0.78**, 49 % of the ceiling against 45 %.

**What it costs the EE, measured on the console** (`FTSPLIT`, this scene):

| term | ms |
|---|---|
| `proxy` — the bag-proxy feed, inside scene submission | 2.69 |
| `net` — the MLP | **0.79** |
| `pkt` — the grid packet build | 0.55 |
| `reproj` | 0.28 |
| `feat` | 0.19 |
| `beginScene` / `endScene` | 0.41 / 0.11 |
| **total** | **5.02** |

`net` was **1.93 ms** until the activation table was enabled on both twins; the
emulator predicted that cut would be worth 2.11 ms and it is worth **1.14** here.
`proxy` is now more than half the bill.

## Two defects this scene found — one fixed, one switched off

**1. ~~With BLSS on, every textured primitive and the textured terrain
disappear.~~ FIXED — the z mask was never on** (see
`docs/blss-reconstruction.md` §6). Re-checked on the re-tuned scene in PCSX2's
software renderer, BLSS on, from the parked vantage: both cottages, their window
and wall textures, the crates and the cobbled terrain all draw. The table below
is kept because it is a good minimal control and because the *shape* of the
symptom — texture resident, bound thousands of times per frame, nothing reaching
the target — is what a GS alpha-test cutout looks like, and that trap is real
elsewhere. As originally reproduced in a minimal control (a fresh `--new` fpp project, one plain-coloured
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
