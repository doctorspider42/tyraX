# BLSS — a neural upscaler for the PlayStation 2, trained on the host

Nineteen commits. The 3D scene renders into a **half-resolution GS target**; a
small neural network, trained on the host and baked into the game as 123 floats,
decides per **32×32 screen tile, every frame**, how that image is blown up to the
display buffer and how much of the previous frame to reuse. Off by default,
proof of concept.

It is not a super-resolution network. Nothing on a 147 MHz rasteriser pushes
327 680 pixels through a net at 50 Hz. This is the *other* half of DLSS — the
part that picks and blends reconstruction kernels — and the network never touches
a pixel: its output rides into the frame as 255 vertex colours.

Full guide: [`docs/neural-upscaler.md`](../docs/neural-upscaler.md). The exact
arithmetic, which is the contract between the host trainer and the console, is
[`docs/blss-reconstruction.md`](../docs/blss-reconstruction.md).

---

## Why it can work on this hardware at all

Three properties of the GS line up. None of them is a trick we invented; all
three are in the register file already.

1. **`XYOFFSET` is 12.4 fixed point.** The GS addresses the raster in sixteenths
   of a pixel, so the whole frame can be jittered by an exact sub-pixel offset
   for free — no projection-matrix change, no per-vertex work. Two phases at
   −4/16 and +4/16 of a low-res pixel are two of the four output-pixel centres
   inside one low-res pixel: a quincunx pair, exact rather than approximate.
   Two phases and not a longer Halton sequence, because the history is one frame
   deep.
2. **The blend unit's `(A−B)·C ≫ 7 + D` takes `C` from per-vertex alpha.** Draw a
   Gouraud-shaded grid instead of a sprite and the blend factor becomes a smooth
   spatial field the rasteriser interpolates for nothing. That is exactly the
   shape of the network's output — "how much of this kernel, here" — so the
   weights never have to be uploaded as a texture.
3. **Double buffering already keeps a full-resolution history.** The other
   display buffer holds the previously presented frame, finished and composited,
   sitting in VRAM whether you use it or not. The temporal pass costs **zero**
   extra memory, and because the frame it reuses was itself composited, a still
   camera keeps accumulating instead of stopping at two samples.

The six input features all come from what the EE already holds while it is
submitting bags — motion, depth, depth gradient, geometric edge density, texel
density, coverage — so **no frame is ever read back** and nothing stalls the
pipeline.

---

## What is measured, and the honest framing

### Fit the project you will ship, and ship that net

This is the only sentence in the PR with a decision in it, and it was the last
thing the branch learned.

Measured on `examples/procedural` (39 meshes, 15 098 triangles, no textures at
all), 72 frames over six camera moves, frame-weighted over the whole corpus:

| | margin over plain bilinear | mean full-screen passes |
|---|---|---|
| **bestiary-trained** net | **−0.40 dB** | 1.72 |
| **project-trained** net (`--all-shots`) | **+0.06 dB** | 1.19 |
| oracle — the ceiling for this scene | **+0.77 dB** | 1.20 |

A net fitted to the built-in procedural corpus is **worse than doing nothing** on
a real project, by four tenths of a decibel, while paying half a pass more for
the privilege. The mechanism is in `--blss-eval --features` and is not subtle:
`texDetail` is identically zero over all 16 128 tiles of that untextured project
and is the bestiary's channel most correlated with the temporal weight;
`edgeDens`, the next one, saturates at 1.0 in 63 % of project tiles against 29 %
of bestiary tiles. The bestiary net's temporal gate is driven by inputs that are
out of range, so it asks for 62–93 % temporal occupancy where the oracle asks for
7–30 %.

`--blss-train <projectDir>` builds the corpus from the project's own scenes —
primitives, static `.obj` and terrain chunks, walked / panned / orbited /
whipped / pitched / strafed from the scene's bounds and its player start, plus
any authored Cutscene Director camera track. Animated `.glb` is skipped *because*
it goes down the dynamic pipeline, which does not feed BLSS at all.

### A project corpus does not generalise across camera moves

