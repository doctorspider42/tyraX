# upscaler-lab — a watchable scene that is GS fill-bound, so the neural upscaler has something to win

Every other example in this repo is built to look right. This one is built to be
**measured**: a deliberately overdraw-heavy PAL scene for the
[neural upscaler](../../docs/neural-upscaler.md), because BLSS trades GS fill for
EE work and until now it had never been pointed at content where that trade can
pay.

The arithmetic it exists to test, **as measured on hardware rather than as
derived from the GS data sheet**: one full-screen 512x448 alpha-blended textured
pass costs **0.587 ms** on a real PS2 (the sheet's 8 pixels/clock at 147.456 MHz
predicts ~194 us — a third of it, and the measured number is the one that
counts). BLSS keeps **25.9 %** of the scene's fill and costs **5.02 ms of EE +
0.46 ms of composite**, so break-even is `0.741 * 0.587 * D > 5.48`, i.e.
**about 13 full-screen coverages**. So overdraw is the target, not triangles —
and a triangle has to exceed ~288 px before its halved version is still
fill-bound at all.

The editor will now tell you where a scene sits on that line before you build
anything: *Tools > Neural Upscaler (BLSS)* > **Will the frame get faster?**. It
reads **72.6 coverages** here — 1.0 of geometry and **71.7 of haze** — against
the ~75 the hardware A/B below implies. That agreement is the point of this
example as a fixture; it is also why the emitter term exists at all, since a
count taken from geometry alone reads this scene as one coverage.

Open `upscaler-lab.tyra` and Build & Run (`F5`), or headless:
`tyrax-editor --build <this folder> --run`.

> **Before redistributing anything built from this example**, read
> [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt): two of its art assets —
> the cottage model and the animated spider — have **unverified redistribution
> terms**. They are free to build, run and measure with; shipping them is a
> question only their original licences can answer.

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

