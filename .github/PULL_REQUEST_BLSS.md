# BLSS — a neural upscaler for the PlayStation 2, trained on the host

The 3D scene renders into a **half-resolution GS target**, and a small network —
fitted on the host, baked into the game as **123 floats** — decides per **32×32
screen tile, every frame**, which reconstruction kernels blow that image back up
to the display buffer and how much of the previous frame to reuse. The network
never touches a pixel: its output rides into the frame as **255 vertex colours**
on a Gouraud-shaded grid, so the GS's own blend unit does the work and a 147 MHz
rasteriser can afford the whole thing.

It ships **off by default**, and that is the honest default rather than a timid
one: this is a large win above a break-even amount of overdraw and a straight
loss below it, and only your scene knows which side it is on. The branch's real
deliverable is the pair of instruments that tell you which — `--blss-eval` for
the picture and `--blss-coverage` for the frame time.

Full guide: [`docs/neural-upscaler.md`](../docs/neural-upscaler.md). The exact
arithmetic, which is the contract between the host trainer and the console, is
[`docs/blss-reconstruction.md`](../docs/blss-reconstruction.md). Every timing
claim below traces to [`docs/profiling.md`](../docs/profiling.md).

---

## The measured result, and its regime

Both arms on a **real PlayStation 2** over ps2link, paired per-frame `FTRAW`
samples, camera parked so frame *k* of one arm shows what frame *k* of the other
does, two runs per arm and every cross-pairing. Sign: **d = work(off) −
work(on)**, so d > 0 means BLSS made the frame shorter.

| fixture | overdraw | BLSS off | BLSS on | d |
|---|---|---|---|---|
| `examples/upscaler-lab` — the haze demo [^geom] | heavy | **52.95 ms** | **32.42 ms** | **+20.53 ms — 1.63×** |
| `blssrig` — terrain + six slabs | a handful of coverages | 9.42 ms | 19.25 ms | **−9.83 ms** |

The win row: 95 % CI **[+20.46, +20.61]**, sd 1.12, **n = 1024 paired frames per
pairing** (2 048 pooled per arm); the four cross-pairings span **0.010 ms**
against an effect of 20.5. This is the shipped configuration — `blssJitter`
**off**. The same fixture with the jitter **on** measured **52.86 → 32.98 ms,
+19.88 ms, 1.60×**; both tables are kept and labelled, because the jitter-off
re-run was a published prediction for a day before it was a measurement, and it
held (the off arm did not move, the BLSS arm came out 0.56 ms faster, and
`BLSSFILL` reads **`passes = 1.56`** in both — the retrained net did not start
asking for more fill).

The loss row was measured with the **pre-cut engine**, whose BLSS bill was
7.92 ms of EE against today's 4.60. The loss is smaller now and it is still a
loss: that fixture has nothing to trade.

### The line between them

**Break-even is 11.5 full-screen coverages.** Derived, not written down, from

> `0.7548 × 0.5872 × C > 4.60 + 0.50`

- **0.7548** — the fraction of the scene's fill BLSS removes. `blssScale 0` is
  `Scale::X2Y2`, half in *each* axis, so a quarter of the pixels survive.
  **Fitted on hardware over five load points**, not assumed from the geometry.
- **0.5872 ms** — one full-screen alpha-blended textured pass, **measured** on
  the console with a calibration sweep. The GS data sheet predicts ~194 µs; the
  measured number is the one that counts.
- **4.60 ms** — BLSS' EE bill on hardware, counted term by term: `proxy` 2.34 +
  `reproj` 0.28 + `feat` 0.19 + `net` 0.78 + `pkt` 0.50 + `begin` 0.41 +
  `end` 0.10.
- **0.50 ms** — the fill the composite itself adds back, measured in the same
  runs.

With the **proxy budget** on it would be **10.5** coverages. That switch ships
**off**; see the costs section.

### The speed model, and the confirmation that fell out of it

The editor predicts a project's speedup before you build anything. That model
used to be a guess with an anchor; it is now a fit:

> **saved(ms) = 0.7548 × (scene fill, ms) − 5.10**, residual **RMS 0.093 ms**,
> five load points over a 0.7–34 ms fill range.