And that is fine, as long as nobody quotes it. Leave-one-shot-out on a project
corpus holds out a *camera move*, not a *kind of content*, so it asks whether
walks and orbits predict a strafe. On `examples/procedural` the answer is no:
**−0.17 dB, 9 of 18 fold-runs below bilinear.** It does not matter for the net
you ship, because the console runs the frames the net was fitted on — which is
why `--all-shots` is the shipping configuration and now the default.

### Some scenes have no headroom at all

The **oracle** row of `--blss-eval` is the scene's own ceiling: the best any
per-tile weighting can reach under the exact GS composite, which no network can
beat. On `examples/showcase` that ceiling is **+0.02 dB at 1.00 passes**, frame-weighted
over 156 frames — soft ground texture, low-poly props, nothing that aliases. There is nothing to
reconstruct there and no amount of training changes it. `--blss-eval <projectDir>`
is how you find that out before spending an afternoon, and the window's Evaluate
tab now says it in one line above the table instead of burying it in the sixth
row.

### The bestiary number, labelled as such

On the built-in 13-shot corpus, under leave-one-shot-out cross-validation with 3
independent corpora — 39 fold-runs, 156 frames, shipped defaults:

**+0.42 dB over plain bilinear**, sd 0.35, **3 of 39 folds below bilinear**,
**1.80 mean full-screen passes** (sd 0.30) against 1.00 for bilinear. The
conservative reading is **+0.26 dB**, over the six shots that were added *after*
the defaults were chosen and took no part in choosing them.

That is a statement about the bestiary. It is not a promise about anyone's game,
and this branch spent its last three commits removing places where it was being
read as one.

### Use `--cv`, never a single split

A single held-out split is a sample of size one, and this feature quoted one five
times before anybody checked. The ±0.4 dB it kept blaming on the training seed
was **split-selection** variance: fold to fold the sd is 0.35 dB, while the sd of
the per-seed fold *mean* is 0.04 dB. Same net, same code — only the question
changed.

---

## What it costs and what it returns

**VRAM: it gives back more than it takes.** The z buffer follows the *raster*
now, not the display buffer, so it shrinks with the reduced render. At 512×448
output from a 256×224 raster, in words:

| region | baseline | BLSS |
|---|---|---|
| display buffers × 2 | 458 752 | 458 752 |
| z buffer | 229 376 | 57 344 |
| low-res colour target | — | 57 344 |
| history buffer | — | 0 (the other display buffer) |
| **total** | **688 128** | **573 440** |

**114 688 words — 448 KB — returned.** Measured earlier on this branch, on a
booted `fpp` fixture in PCSX2's software renderer at `Pal576i`, from the game's
own `VRAMSTAT` line: texture VRAM free went **0.227 MB with BLSS off → 0.727 MB
with it on**, the one texture eviction disappeared, and the largest free block
went 232 KB → 744 KB (which is the number that decides whether a 256×256 32-bit
texture can be placed at all — it wants 264 KB with padding). That is the only
display mode that has been booted since the z shrink; no other has.

What makes the shrink *safe* is one invariant, and it is the thing to keep if you
touch any of this: **`zBuffer.mask` is 0 only inside the low-res bracket**. Every
`draw_enable_tests` in the engine reads that field, so the 2D/HUD/post-fx half of
the frame — which draws full-screen sprites and would otherwise stamp 448 rows at
a 512 stride — cannot reach past the smaller allocation.

**Fill: 1.00 is plain bilinear, 5.00 is every kernel everywhere.** The composite
is up to five full-screen Gouraud-grid passes, and a grid cell whose weight
rounds to zero is not drawn — so the network's own confidence sets the frame's
cost. The shipped net measures **1.67 passes in distribution, 1.87 out of it,
1.80 ± 0.30 across 39 cross-validation fold-runs**, and **1.96** in the one
console frame that has ever been instrumented. The oracle reaches better quality
at 1.36, so roughly half a pass of what remains is the network failing to
generalise the cost model.

That is a knob only because the **objective charges for it** — as a step on the
quantised alpha byte, because the byte is what the engine's skip test reads. A
smooth penalty would park the oracle at alpha 1, which costs a whole pass and
buys nothing.

