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
counts). BLSS keeps **24.5 %** of the scene's fill — *fitted* over five hardware
load points, not assumed from the quarter-area raster — and costs **4.60 ms of
EE + 0.50 ms of composite fill**, so break-even is
`0.7548 * 0.5174 * D > 5.10`, i.e. **13.1 full-screen coverages at this
project's 512×448 raster** (11.4 at 512×512 — one full-screen pass is priced
per PIXEL, measured 2.2524 ms/Mpx, so the line moves with the display mode; the
bare "11.5" this README used to print was a 576i figure). So overdraw is the
target, not triangles —
and a triangle has to exceed ~288 px before its halved version is still
fill-bound at all.

The editor will tell you where a scene sits on that line before you build
anything: *Tools > Neural Upscaler (BLSS)* > **Will the frame get faster?**, or
headlessly `tyrax-editor --blss-coverage <this folder>`. It reads **72.23
coverages** here (p95 93.32) — **0.96 of geometry and 71.27 of haze**.

**Those two instruments do not agree, and the gap is now LOCATED rather than
open.** Working back from the hardware fit, this scene's true fill is 34.46 ms =
**58.7** blended-pass equivalents. Two explanations of the difference have been
measured and both are dead. It is not the unit — geometry is ~1.0 of the ~72.5
against a measured ceiling of ≤1.14, and re-weighting it moves the total by 0.49
against a 13.93 gap. And it is not the **camera**: walked under this fixture's
*own* parked gameplay camera (the standpoint the console A/B sampled, authored as
a training vantage — eye `(0, 1.8, 27)` looking −Z) the estimator reads **78.99**,
and under the Cutscene tour **85.64**, i.e. *above* the six-move mean (72.63 on
the geometry those readings were taken on) rather than near the 57.7 that theory
predicted.

What it is, is a constant scale error in the modelled emitter term. With the haze
banks stepped 6 / 4 / 2 / 0 exactly as the hardware rig stepped them, the counter
reads 78.99 / 46.82 / 19.55 / 1.55 against 58.70 / 37.17 / 15.40 / 1.14 measured
— a ratio of 1.35 / 1.26 / 1.27 / 1.36 across a fifty-fold range of load, which
no error in where the puffs are or how big they are could hold. Fourteen of those
points are arithmetic: `FrameProfile::gsFillProbe` sizes its calibration sprite
from the current framebuffer and the fixture that measured 0.5872 ms ran PAL 576i,
so that figure is per **512×512**, while a coverage here is per this project's own
**512×448** — 14.3 % fewer pixels, i.e. 0.5138 ms. The rest (a counted haze
coverage costs 0.436 ms) is a magnified 128² puff being cheaper per pixel than the
probe's 1:1 framebuffer blit, and only a console settles that. Nothing has been
rescaled; the tables are in `docs/neural-upscaler.md`, "The overdraw count is an
INDEX". The emitter term is why this is a discrepancy of a third rather than of
two orders of magnitude: a count taken from geometry alone reads this scene as
**one** coverage.

Open `upscaler-lab.tyra` and Build & Run (`F5`), or headless:
`tyrax-editor --build <this folder> --run`.

> **Everything in this example is CC0 1.0** — see
> [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt). Build it, ship it, sell it.
> It was not always: until 2026-08-09 the buildings and the animated model were
> free downloads whose **redistribution terms had never been verified**, and this
> file carried a banner saying so. They are gone.

## What is in the scene

A 96x96 cobbled service yard between two industrial blocks, under a haze bank
thick enough to matter and thin enough to watch.