Two independent confirmations, and the second one is why this is worth quoting.
The retention term that had been *assumed* at 0.741 measures **0.7548** — the
shipped model was 2 % conservative. And **the fit's intercept, 5.10 ms,
reproduces the independently-counted 4.60 EE + 0.50 composite fill to two
decimals.** Two instruments, one number.

### VRAM: it gives back more than it takes

The z buffer follows the **raster** now, not the display buffer, so it shrinks
with the reduced render. Net words returned = `outW · outH · (1 − 2/A)` for area
divisor `A`:

| scale | area | VRAM returned at 512×448 |
|---|---|---|
| **1×2** | 1/2 | **exactly zero** — the low-res target costs precisely what the z buffer saves |
| **2×2** (shipped) | 1/4 | **448 KB** |
| **4×4** | 1/16 | **784 KB** |

So `1×2` is a choice about fill and picture and **never** about memory. `4×4` is
for exactly one situation — a project whose textures do not fit at all — because
it costs about 2.6 dB, nearly eight times the trained margin.

---

## What shipped

- **`blssJitter` defaults to `false`.** The ±¼-pixel raster jitter is what makes
  the two half-res phases a real quincunx pair, and it is also **a visibly
  shaking picture**: three builds differing in nothing else were handed to a
  person, who called them steady / **"like an earthquake"** / steady, for
  BLSS-off / jitter-on / jitter-off. It is **not fixed, it is switched off**, and
  turning it off costs about 40 % of the reconstruction ceiling. Project format
  **v6** carries the key, and it is the one deliberate exception to "an older
  file opens byte-identical" — because the behaviour it declines to preserve is
  the shake.
- **A default net ships with the editor** (`resources/blss-default.net`,
  embedded), fitted on the **union** of seven real example projects **and** the
  synthetic bestiary. Leave-one-**project**-out over those seven (`--cv-groups`,
  3 seeds × 6 shots = 18 fold-runs each) says a net that has **never seen**
  `upscaler-lab` scores **+0.29 ± 0.37 dB** there against that project's own
  held-out net's **+0.31 ± 0.34** — a tie. Either half of the corpus alone
  fails, in two different ways: the bestiary alone is a lottery (**−0.34 dB**
  mean, **−1.09** worst), and real projects alone degenerate to plain bilinear.
- **The training-shot plan** (`Project::blssShots`, format v6) — which of the six
  automatic camera moves the corpus shoots, their frame counts, whether Cutscene
  Director takes join, and the author's own vantages. **A default plan writes
  nothing at all**, so every project saved before the key existed round-trips
  byte-identically and every published fold table stays reproducible.
- **The speed predictor**, `fill::` in `src/blss_ui.hpp` — every constant in it
  hardware-fitted — plus **`--blss-coverage <projectDir>`**, its headless twin.
  Same `blss::measureCoverage`, same verdict arithmetic, no second
  implementation. It exists because the round that *measured* the model could not
  re-derive the estimator's own figure: the estimator was a button in a GUI, and
  a number nobody can re-run is a number nobody can check.
- **The frame-timing rig**, `vendor/tyra/engine/inc/debug/frame_profile.hpp`
  (`TYRA_FRAME_PROFILE`, default 0, so a shipped `libtyra.a` carries none of it):
  `FRAMETIME` once a second, `FTRAW` per-frame ticks for **paired** samples,
  `FTSPLIT` for per-term attribution, a fairness fence so both arms are measured
  the same way, and **a calibration gate that must be run before any emulator
  number is quoted**.
- **The build interlock.** BLSS cannot be combined with depth of field, portals
  or split-screen — all three want real GS depth at display resolution, which
  since the z shrink is not merely unwritten but unallocated. `blssClashes()` +
  `blssInterlock()` put `#error` lines into the generated `inc/scene_data.hpp`
  naming the feature, the scene and the fix, so **nothing that produces an ELF
  gets past it**: not the build button, not `docker compose` + `make`, not CI. It
  **refuses rather than auto-disabling**, because a build that quietly measured a
  different configuration than the project describes is the worst outcome
  available for a feature whose entire point is being measured.
- **Two EE cuts, each priced against its own switch on hardware.** The activation
  table took `net` **1.93 → 0.79 ms** (both twins, one commit or neither), and an
  inline floor-to-int in place of newlib's `floorf` took **+0.227 ms**
  [+0.225, +0.229] off the bill. Both **bit-identical by construction and
  checked**: under `blssDebugView 2` the `BLSSWORST` / `BLSSFEAT` / `BLSSOUT` /
  `BLSSFILL` lines are byte-identical across paired frames before and after.