**The build refuses conflicting configurations.** BLSS cannot be combined with
depth of field, portals or split-screen: all three want real GS depth at display
resolution, which since the z shrink is not merely unwritten but unallocated.
`blssClashes()` + `blssInterlock()` put one `#error` headline and one `#error`
per clashing feature into the generated `inc/scene_data.hpp`, **naming the
feature, the scene it is in and what to do about it** — so nothing that produces
an ELF gets past it: not the editor's build button, not `docker compose` + `make`
by hand, not CI. `generate()` prints the same lines on the host, so
`--refresh-gen` reports it and `--build` says it before Docker starts. Checked
here on a scratch `fpp` project with the upscaler on and a DoF amount of 0.5:
`--refresh-gen` printed `[blss] BUILD WILL BE REFUSED: BLSS x DEPTH OF FIELD:
scene main …` and put the matching pair of `#error` lines into
`inc/scene_data.hpp`.

It **refuses rather than auto-disabling**. A build that quietly measured a
different configuration than the project describes is the worst outcome available
for a feature whose entire point is being measured. Each condition mirrors what
the generated game will actually do — per-scene resolved DoF quantised the way
`POSTFX_DOFS` is and gated on a non-zero focus distance, the `Set Depth Of Field`
flow node (which raises DoF at runtime in a project whose authored amount is zero
everywhere), a portal only when its target resolves to another `Portal` in the
same scene, split-screen only when a scene has a second `Player` object — so it
refuses no project that would have worked.

Reflections, camera feeds and projected shadows used to be silently broken by it.
They nest correctly now.

**Training costs 18 seconds.** `--threads N` bounds the corpus render and the
oracle; on 6 cores a 156-frame, 400-epoch, `--all-shots` fit goes **68.4 s at
`--threads 1` → 18.5 s at every core**. It is a wall-clock knob and nothing else:
the same seed writes a **byte-identical `blss.net` at any thread count, including
matching the binary from before the corpus was parallelised**. That last equality
is what keeps every fold table in this PR a measurement of the code that is
actually here.

---

## What is NOT verified

Read this section before believing anything in the one above it.

- **No BLSS frame has ever been timed.** Not in the emulator, not on hardware. No
  profiling pass exists. Every performance statement in this PR — including all
  the pass counts — is **fill arithmetic and host measurement**, never a
  stopwatch. Occupancy is a count of grid cells, not a millisecond.
- **No physical PlayStation 2 has ever run this.** Everything on-console was
  observed in PCSX2's software renderer.
- **Nobody has watched the picture since the objective was retuned.** The last
  time a human watched it in an emulator, **the picture visibly oscillated** — a
  stationary sub-pixel bob, which was traced to the jitter and not to
  interlacing. The objective has since gained a fill term that culls exactly the
  two passes which alternate with the jitter, and the host's flicker metric
  improved with it, but **the oscillation is neither confirmed fixed nor
  confirmed present**. That is a twenty-minute PCSX2 boot and it gates the
  feature.
- **The editor window's layout has never been seen.** An earlier state of it was
  driven with `--ui-script` and screenshotted; everything added in the last
  commit — the header corpus switch, the Evaluate verdict block, the
  cross-validation caveat, the three-entry Debug view combo — was written on a
  machine whose compositor was dead. The *arithmetic* behind those was checked
  from a host-only harness against captured runs (all three verdict branches, and
  that the progress bar never runs backwards); that is a different claim from
  "somebody looked at it". The finished cross-validation table and the error
  banner have also never been seen.
- **The sky-dome proxy opt-out has never been booted**, only compiled. The
  console feature-spread numbers quoted anywhere predate it.
- **The `--standardise` and `--flicker-weight` tables were measured at 8 input
  channels** and have not been re-run since the vector shrank to six. What is
  claimed from them is the *direction*, which two independent corpora agree on.

---

## The thread this branch is really about

Count them: **nine or ten times on this branch, something a document or a code
comment described did not exist in the code, or was measured on the wrong
thing.** Not one root cause — the same shape of mistake, one level further up
each time.