> **This table was measured with `blssJitter` ON, which is no longer what this
> example ships** (2026-08-09). It has **not** been re-measured against the
> jitter-off build: the attempt was made and the console dropped off the LAN
> mid-run, and no number was going to be invented to fill the gap. Read it as
> the timing of the jitter-**on** configuration until someone re-runs it.
>
> What is known without the re-run: the jitter changes *where* the half-res
> raster samples, not how much of it there is, and the retrained net asks for
> the same **1.76 passes** as the old one, so the fill either arm pays should be
> the same and the figure is expected to hold. "Expected to hold" is a
> prediction, not a measurement — the re-run is filed in `docs/backlog.md`. The
> rig is one line: `TYRA_FRAME_PROFILE 1` in
> `vendor/tyra/engine/inc/debug/frame_profile.hpp`, then the protocol above.

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
half-res + bilinear                 26.658 dB   1.00 passes
half-res + BLSS (trained)           27.17  dB   1.76 passes   +0.51 dB
half-res + oracle (upper bound)     27.716 dB   1.38 passes   +1.06 dB
native full-res                     28.521 dB
```

**The oracle row is the number that matters: +1.06 dB is this scene's ceiling**,
against +0.33 dB on `examples/procedural` and **+0.01 dB on
`examples/showcase`** — all three measured with `blssJitter` **off**, which is
what all three projects now ship. The scene really does have something to win —
three times what `procedural` has and a hundred times what `showcase` has —
which is the first thing this example set out to establish. The trained net
captures 48 % of it. Per `docs/neural-upscaler.md`, the held-out decibel of a
*project* corpus is not quotable (six camera moves of one scene do not
generalise) and is deliberately not quoted here.

**These numbers are lower than the ones this file used to quote, and the reason
is the sampler, not a regression.** Until 2026-08-09 this example shipped
`"blssJitter": true` and its net was fitted against the jittered sampler, which
scored **+0.85 dB trained against a +1.730 dB ceiling (49 %)**. The jitter is
what makes the two half-res phases a real quincunx pair, so it is also most of
the reconstruction there is to win: turning it off costs about 40 % of the
ceiling (+1.730 → +1.058 dB). It was turned off because **the picture visibly
shakes with it on** — see below — and a demo that has to be edited before anyone
can look at it is not a demo. The net was retrained against the sampler the
example actually ships, because `--blss-train` / `--blss-eval` read the
project's own `blssJitter`, and a net fitted for a sampler the game no longer
uses is fitted out of distribution.

**The 6 × 32 re-tune did not change the corpus, and that is not an accident**:
`blssscene` walks primitives, static `.obj` and terrain chunks and **never sees
an emitter**, so deleting six haze banks left the training corpus byte-identical
(156 frames, 34 944 tile samples). Emitters change what the *console* draws, not
what the trainer sees.

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
off).

**So as of 2026-08-09 this example ships `"blssJitter": false`, like the rest of
the repo, and its `blss.net` is retrained to match.** It used to ship `true` as
"the jitter-on reference", with a note telling you to flip it before showing the
example to anyone — which is the wrong default for the project's flagship
demo. A reference build you have to repair before you can look at it is a
liability, not a reference.

**To see the shake, flip one line** in `upscaler-lab.tyra`:

```json
"blssJitter": true,
```

Rebuild and it is build **B** of the A/B/C in `docs/neural-upscaler.md`. Retrain
first (`--blss-train . --all-shots -o blss.net`) if you intend to quote decibels
afterwards — the trainer reads that flag, so the shipped net is fitted for the
un-jittered sampler and would otherwise be out of distribution. Do not commit
either change.

**Confirmed still, by capture, on the shipped configuration** (2026-08-09). The
gate is the one in `docs/profiling.md` — 480p progressive, HUD off, camera
frozen (`walkSpeed`/`lookSpeed` 0), **emitters off** (one `press square`), 40
GDI grabs of the render child **back to back** rather than on a stride, no
`-Trim`, clustered by pairwise difference rather than counted:

| | frame-to-frame | best 2-cluster split | within | between | lag | verdict |
|---|---|---|---|---|---|---|
| jitter **off** (shipped) | 0.031/255 | **38 / 2** | 0.0282 | 0.0352 (**1.25x**) | (0,0) | **stable** |
| jitter **on** (reference) | — | 20 / 20 | **0.0000** | 1.15/255 | (0,0) | alternating |

**A period-2 alternation is two *balanced* clusters, near-zero within and large
between.** 38/2 at a ratio of 1.25x is not a split at all, it is the clustering
finding nothing; the 0.03/255 residual is the same "stable" floor the BLSS-off
arm measures. Byte-identical frames were the expected result and are not quite
what came out — two things move that are not the upscaler, and both were
identified rather than assumed: the debug HUD prints a live frame counter, and
**PAL interlaced mode alternates fields, which is itself a period-2 signal in a
window capture**. The table above is from a build with the HUD off and
`displayMode` progressive; measured *with* those two confounds in, the same
scene reads 0.10-0.15/255 of pure instrument noise, above the artefact it is
supposed to detect. That is the "an instrument whose noise floor is above the
signal is not measuring the signal" rule, met twice in one afternoon.

Worth noting against the documented case: on **this** scene the trained net puts
**72-78 % of its weight on the temporal pass** and 0 % on point and sharpen — so
the accumulator that fuses the two jitter phases was doing most of the work
here, which is the opposite of the "jitter on, nothing fusing it" configuration
`docs/neural-upscaler.md` warns about. It bobbed anyway. That is why the cure
here is the sampler and not the net.

## An honest limitation of the corpus

**The training corpus does not see the particles.** `blssscene` walks
primitives, static `.obj`, the terrain and — since the animated models were
found to be missing from it — skinned `.glb`/`.fbx` posed per console frame. It
does **not** walk emitters, and on the console the particle bags contribute **no
BLSS proxy at all** (`stapip_core.cpp` gives a billboard bag no bbox, so the
sphere fallback has radius 0 and is rejected). The net is therefore fitted on
the cottages, the primitives, the terrain and the spiders, and then run on a
frame whose fill is overwhelmingly haze it has never seen. That is a real gap in
the feature, not a property of this scene; it is filed in `docs/backlog.md`.

**The speed estimate is the one place that gap is closed**, and only
approximately: `blss::measureCoverage` reads the emitters out of the project and
expands each into `count` camera-facing quads `2 * size` across, placed through
the spawn box and sized by the average of the kind's own life curve. It is not a
simulation and it does not feed the network — it exists so that "will the frame
get faster" is not answered from 1.0 coverage of cobbles on a scene that is 99 %
haze.

## Build & run

```
tyrax-editor --refresh-gen <this folder>     # codegen + the .tskl / .tmdl bakes
tyrax-editor --blss-eval   <this folder>     # the oracle ceiling, before any net
tyrax-editor --blss-train  <this folder> --all-shots -o <this folder>/blss.net
tyrax-editor --build       <this folder> --run
```

The **speed** half has no CLI verb — it is a second of in-process work with
nothing to write, so it lives only in the window: open *Tools > Neural Upscaler
(BLSS)* and press **Will the frame get faster?**. Both halves together produce
one answer ("TURN IT ON. The picture has room and the frame gets shorter." on
this example), and the per-camera-move breakdown is behind the fold under it.

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