- **Determinism as a test, not an assertion.** `--threads N` is a wall-clock knob
  and nothing else: the same seed writes a **byte-identical `blss.net`** at any
  thread count, including matching the binary from before the corpus was
  parallelised. Training a 156-frame, 400-epoch, `--all-shots` fit goes 68.4 s at
  `--threads 1` to 18.5 s at six cores.

---

## What is measured and what is estimated

This distinction is the point of the branch, so it gets its own section.

**Measured, on a real PlayStation 2:** both A/B tables, the 4.60 ms EE bill and
every term in it, the 0.50 ms composite fill, the 0.5872 ms/pass calibration, the
0.7548 retention slope, both EE cuts and the proxy budget's price, and
bit-identity of all three.

**Measured, but in PCSX2 and admissible only for the EE:** the earlier
attribution rounds. The calibration gate says PCSX2 **under-reports GS fill by
76×**, so no emulator fill figure is admissible here. Its EE numbers transfer *in
aggregate* and **not per-function** — PCSX2 ranks `net` above `proxy`, hardware
ranks them the other way, and the activation table it valued at 2.11 ms was worth
**1.14** on the console. Attribute on hardware.

**Estimated, and flagged as such wherever it appears:** the **coverage count** a
project is judged by. `--blss-coverage` reports **72.63** on `examples/upscaler-lab`
(0.98 geometry + 71.65 emitters, p95 94.39) against the **58.7** blended-pass
equivalents the hardware fit implies — a **23.7 %** over-read, i.e. one *counted*
coverage there really costs **0.474 ms**, not 0.587.

> The mechanism this branch asserted for that gap — that the counter prices
> terrain and blended puffs in the same unit — is **falsified**. Geometry is
> **0.98** of the count against its own measured ceiling of **≤ 1.14**, so it is
> not over-priced; **98.7 %** of the count is already the 1.0-weight blended
> textured unit; and applying the weighting rule moves the total by **0.49**
> coverages against a **13.93** gap. So the anchor was restated in blended-pass
> equivalents and **the counter was deliberately left alone.**
>
> The live hypothesis is the **camera**. Hardware ran the fixture's own gameplay
> camera; the estimator averages six synthetic corpus shots whose per-shot totals
> span **36.2 to 88.4**, and a single camera at **57.7** sits inside that spread.
> If that is the whole story there is no counter bug at all — the two instruments
> are answering different questions. **The experiment that settles it** is to walk
> coverage under the fixture's own camera and compare that against 58.7; nothing
> should be rescaled first.

**Also estimated:** break-even at scales other than `2×2` (only `2×2` has a
measured point on that line), and the speed model outside the one scene it was
fitted on — five points on a single fixture whose variable load is alpha-blended
billboards. A scene whose overdraw is opaque geometry may retain a different
fraction, and nothing here measures that.

---

## The honest costs

- **The picture is softer.** With BLSS off, `upscaler-lab` resolves the cobbles
  crisply; with it on they smear into directional streaks at grazing angles.
  That is the reconstruction cost of a quarter-area raster on a high-frequency
  ground texture, and the PSNR table prices it — **27.17 dB against 28.52
  native**. What you buy for it is the frame rate above.
- **The demo's hardware A/B describes its previous geometry.** Every art asset in
  `examples/upscaler-lab` was replaced with **CC0 1.0** material on 2026-08-09
  (Kenney's Retro Urban Kit, plus the `wobbler.glb` five other examples already
  ship) because the cottage and the spider it shipped before had **unverified
  redistribution terms**. The GS fill that A/B measures is unchanged
  (`--blss-coverage` 72.63 → 72.23, emitters untouched) and the oracle ceiling
  rose (+1.058 → +1.108 dB), but the EE is ~4 ms a frame cheaper — almost all of
  it the animated model, 2 × 1 092 vertices → 2 × 123 — so both arms should land
  lower and the ratio higher. **That re-run is owed**: the console was unreachable
  for the session that made the swap, and PCSX2 cannot price GS fill.