1. **Per-frame PSNR could not see flicker.** It is a still-image metric, and it
   did not merely miss the oscillation — it *rewarded* it, because the average of
   two jitter phases really is closer to the supersampled truth than either
   phase.
2. **The on-screen sampler could not see it either.** It sampled at 0.8 s, which
   at 50 Hz is 40 frames — an even number, so every sample landed on the same
   jitter phase and the picture read as perfectly still to the tool while it
   bobbed on the television.
3. **Flicker got measured, but only reported.** `--blss-eval` grew the column and
   the oracle went on scoring single-frame PSNR alone, so the labels carried no
   notion of stability and asking for history stayed free.
4. **Nothing ever charged for fill.** Sparsity is the entire performance case for
   this feature, and the objective was blind to it — so the network asked for
   every kernel everywhere and the composite degenerated to five full-screen
   passes.
5. **The measurement itself was a sample of size one, and nothing said so.**
   Every out-of-distribution decibel this feature ever quoted came from a single
   held-out split chosen once and never varied. The honest-sounding retraction
   that came out of it — "≈+0.1 dB, statistically a draw" — was printed in the
   README, the preferences dialog, the engine skill and the backlog, and it
   **understated a win that measures +0.42 dB**. It also produced a fake "sharp
   knee at 6" in the fill-weight sweep (the real shape is a plateau from 12 to
   24) and a fake 0.22 dB price on the flicker term (it costs 0.02 dB and buys
   nothing).
6. **`histAge` was designed as the recurrent channel and measured actively
   harmful.** The fold that gained most from deleting it is the one that indicts
   it: `foliage static`, the shot with the *highest* `histAge` in the corpus. The
   channel was hurting hardest exactly where it existed to help.
7. **`luma` was a channel the EE cannot produce.** `stapip_core` can fill a bag's
   brightness only when the bag has a single colour, so every per-vertex-lit mesh
   — which is every static mesh a generated game submits — read a constant 0.5 on
   the console while the corpus spread it over 0–0.48. Fitted on a photometric
   feature, run on a constant, and the constant was **outside the corpus' range**.
8. **The build interlock was documented in three places and never implemented.**
   Nothing in codegen gated depth of field, portals or split-screen on
   `blssEnabled`. For three commits the preferences warning was the whole guard,
   and a user who clicked past it got an ELF that compiled, booted and drew the
   wrong picture.
9. **The "minimal fix" the docs prescribed could not have worked.** Both this
   page and the backlog said to make `getCurrentFrameBuffer()` return the BLSS
   target while the bracket is open. Those `end()` implementations restore
   **four** registers and only `FRAME` comes from that accessor — so it would have
   left a 512-wide scissor over a 256-wide `FRAME` and a raster window centred on
   the wrong window, with the frame's jitter silently dropped. A differently
   broken frame, arrived at by a change that looks like it cannot be wrong.
10. **The corpus described a machine the console is not.** `StaPipCore` handed
    BLSS a bag's bounding *sphere*, so `wNear = w − radius` collapsed to the near
    clamp and a generated game's entire frame was described by **two** proxies,
    with `depth`, `depthGrad` and `coverage` pinned at 1.0 in every tile and the
    composite paying 5.00 of a possible 5.00 passes. None of that is visible in a
    host number, because the host's own frames were fine. It ran that way for
    eleven commits.

Three more of the same shape, smaller: the emitter's documented "a missing net is
never a build failure" path **had never once been executed** (`%.9g` renders
`0.0f` as `"0"`, and `0F` is not a float literal, so every BLSS project without a
trained network failed to compile); the editor's Debug view combo offered two
entries against a field the loader clamps to three, so the one instrument that
says what the console's network sees was unreachable without a text editor and a
project that had it on displayed as "Off"; and an engine comment claimed 147
weights for a net that has had 123 since two channels were deleted.

**The tooling that came out of this is the durable part**, and it is the reason
to take the numbers above more seriously than the numbers this feature published
earlier:

| | what it answers |
|---|---|
| `--blss-eval --cv` | leave-one-shot-out cross-validation. The only out-of-distribution number worth acting on |
| `--blss-eval --features` | what each input channel looks like over the corpus, and how much of it sits against a clamp |
| `--blss-eval --probe "<line>"` | where a **console** feature vector sits inside the training distribution — spread, percentile, support within ±0.05, and a verdict per channel |
| `--blss-eval --drop-feature <name>` | holds a channel at zero everywhere, which is what deleting it would do. Only means anything **with a control** |
| the occupancy columns | what fraction of grid cells each pass actually draws, through the engine's own skip rule |
| the flicker column | mean per-pixel change between consecutive frames, because PSNR is structurally blind to it |
| engine **debug view 2** | `BLSSGRID` / `BLSSWORST` / `BLSSFEAT` / `BLSSOUT` / `BLSSFILL` into the game's `bin/log.txt`, once a second — the console's own answer, paired with `--probe` |
| `--threads 1` vs every core | determinism, checked rather than asserted: the two `blss.net` files must be byte-identical |

Two rules fell out of it that are worth more than the feature:

- **An instrument only one side of a twin can run is not an instrument.** The
  corpus could always describe its own distribution; the console could describe
  nothing. Both halves ship permanently now.
- **A negative result needs a control or it is not a result.** Both channel
  deletions were 0.02–0.03 dB, which means nothing until something known-useful
  has been dropped for comparison. `edgeDens` is that control.

---

## Reviewing it

```bash
./build.sh

# fit the project, which is the configuration that ships
build/tyrax-editor --blss-train examples/procedural --all-shots -o /tmp/proj.net

# is there anything to win on this scene at all?
build/tyrax-editor --blss-eval examples/procedural -i /tmp/proj.net

# the out-of-distribution number on the built-in corpus (3m38s on 6 cores)
build/tyrax-editor --blss-eval --cv --cv-seeds 3 --assets examples

# determinism: these two must be byte-identical
build/tyrax-editor --blss-train examples/procedural --all-shots --threads 1 -o /tmp/a.net
build/tyrax-editor --blss-train examples/procedural --all-shots            -o /tmp/b.net
md5sum /tmp/a.net /tmp/b.net
```

### Where the numbers in this PR came from

Re-run against the tip of this branch, on a 6-core box, and reproduced:

- the project-vs-bestiary table (−0.40 / +0.06 / +0.77 dB at 1.72 / 1.19 / 1.20
  passes) — `--blss-eval examples/procedural --frames 72`, frame-weighted over
  both splits the way the window's verdict is;
- the `examples/showcase` ceiling (+0.02 dB, 1.00 passes);
- the whole 39-fold cross-validation table — every fold mean, every per-seed
  column, +0.42, sd 0.35, 3 of 39, 1.80 passes, per-seed fold-mean sd 0.04;
- the project-corpus cross-validation (−0.17 dB, 9 of 18 below bilinear, 1.22
  passes);
- the fixed-kernel tables (in-distribution 27.81 against bilinear's 27.11 at 1.67
  passes; held-out 24.49 against 24.32 at 1.87, oracle 25.19 at 1.36);
- `texDetail` at **100.0 % zero** over all 16 128 tiles of `examples/procedural`,
  `edgeDens` saturated in 63.4 % of them;
- the threading table and all four determinism md5s, including a build of
  `7d3dbf67^` in its own worktree;
- the build interlock, on a scratch project.

**Not re-run, and taken from earlier commits on this branch:** every console
number (the VRAM residency table, the 2 → 41 proxies and 5.00 → 1.96 passes from
debug view 2, the on-screen stability figures) — this box has no working display
and PCSX2 cannot open a GS window here. The `--standardise` and `--flicker-weight`
sweeps were measured at eight input channels and have not been re-run since.

Everything above is also in the editor, under *Tools ▸ Neural Upscaler (BLSS)*,
which runs `tyrax-editor --blss-<verb>` as a subprocess and parses its stdout —
deliberately, so there is no second implementation of what `--blss-eval`
measures, and so the tool's raw output can sit on screen under every table. A
parsed number you cannot falsify is a number this feature has already been burned
by.