| Piece | Count | Why |
|---|---|---|
| Depot block (`res/models/depot/block-{west,east}.obj`, 1 134 + 998 tris) | 2 | The textured static landmark, and **the content the BLSS corpus can actually see** — `blssscene` walks primitives, static `.obj` and terrain chunks, nothing else. Their `map_Kd`s are what make the `texDetail` feature non-zero; that channel being identically zero is the documented mechanism behind the −0.40 dB disaster on `examples/procedural`. Kit-bashed from Kenney's Retro Urban Kit (CC0) — see [How the buildings were made](#how-the-buildings-were-made). |
| Static boxes (wall, fence posts + rails, crates, two slabs) | 26 | Cheap, static-batched (`26 objects in 4 batches`), textured. Edge density and proxy count for almost no EE. |
| Parked trucks (`res/models/depot/trucks.obj`, 108 tris) | 1 object, 3 vehicles | Three whole kit models merged into **one material**, so the lot is a single bag submit. Character for the price of one draw call. |
| Wobbler (`wobbler.glb`, clips `Wiggle` / `Twist`) | 2 | **123 vertices each** — the animated pass, kept deliberately small. Skinning is EE work and EE is the half BLSS does **not** reduce, so the animated budget is there for realism, not for the measurement. It replaced two 1 092-vertex spiders and that is most of the 4 ms of EE this scene shed; see [What the asset swap moved](#what-the-asset-swap-moved). |
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
the worst-case vantage inside the haze**, a lateral past the west block, then a
crane out. Its t = 16 s key sits at world `(-15, 3, -1)`, which is **2.6 m clear
of the west block's north face** — the block is centred at `(-15, 0, -8.5)` and
is 9.8 m deep, so growing it southward is fine and growing it northward puts the
camera inside the building. That is a real constraint, not a note.

The tour does two jobs at once. It is a deterministic, pad-free A/B — the
same twenty seconds every boot — **and** it is a training shot:
`blssscene::authoredShots` copies a sequence's camera track straight into the
training corpus, so the net is fitted on exactly the frames the demo shows. It
carries no bars and no fades on purpose: both are full-screen 2D passes drawn
*after* the composite, so they can only dilute the comparison.

Afterwards you are standing at the south edge of the yard. Walk north into the
bank.

### Under a controller

Everything above was measured from a **parked or frame-indexed** camera, which is
what makes an A/B possible and is also the one thing a player never does. First
pad-driven run, 2026-08-09: ~110 s of `--pad` driving (`tyra-testing`) in PCSX2's
software renderer, shipped configuration, one 20 640-frame boot.

- **`bin/log.txt` is clean** — no assert, no `Max buffer size in VU1`, zero
  texture evictions and zero re-uploads, GS free VRAM flat at 1.19 MB the whole
  way. The only two lines are the pre-existing `DynamicMesh` notes for the
  animated models, printed at load. (Re-checked after the CC0 asset swap on the
  shipped build: still clean, still zero evictions, and free VRAM is now **1.27
  MB** — the kit's 64² textures cost a fraction of the 512² diffuse they
  replaced.)
- **The frame splits exactly where this example says it does.** Away from the
  haze it holds `FRAME 20.00 ms` at 50 FPS; driven into the bank at the south end
  of the yard it runs **23.2–31.7 ms at 25 FPS** — and `PART`, the EE particle
  phase on the HUD, reads **0.46 ms in every single sample**, moving or not. The
  emitters cost the EE nothing and the frame everything, under a pad, without a
  measurement rig.
- **Standing inside a bank costs about double.** With one haze emitter moved to
  eye height 5 m ahead (a scratch copy — do not commit it), walking in and
  stopping reads `FRAME 40.0 / SCENE 15.6 / PART 0.46`. Near puffs drop out as
  described above; nothing corrupts, and no raster-window wrap smear appears —
  the quad is discarded cleanly rather than wrapping.

These are **emulator** milliseconds and are not comparable to the console table
in [Measured](#measured) — PCSX2 mis-prices GS fill by a large factor
(`docs/profiling.md`). They are the shape, on the hardware nobody was driving.

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
  prices it — 27.17 dB against 28.52 native (the 27.25 this line used to quote
  was the jitter-on net, which is not what ships). What you buy for it is the frame
  rate in [Measured](#measured).
- **Switch *Reconstruction* to Plain and rebuild.** This project is the reason
  the mode exists and the reason it has to be measured rather than assumed: its
  own trained net does **not** ask for nothing. Under *Debug view* > *Log the
  feature spread* it reads `BLSSOUT temporal=0.000/0.044/0.169` and
  `BLSSFILL ... temporal=58.0% passes=1.58` - point and sharpen are dead, the
  temporal pass is not - so here Plain is a genuinely different picture and a
  4.08 ms cheaper frame, not a free lunch. On a project whose net **does** ask
  for nothing (this one with *Temporal* switched off is the constructed case)
  the two builds are byte-identical: 0 differing pixels of 811 426, nine
  cross-pairings, camera pinned and every emitter hidden. See
  [Plain mode](../../docs/neural-upscaler.md#plain-mode--the-reduced-raster-without-the-network).
- Re-run the training and watch the ceiling move:
  `tyrax-editor --blss-eval <this folder>` prints the oracle row before any net
  exists.

## Measured

> ### ⚠ The hardware table below describes this example's PREVIOUS geometry
>
> It was taken on 2026-08-09, before the art assets were replaced with CC0 ones
> (see [What the asset swap moved](#what-the-asset-swap-moved)). **The GS fill it
> measures is intact** — the emitters were not touched and `--blss-coverage` reads
> 72.23 against the 72.63 it read then, a 0.6 % difference. **The EE is not**: the
> swap took roughly **4 ms a frame** out of the EE budget, almost all of it the
> animated model. So both arms should now land about 4 ms lower and the ratio
> should come out *above* 1.63x — but that is arithmetic over a PCSX2 measurement,
> not a hardware measurement, and this file does not print predictions as results.
>
> **The hardware re-run is OWED.** It could not be done here: the console at
> 192.168.100.150 was unreachable for the whole session (`Destination host
> unreachable` — not even ICMP), and PCSX2 is inadmissible for GS fill, which it
> under-reports by 76x. Every number below is still a real measurement of a real
> console; it just describes the scene as it was.

**On a real PlayStation 2**, over ps2link, with the frame-timing rig
(`docs/profiling.md`): `buildProfile debug`, PAL interlaced, Live Link / Live
Debugger / Live Logic / Remote Pad / Time Machine **all off**, the 20 s tour
followed by the parked camera. Samples are the per-frame `FTRAW` ticks of frames
**550–1611**, inside the parked region in both arms. **Two runs per arm, all four
cross-pairings.** Sign: **d = work(off) − work(on)**, so d > 0 means BLSS made
the frame shorter.

**This is the configuration the example ships** — `blssJitter` **off** in both
arms of the BLSS-on row:

| arm | mean | median | p95 | max | FPS |
|---|---|---|---|---|---|
| BLSS **off** | **52.95 ms** | 53.11 | 54.52 | 55.38 | **18.9** |
| BLSS **on**, jitter **off** | **32.42 ms** | 32.36 | 33.07 | 33.94 | **30.8** |

**d = +20.53 ms, 95 % CI [+20.46, +20.61], sd 1.12, n = 1024 paired frames** per
pairing (2 048 pooled per arm); the four cross-pairings span **0.010 ms** against
an effect of 20.5. **A 1.63x speedup, on a scene you can actually watch** — on
the geometry of 2026-08-09, per the box above.

## What the asset swap moved

The example's buildings and its animated model were replaced with CC0 assets on
2026-08-09. **The point of the swap was licensing; the point of measuring it was
that the swap touches 100 % of what the network is fitted on while touching ~1.4 %
of the frame.** The corpus renderer draws *no emitters at all* — it sees only
geometry — so changing the buildings changes the entire training corpus, while the
haze that is 98.7 % of the fill never moves. Both halves had to be checked, and
neither could be assumed from the other.

**GS fill — unchanged.** `--blss-coverage`, the same instrument on both scenes:

| | geometry | emitters | total | p95 |
|---|---|---|---|---|
| cottages + spiders | 0.98 | 71.65 | **72.63** | 94.39 |
| depot blocks + wobblers | 0.96 | 71.27 | **72.23** | 93.32 |

The emitter term moves by 0.5 %, which is the haze being sampled against slightly
different occluders, not the haze changing. The emitter setup — 6 banks x 32
custom billboards at `size 9.0`, plus the campfires and rain — is byte-identical.

**Reconstruction quality — slightly better.** `--blss-eval <this folder>
--all-shots`, jitter off, 2x2:

| | oracle ceiling | trained | fraction captured | strongest channel correlation |
|---|---|---|---|---|
| cottages + spiders | +1.058 dB | +0.51 dB | 48 % | 0.293 |
| depot blocks + wobblers | **+1.108 dB** | +0.50 dB | 46 % | **0.358** |

The ceiling did **not** collapse — it rose. Under leave-one-shot-out
cross-validation the new corpus reads **+0.39 dB, sd 0.36, 0 of 6 folds below
plain bilinear**. `--features` says why it survived: `texDetail` has mean 0.312
with only 3.6 % of tiles pinned at 1, and it is now the channel most correlated
with the temporal weight (+0.356) — Kenney's 64² `map_Kd`s carry the texture
signal the outgoing 512² cottage diffuse used to.

**EE — about 4 ms a frame cheaper, and this is the part that moves the headline.**
PCSX2, software renderer, the shipped build, the same deterministic 20 s tour
sampled once a second (`-Watch`), old build and new build. **PCSX2 is admissible
for EE aggregate and counts and for nothing else here.**

| | `SCENE` mean | `SCENE` peak | `PART` | `FRAME` |
|---|---|---|---|---|
| cottages + spiders | 11.53 ms | 16.82 ms | 0.46 ms | 19.8–40.0 (four frames at 25 FPS) |
| depot blocks + wobblers | **7.53 ms** | **10.16 ms** | 0.46 ms | 19.6–20.5 (never drops) |

`PART` — the EE particle phase — is **0.46 ms in every sample of both runs**,
which is the emitter setup being genuinely untouched. The 4 ms is geometry and
skinning: 11 650 → 4 960 triangles, and 2 x 1 092 animated vertices → 2 x 123.
The animated model is the bulk of it at roughly 2 ms of EE per 1 000 vertices.

The buildings cost *more* draw calls than the outgoing cottages did — 5 material parts
each against 1, because Kenney's UVs tile and so the textures cannot be atlased
(`textureAtlas` skips anything whose UVs leave [0,1]) — and the frame still came
out ahead. That is the honest shape of the trade: submits are cheap here,
skinning is not.

> **The jitter-ON timing, kept because it is what this file quoted for a week.**
> Same fixture, same protocol, same window, jitter on:
>
> | arm | mean | median | p95 | max | FPS |
> |---|---|---|---|---|---|
> | BLSS **off** | 52.86 ms | 53.01 | 54.44 | 55.37 | 18.9 |
> | BLSS **on**, jitter **on** | 32.98 ms | 32.93 | 33.62 | 34.37 | 30.3 |
>
> **d = +19.88 ms, 95 % CI [+19.81, +19.95], 1.60x.**
>
> The re-run against the shipped jitter-off build was owed for a day and has
> **landed** (2026-08-09, real hardware) — it is the table above. It had failed
> twice first, both times because the console dropped off the LAN mid-session,
> and no number was invented to fill the gap in the meantime.
>
> **The prediction held and was slightly beaten.** It was that the timing would
> not move, because the jitter changes *where* the half-res raster samples rather
> than how much of it there is. The off arm is unchanged (52.95 vs 52.86, inside
> run-to-run drift) and the BLSS arm came out **0.56 ms faster**; the console's
> own `BLSSFILL` line reads **`passes = 1.56`** on the shipped build, the same
> figure the jittered build logged, which is the substance of the claim — the
> retrained net did not start asking for more fill. The extra 0.56 ms is the
> jitter arithmetic itself coming out of the raster setup.
>
> The rig is one line: `TYRA_FRAME_PROFILE 1` in
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

- **BLSS keeps about a quarter of the fill.** `blssScale 0` is `Scale::X2Y2` —
  half in *each* axis, so a quarter of the pixels. Not half; `docs/profiling.md`
  said half for a week and the break-even it computed was ~70 % too pessimistic.
  This two-point ratio gave **25.9 %**, and it is biased toward 1 because the
  particles' EE cost sits in both arms; the five-point fit that removes that term
  (below) measures **24.5 %**, i.e. a saved fraction of **0.7548**.
- **The scene's non-haze floor is 21.2 ms off / 24.8 ms on.** That is why the
  demo lands at 19 → 31 FPS and not higher: with BLSS on this scene cannot beat
  ~40 FPS however thin the haze gets, and **thinning the haze shrinks the win**,
  because the haze is the only thing BLSS is paid to remove. 6 x 32 is the
  compromise: both arms watchable, the win still 1.63x, and the scene still
  **58.7 blended-pass equivalents** of fill — comfortably above the
  **13.1-coverage** break-even at this project's raster.

### Five load points, and the speed model fitted on them

The two-point model above became a five-point fit on 2026-08-09
(`docs/profiling.md`). A frame-indexed object script steps the six haze banks by
index so segment *k* of each arm shows the same load, plus one segment that
leaves all 192 particles simulated and submitted at `emitSize 0.05` — every
particle still costing EE, no raster area at all. That last segment is what
**separates the emitters' EE cost from their fill**, which a slope ratio cannot
do:

> **saved(ms) = 0.7548 × (scene fill, ms) − 5.10**, residual RMS **0.093 ms**
> over a 0.7–34 ms fill range.

Two independent confirmations fell out of it. The retention term was right (the
shipped 0.741 was 2 % conservative), and the fit's **intercept, 5.10 ms,
reproduces the independently-counted 4.60 EE + 0.50 composite fill to two
decimals** — two instruments arriving at one number. What is *not* confirmed is
the coverage estimate that feeds the model; see the top of this file.
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

The `passes` column there is the **host** figure, averaged over the training
corpus. It is not the console's: the game's own `BLSSFILL` line reads
**`passes = 1.56`** on the shipped build. Quote 1.56 for what the PlayStation 2
draws and 1.76 only as what `--blss-eval` scores over the corpus — a "1.76" has
already circulated as the console number and it is not one.

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
| `proxy` — the bag-proxy feed, inside scene submission | **2.34** |
| `net` — the MLP | 0.78 |
| `pkt` — the grid packet build | 0.50 |
| `reproj` | 0.28 |
| `feat` | 0.19 |
| `beginScene` / `endScene` | 0.41 / 0.10 |
| **total** | **4.60** |

…plus **0.50 ms** of composite fill on the GS, measured in the same runs. That is
the whole bill, and both halves are counted rather than inferred.

`net` was **1.93 ms** until the activation table was enabled on both twins; the
emulator predicted that cut would be worth 2.11 ms and it is worth **1.14** here.
The bill was **5.02 ms** until `addBag`'s four `floorf` calls and `emitGrid`'s
two per grid corner became an inline floor-to-int — bit-identical for every
finite argument in int range, and worth **+0.227 ms** [+0.225, +0.229] priced on
hardware against its own switch. **`proxy` is more than half the bill**, which is
the opposite of what PCSX2 said (it ranks `net` first) and the reason this table
is a console table.

A second cut, **the proxy budget, is measured and deliberately not shipped**: it
takes the bill to **4.17 ms** (+0.429 [+0.427, +0.430], all of it in `proxy`) and
break-even from 11.5 to 10.5 coverages, for one channel of description
(`coverage` 0.631 → 0.638, every other channel identical to three decimals). It
stays at 0 because `src/blsscorpus.cpp` does not cut the same way yet, and
flipping one half of a twin silently invalidates every trained net.

## Two defects this scene found — one fixed, one switched off

**1. ~~With BLSS on, every textured primitive and the textured terrain
disappear.~~ FIXED — the z mask was never on** (see
`docs/blss-reconstruction.md` §6). Re-checked on the re-tuned scene in PCSX2's
software renderer, BLSS on, from the parked vantage: both blocks, their window
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
buildings floating over an empty sky.

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
the buildings, the primitives, the terrain and the animated models, and then run on a
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
tyrax-editor --blss-coverage <this folder>   # how much fill it asks the GS for
tyrax-editor --build       <this folder> --run
```

The **speed** half is `tyrax-editor --blss-coverage <this folder>`, the headless
twin of the window's **Will the frame get faster?** button — same
`blss::measureCoverage`, same verdict arithmetic, so there is no second answer to
keep honest. It exists because the round that *measured* the speed model could
not re-derive the estimator's own figure: the estimator was a button in a GUI,
and a number nobody can re-run is a number nobody can check. Both halves produce
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

Every art asset here is **CC0 1.0** and attributed in
`THIRD-PARTY-NOTICES.txt`. Total committed assets under `res/`: **758 KB** (it
was 1.7 MB before the swap), of which the buildings and trucks are **217 KB** of
Wavefront text and **45 KB** of 64² and 128² textures, and the animated model is
**14 KB**. Nothing here is over 512 px or non-power-of-two, so `texbake` resizes
nothing on the way to the console.

### How the buildings were made

`block-west.obj`, `block-east.obj` and `trucks.obj` are **kit-bashes of Kenney's
Retro Urban Kit** (CC0 1.0, `www.kenney.nl`) — derivative works, which CC0 permits
without condition. The recipe is written down in `THIRD-PARTY-NOTICES.txt` so the
result is reproducible rather than mysterious, and three decisions in it are worth
keeping if you re-cut them:

- **Merged, not placed.** A model renders one bag submit *per material part* and
  is never static-batched (`staticBatchEligible` takes primitives only), so
  thirty kit modules dropped in as thirty scene objects would be thirty draw
  calls. They are merged into one `.obj` per building instead, with faces grouped
  by material — 5 parts each — and the three trucks into a single one-material
  file.
- **Kenney's OBJ export writes every face twice.** Dropping the duplicate halves
  the triangle count for nothing.
- **Interior faces are culled on the grid.** The modules are 1x1x1 cells, so a
  face with another module behind it can never be seen and is dropped at author
  time. That is what keeps a solid, articulated mass down to ~1 100 triangles.

**Windows must be stamped onto cells that are actually exposed**, not authored by
hand into the plan. A `wall-a-window` module's window quad has normal `(0, 0, -1)`
— an axis direction — so the interior-face cull deletes it exactly like a wall
face when the cell behind it is occupied. Hand-authored window rows landed inside
the mass, the cull removed all of them, and the building came out blank with no
error anywhere. The generator now finds the exposed cells itself and rotates each
module's front toward open air.

**Why the textures are not atlased.** `ProjectSettings::textureAtlas` would pack
these 64² `map_Kd`s into shared pages and cut the part count, and it correctly
refuses to: every model in the kit **tiles** its UVs (they run to u = −46), and an
atlas page can only hold textures sampled inside [0, 1]. That is why a building
costs 5 submits rather than 1, and it is a property of the source art, not a
setting anyone can flip.

### The animated model

`wobbler.glb` — already shipped by five other examples, so it adds no new licence
surface at all. **Quaternius' Universal Base Characters [Standard] was evaluated
first and rejected on evidence**: its free tier ships a rigged mesh (a skin and a
130-bone skeleton) but **zero animation clips** — the glTF has no `animations`
array and the FBX has no `AnimationStack` and no `AnimationCurve`. The rigged,
animated sources are in the paid SOURCE version. A rig with no clips renders as a
statue, and faking one was not on the table. `THIRD-PARTY-NOTICES.txt` records
this so nobody re-derives it.

**Static geometry wants `.obj`, never `.fbx`.** `.fbx` is *always* treated as an
animated model (`project.hpp:797`, `isAnimatedModelPath`), so a building shipped
as one would go down the skeletal pipeline, pay per-frame EE pose work for
something that never moves, and — until animated models were added to the corpus
walk — be invisible to BLSS training. The buildings are `.obj` for that reason.