- **The proxy budget ships off.** It is measured, it works, and it takes the EE
  bill to **4.17 ms** and break-even to 10.5 coverages for one channel of
  description (`coverage` 0.631 → 0.638, everything else identical to three
  decimals). It stays at 0 because `src/blsscorpus.cpp` has not been taught to cut
  the same way, and **flipping one half of a twin silently invalidates every
  trained net**. It needs a paired flip, in one commit, or not at all.
- **`blssrig` is a real fixture and BLSS loses on it.** Generated games fall on
  both sides of the break-even line. That is the feature's shape, not a defect,
  and it is why the default is off and why the estimators exist.

---

## The thread this branch is really about

Do not read the tables above without this section. **Eight times on this branch,
something a document or a code comment described did not exist, or was measured
on the wrong thing** — the entries are enumerated in `docs/neural-upscaler.md`
under "Measured is not optimised", and `docs/profiling.md` keeps its own
retractions in place rather than tidying them away. The shape recurs: an
instrument that cannot see the thing it exists to measure.

The four most expensive, in one line each:

1. **Per-frame PSNR could not see flicker** — it is a still-image metric, and it
   did not merely miss the oscillation, it *rewarded* it.
2. **Nothing ever charged for fill.** Sparsity is the entire performance case for
   this feature and the objective was blind to it, so the network asked for every
   kernel everywhere.
3. **The measurement was a sample of size one, and nothing said so.** Every
   out-of-distribution decibel this feature quoted came from a single held-out
   split. The ±0.4 dB blamed on the training seed was **split-selection**
   variance: fold to fold the sd is 0.35 dB, the sd of the per-seed fold *mean*
   is 0.04. Use `--cv`, never a single split.
4. **`drain` was published as the EE-bound/GS-bound discriminator for four
   months**, and the verdict "BLSS saves nothing" rested on it. It reads
   **0.02 ms in both arms** of the run that shows a 3.4× GS win. The only honest
   discriminator is to change the GS load and see whether the frame gets shorter.

Two rules fell out of it that are worth more than the feature:

- **An instrument only one side of a twin can run is not an instrument.** The
  corpus could always describe its own distribution; the console could describe
  nothing. Both halves ship permanently now — which is also why
  `--blss-coverage` exists.
- **A negative result needs a control or it is not a result.** Both channel
  deletions were 0.02–0.03 dB, which means nothing until something known-useful
  has been dropped for comparison.

---

## Reviewing it

```bash
./build.sh

# is there anything to win on this scene's PICTURE?
build/tyrax-editor --blss-eval examples/upscaler-lab

# ...and will the FRAME get shorter?
build/tyrax-editor --blss-coverage examples/upscaler-lab

# the out-of-distribution number: leave-one-PROJECT-out on the union corpus
build/tyrax-editor --blss-eval examples/procedural examples/cube bestiary \
    --cv --cv-groups

# determinism: these two must be byte-identical
build/tyrax-editor --blss-train examples/procedural --all-shots --threads 1 -o /tmp/a.net
build/tyrax-editor --blss-train examples/procedural --all-shots            -o /tmp/b.net
md5sum /tmp/a.net /tmp/b.net
```

Everything is also in the editor under *Tools ▸ Neural Upscaler (BLSS)*, which
runs `tyrax-editor --blss-<verb>` as a subprocess and parses its stdout —
deliberately, so there is no second implementation of what the CLI measures, and
so the tool's raw output can sit on screen under every table. **A parsed number
you cannot falsify is a number this feature has already been burned by.**

## What a human still owes

- **Walk coverage under `upscaler-lab`'s own camera** and settle the 72.63 vs
  58.7 question. Until then the estimator carries a known ~24 % question mark on
  that fixture, and the counter must not be rescaled.
- **The proxy budget's paired twin flip** (`src/blsscorpus.cpp` + the engine's
  `TYRA_BLSS_PROXY_BUDGET`), worth 0.43 ms and one coverage of break-even.
- **Hands-on eyes on the BLSS window's newest layout.** The arithmetic behind the
  verdict block, the cross-validation table and the error banner was checked from
  a host-only harness against captured runs; that is a different claim from
  "somebody looked at it".
- **The sky-dome proxy opt-out has never been booted**, only compiled; the
  console feature-spread numbers quoted anywhere predate it.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

[^geom]: Measured on this example's pre-CC0 geometry — see the licence bullet
    above and `examples/upscaler-lab/README.md`, "What the asset swap moved".
