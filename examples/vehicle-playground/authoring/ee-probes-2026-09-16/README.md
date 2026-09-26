# Raw evidence: the two bounding probes for the EE submission rearchitecture

Measured on a **physical PS2** (192.168.100.150, ps2link), 2026-09-16, against
[docs/ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md),
"The two probes that come first".

Neither probe ships. Their only product is evidence about how much of the
planned rearchitecture is worth building, and both of them **cap** a direction
the plan proposes.

Headline, garage day, frame `work` (update + submit + finish), against a
**repeatability floor of 0.135 ms**:

| question | answer |
| --- | ---: |
| what per-package frustum rejection BUYS (A1) | **2.60 ms** |
| what per-package classification COSTS (attrib arm) | **1.79 ms** |
| what coarsening the classification costs (A2) | **+4.59 ms** |
| what `FlushCache` costs, refill included (b1 − b2) | **1.09 ms** |
| what the cheapest legal way to stop calling it costs (b2 − ctl) | **+7.55 ms** |

So **S3 is refuted and S1 is capped at about half its predicted value**, and
the one arm that stopped calling `FlushCache` **corrupted the picture** even
with the packet uncached. Details below.

## Method

Every rule this repository has been burned by, and how this round satisfies it:

- **Fixture regenerated before measuring**, never from the example's committed
  generated sources: `examples/vehicle-playground` was rebuilt with
  `tyrax-editor --build` first, and every arm then ran `--refresh-gen` on its own
  fixture before the instrumenter. `reuploads` is **0.000 in all four poses of
  all eight runs** and `triangles` is identical to the unit between the two
  control boots (docs/vu1-and-dma-cache-cost.md, "the fixture was stale").
  **But "regenerated" is not the same as "branch tip" — see
  [Fixture identity](#fixture-identity-the-control-is-a-72-run-fixture) below,
  which is the check that catches the difference.**
- **Built with `tools/toolchain/native-build.ps1` directly** (`build-arm.ps1`),
  never through an editor `--build`, which would regenerate the instrumentation
  away.
- **Fixture**: `authoring/benchmark-district.py` + `authoring/instrument-frame-cost.py
  --attribute`, four parked poses, **120 warm-up frames then 240 recorded rows
  per pose** = 960 rows, written once at frame 1440 with no host I/O during
  sampling. Every run collected all 960.
- **Every ELF hashed, and no two arms are one binary** — see the table below.
- **Control alternated with candidate**, and the repeatability floor is quoted
  from two boots of ONE ELF.
- **Picture compared** on garage day only. The night poses twinkle and nothing
  may be read from a repeat there.

The traffic is parked, which matters for Probe B — see "What this does not
establish".

### Arms

Each arm is a separate ELF; the probes are compile-time macros in
`vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_probes.hpp`,
all defaulting to 0, in the style of `TYRA_STAPIP_ATTRIB`. (`#ifndef NDEBUG` is
NOT the gate in this engine - a game build never defines NDEBUG, so diagnostics
hung off it ship live; docs/render-submission-attribution.md.)

| arm | macros | ELF sha256 |
| --- | --- | --- |
| `ctl` | all 0 | `0FA62C5A2AA009694AAB257E9D72CBB8CA12BAD6A4DEEAD589076EAD7121D998` |
| `a1` | `ACCEPT_ALL=1` | `4D5E030E9AE7CEA9B7D3DAF380FDC4BC2D175BCDFDE443A16098C7C160F39D2D` |
| `a2` | `COARSE_CLASSIFY=1` | `F9618C9CC36CB12B9B161B6921EFFD72B74E13A48286AEF1F99C8F8453499FD9` |
| `b1` | `UNCACHED_CHAIN=1` | `FEC48B0DD7F43E0FCEBC26E9157FC4E94459F9E13801D798221C5240C5A6FD80` |
| `b2` | `UNCACHED_CHAIN=1, FORCE_FLUSH=1` | `D864B9056F177E5FA18203F874634808334161273E7CE63A3363619FC9120E19` |
| `attrib` | `TYRA_STAPIP_ATTRIB=1` | `B440F8DEB1ECFE92197760D25E565ECD92C02E67195E398DB845B8C963062D5D` |
| `ctlquiet` | all 0, `--profile quiet-debug` | `DB461099CD43647012F0FC9948EC8F754ACB9A6A33628EE72D24E6E2E83C4902` |

`ctl` was booted twice on the same ELF (`ctl-boot1`, `ctl-boot2`).

### Reproducing

```powershell
# one arm: fixture + regenerate + instrument + native build, ELF hash recorded
.\build-arm.ps1 -Arm a1 -AcceptAll 1
# one physical run: reset, boot over ps2link, collect the CSVs
.\run-console-arm.ps1 -Arm a1
# the picture gate: hold garage day (pose 0) and let the game screenshot itself
.\capture-arm.ps1 -Arm a1
# read it
python summarize_cost.py ctl1=<results>\ctl-boot1 a1=<results>\a1
python summarize_classify.py <results>\attrib
```

`probe-patch.diff` plus the committed `stapip_probes.hpp` is the whole probe, so
a later reader can re-run these arms without reconstructing them.

**THE CONSOLE IS A SHARED RESOURCE.** A listening tcp/18193 does not mean free —
check `Get-CimInstance Win32_Process -Filter "name like '%ps2%'"` for a live
`ps2client` command line before resetting anything.

## Fixture identity: the control is a 72-run fixture

**Run this check on any control before quoting it against another round's
table. A matching capture hash is a PICTURE check and never a fixture check** —
changing the strip run changes how a surface is cut into runs, not which pixels
it covers, so a fixture built by the wrong editor still hashes to the expected
frame. The checks that do catch it are the scene-load producer lines and the
baked constant (`console/fixture-identity.txt`):

| check | branch tip (75) | this control | |
| --- | ---: | ---: | --- |
| `ROADSTRIP … packages` | 470 | **526** | pre-75 |
| `TERRAINSTRIP … vertices / packages` | 588 / 8 | **591 / 9** | pre-75 |
| `stripRun` in the generated source | 75 | **72u** | pre-75 |
| garage-day capture hash | `193D9D59…` | `193D9D59…` | **matches, and proves nothing** |

The cause is exact and worth generalising: the editor binary used here
(`B54E4CA4…`) was built at **15:17:06**, and `9a9d25e0` *"Raise the VU1 package
ceiling to 75 and re-bake the strip runs"* landed at **17:07:26** — nearly two
hours later. **An editor binary sitting in `build/` is not "the editor at the
tree's commit"**; it can be older than HEAD, and nothing in a build log says so.
`--refresh-gen` faithfully regenerated the fixture with a baker that still said
72.

So this round's engine derives `getMaxVertCount` 75 while the baked runs are 72,
and `pinPackageSize` pins the packages to 72. That is a legal, self-consistent
configuration — pinning *below* a class's derived size is safe, only pinning
above it overflows the VU1 buffer — but it is **not the configuration the plan
document's four-pose table describes**, which is the branch-tip 75.

**What this does and does not cost.** Every one of the seven ELFs was built from
the same regenerated example with the same editor, so all of them carry
`stripRun = 72` identically and **every delta in this document is unaffected**.
What carries a caveat is the absolute level: at 75 the roads hold 10.6% fewer
packages and terrain chunks 11.1% fewer, so a branch-tip fixture would classify
and submit somewhat fewer packages. Expect the classification bracket, the
rejection benefit and the flush cost all to be a few per cent smaller there, in
the same direction — which moves none of the conclusions, since the two
refutations turn on 2.60 against 1.79 and on +4.59 against a 1.79 ceiling.

**The 40 412 triangles are NOT a sample-window artefact.** The shared reflection
probe alternates every other frame exactly as the plan's S4 says — garage day
reads 45 638 / 35 186 triangles and 133 / 107 flushes — and **both control boots
caught exactly 120 frames of each**, so the window is perfectly balanced and the
mean is not parity-dependent. The 549-triangle difference from the plan's 40 961
is the fixture, not the probe.

## Why this control's `work` is 6.4 ms above the plan document's

The plan's headline reads garage-day `work` 30.42 ms with `update` 3.26; the
`ctl` arm here reads 36.51 and 7.59. **The difference is the live tools, and it
is measured rather than inferred.**

`benchmark-district.py --profile debug` leaves `liveLink`, `liveDebug`,
`liveLogic`, `timeMachine` and `remotePad` **on**, and two of their pollers —
`livepad::tick` and `livedbg::tickFromLoop` — run **inside the instrumenter's
`update` bracket**, `fopen`-ing `livepad.bin` and `livedbg.cmd` over `host:`
every frame. `--profile quiet-debug` is the same build profile with those six
settings off. One extra control boot (`ctlquiet`, ELF `DB461099…`) prices them:

| garage day | plan table | `ctlquiet` | `ctl` |
| --- | ---: | ---: | ---: |
| `update` | 3.26 | **2.804** | 7.594 |
| `submit` | 26.14 | **26.234** | 27.893 |
| `finish` | 1.02 | **1.033** | 1.024 |
| `work` | **30.42** | **30.071** | 36.512 |
| `present` | 9.54 | **9.886** | 6.945 |
| `total` | 39.96 | **39.957** | 43.457 |

So the live tools cost **4.79 ms of `update` and 6.44 ms of `work`** on this
fixture — inside the 4.69–5.41 ms/frame the hardware profiler priced live-tool
`fopen` probing at — and with them off the two tables agree to 0.35 ms of `work`
and 0.00 ms of `total`. The residual is the 72-vs-75 strip run plus the
`--attribute` game-side hooks, which this round applies to **every** arm
including the control (that holds them constant, but it is not what produced the
plan's table).

The same effect explains the only ugly numbers in the arm tables: `update`
carries sporadic +3.5 to +4.4 ms outliers (`attrib` 11.997, `b2` outer-night
11.371) that no code path in those arms can produce. That is host-I/O
contention, not the probes, and it is why **`work` deltas should be read through
`submit`/`dispatch` rather than through `update`** in this round.

## The repeatability floor

Two boots of ELF `0FA62C5A…`, per-pose means over 240 rows each:

| metric | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` | −0.135 | −0.028 | +0.033 | −0.095 |
| `submit` | +0.037 | +0.044 | +0.012 | +0.030 |
| `triangles`, `flushes` | identical | identical | identical | identical |

**Read every delta below against 0.135 ms of `work` and 0.044 ms of `submit`.**

## Probe A — what is per-package frustum classification worth?

### A1 "accept-all": the rejection is worth 2.60 ms

`StaPipBagPackager::checkFrustum` still runs, and so does the coarse
eight-package test; only the `OUTSIDE_FRUSTUM` verdict is mapped to
`IN_FRUSTUM`. `PARTIALLY_IN_FRUSTUM` is untouched, so every package that
straddles a plane still takes the clip route. The EE therefore pays exactly the
classification it pays today and the delta is the BENEFIT side alone.

**The coarse level had to be covered too.** It rejects eight packages on one
test, so an arm that only mapped the per-package verdict would have left most
of the frame's rejections in place and measured almost nothing.

| vs `ctl`, means over 240 rows | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **+2.595** | +1.775 | +1.042 | +0.671 |
| `submit` ms | +1.855 | +1.973 | +0.895 | +0.964 |
| `dispatch` ms | +1.824 | +1.744 | +0.866 | +0.862 |
| `vif_wait` ms | +1.092 | +1.084 | +0.475 | +0.481 |
| `triangles` | +8 387 | +8 424 | +5 195 | +5 215 |
| `flushes` | +5 | +5 | +3 | +3 |

**The picture is byte-identical** — `ctl`, `a1` and `a2` garage-day captures all
hash to `193D9D59C25A0AACFE9D7693D00BB87F27430644C4FA6B480D13BF43841A7B05`.
The extra packages are off screen and the cull programs' per-vertex ADC
judgement drops them, exactly as predicted.

Two things fall out. **Rejection pays for itself**: it buys 2.60 ms and the
whole classification that produces it costs 1.79 ms (below), so the net is
about +0.8 ms in the engine's favour and the redesign **must keep an equivalent
visibility test**. And **most of what it buys is VU1, not EE**: 1.09 of the
2.60 ms is VIF1 wait.

### The cost side: classification is 1.79 ms, and that is the ceiling on S3

From the `attrib` arm (`TYRA_STAPIP_ATTRIB=1`), `dsClassify_ms` — the bracket
around `checkFrustum` alone, summed over every package and subpackage:

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `dsClassify_ms` | **1.789** | 1.895 | 0.576 | 0.600 |
| `dsCreate_ms` (inclusive of it) | 2.469 | 2.605 | 0.763 | 0.793 |
| `dsPackages` | 570.5 | 601.5 | 228.5 | 235.5 |
| `dsMergeParts` | 1 323 | 1 354 | 411.5 | 418.5 |
| `dsMaskCalls` | 269 | 290 | 91 | 94 |
| `dsDirectBags` / `dsPartialBags` | 53.5 / 59 | 76 / 65 | 13 / 20.5 | 16 / 21.5 |

**No change to how packages are classified can save more than 1.79 ms**, because
that is the whole bracket. The `attrib` build costs 5.25 ms of `work` over the
control (nearly all of it the instrumenter's own `--attribute` pass in
`update`), and the COP0 brackets over-report by about 6%, so read 1.79 as an
upper bound with a few per cent of slack in the same direction.

Note `dsPackages` is **570.5**, not the 1 972.5 the plan quotes as "total
package classifications": about half the frame's bags are wholly visible and
take the direct route, which never calls `packager.create` at all. The FTCLIP
figure counts routed packages, not classifications.

### A2 "coarse classify": coarsening is a NET LOSS of 4.59 ms

One classification per existing eight-package coarse group, reused by all eight
— verdict, VU1 plane mask and guard-band answer — with rejection and clip
routing intact. A partial group's mask is a superset of each package's, so the
arm is conservative everywhere and, measured, the **picture is byte-identical**.

| vs `ctl` | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **+4.591** | +4.639 | +2.318 | +2.462 |
| `dispatch` ms | +4.500 | +4.444 | +2.141 | +2.147 |
| `packet` ms | +1.079 | +1.112 | +0.448 | +0.452 |
| `flushes` | +26 | +26 | +11 | +11 |
| `triangles` | +4 878 | +4 878 | +2 664 | +2 664 |

It is **worse than A1** (+4.59 against +2.60) while adding *fewer* triangles
(+4 878 against +8 387). The reason is visible in `packet` and `flushes`: a
coarse box crosses more clip planes, so whole groups of eight take the clip
route where one package would have. Clipping is the expensive route, and this
arm buys eight of it at a time.

Since the entire classification it removes is worth at most 1.79 ms and the
routing it coarsens costs 4.59, **there is no version of this trade that wins**.

## Probe B — what does `FlushCache` really cost, refill included?

`StaPipQBufferRenderer::allocateOnUse` allocates `packets[0..1]` as
`P2_TYPE_UNCACHED`, and `sendPacket()` passes `flush_cache = false`.

**Two controls, because the arm changes two things at once.** Allocating the
chain uncached also makes every packet write go to memory instead of to the
data cache, and on this scene that second effect is by far the larger. `b2` is
the same uncached build **still calling `FlushCache`**, which holds the write
cost constant:

```
b2 - ctl  = the cost of writing the chain uncached
b1 - b2   = the cost of the flush, refill included
```

| garage day | ctl | b2 | b1 |
| --- | ---: | ---: | ---: |
| `work` ms | 36.512 | 44.064 | 42.973 |
| `dma` (the `send_packet2` bracket) ms | 2.235 | 2.102 | 1.260 |
| `packet` ms | 1.861 | 2.546 | 2.568 |
| picture | reference | **byte-identical** | **CORRUPT** |

**The flush is worth 1.09 ms of `work` in garage day**, and the `send_packet2`
bracket falls 0.842 ms when it is dropped:

| `b1 − b2` | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `dma` bracket ms | **−0.842** | **−0.969** | −0.258 | −0.291 |
| `work` ms | −1.091 | −1.487 | +0.161 | −4.021 |

Use the `dma` row. The `work` row is noise-dominated in the outer poses — `b2`'s
outer-night boot carries a +3.5 ms `update` outlier, and both outer poses sit on
a vsync rung where `present` absorbs the difference.

**The plan predicts −2.10 / −2.49 for S1. The measurement is −0.84 / −0.97 of
the bracket and about −1.1 / −1.5 of frame work, so the prediction is roughly
2x optimistic** — and the "unmeasured refill" it hoped for on top is small: the
difference between the bracket delta and the work delta is about 0.25 ms, not a
hidden prize.

### The cost of the legal implementation is 7x the prize

| `b2 − ctl` | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| `work` ms | **+7.552** | +8.420 | +3.596 | +6.832 |
| `packet` ms | +0.684 | +0.697 | +0.195 | +0.193 |
| `prepare` ms | +0.618 | +0.700 | +0.126 | +0.119 |

`packet2` writes floats one at a time and an uncached store does not gather, so
the chain costs 7.55 ms more to build than it saves by not being flushed.

### And dropping the flush CORRUPTED the picture

This is the result that matters most for S1.

`b2` — uncached packets, flush still called — is **byte-identical to the
control**. `b1` — the same build with `flush_cache = false` — differs in
**1 271 of 262 144 pixels**, confined to rows 204..218, columns 170..409: a
14-row band at the horizon where the road and the far buildings tear into
horizontal slices (`captures/b1-vs-ctl-horizon-band.png`, top `ctl`, bottom
`b1`).

The arm was already bounded against the known hazard: a send whose chain
references the qbuffer copy pools (`fillByCopyMax` / `fillByCopy1By2` /
`fillByCopy1By3` / `fillByStripExpand` / `StaPipClipper::writeChunk`) **keeps**
the flush, and the allocation was verified on the console before anything was
read from it — `PROBEB: packet 0 base segment 0x2` in `console/b1/host.log`,
with a `TYRA_ASSERT` that would have halted the boot had it not taken effect.

So **the packet is not the only thing `FlushCache(0)` was writing back**, and
the plan's justification for S1 — "the packet is no longer in cached memory, so
there is nothing to write back" — is false as written. Two hypotheses remain
open and this round did not separate them:

1. **Another EE-written, DMA-read buffer.** Something the generated game or the
   pipeline writes into cached memory per frame and reaches by a DMA `REF` tag,
   which is not one of the copy pools. The horizon band points at the road or
   terrain strips.
2. **`FlushCache(0)` is also a barrier.** It is syscall 100 and contains `sync`;
   removing it removes the ordering as well as the write-back, so the DMAC may
   simply be starting before the EE's stores have landed.

Whoever builds S1 owes an answer to that before anything else.

## What this does not establish

- **The parked fixture cannot see one class of Probe B defect.** A per-frame
  rebake that writes the SAME bytes every frame leaves stale cache lines that
  are indistinguishable from fresh ones, so a flush-removal bug in that path is
  invisible here by construction. The corruption found above is therefore a
  LOWER bound on the problem, not an inventory of it. Re-run Probe B with
  `--keep-routes` before concluding anything about its correctness.
- **`P2_TYPE_UNCACHED_ACCL` was never booted**, deliberately. See
  `stapip_probes.hpp`: the stock ps2sdk packet builder back-patches bytes it has
  already written (`packet2_vif_close_unpack_auto` does an `lbu` of byte 3 and an
  `sb` into byte 2 of the open unpack VIFcode; disassembled from
  `ps2sdk/ee/lib/libpacket2.a`), and under UCAB those go through the EE's
  128-byte write-gather buffer. `P2_TYPE_UNCACHED` has no such buffer, which is
  why it is the arm that was run. UCAB would be faster on the write side and is
  the version S1 would actually want — but it needs a packet builder that never
  reads back, i.e. owning `packet2`.
- **No A/B here is an FPS claim.** The buckets overlap, `work` is
  update + submit + finish, and `bounds`/`prepare`/`dispatch` are included in
  `submit` rather than additional to it.
- **The A2 arm coarsens to the EXISTING eight-package group, not to a
  "1/3-bbox part".** A part is one third of a PACKAGE in this engine, so the
  plan's S3 as literally written would triple the number of tests rather than
  reduce them, and parts are not shared between packages so nothing amortises.
  A2 measures the coarsening that could actually have removed work.
- **The night poses were not pixel-compared.** They twinkle.

## Files

- `console/<arm>/frame-cost.csv` — the 960 recorded rows of every run.
- `console/attrib/frame-attrib.csv` — the `TYRA_STAPIP_ATTRIB` split.
- `console/summary-cost.txt`, `console/summary-classify.txt` — the tables above.
- `console/fixture-identity.txt` — the scene-load producer lines and the baked
  `stripRun` literal, i.e. the check that a capture hash cannot do.
- `console/<arm>/run.json` — ELF hash, arm macros, editor hash, commit, boot time.
- `captures/ctl-garage-day.png` — the reference frame. `a1`, `a2` and `b2`
  produced files that hash to exactly this one
  (`193D9D59C25A0AACFE9D7693D00BB87F27430644C4FA6B480D13BF43841A7B05`), so they
  are not committed a second time.
- `captures/b1-garage-day.png` — the corrupt frame
  (`CB161A9327BD2AFEE83CBDA0ADB10C95D9992FCB8B88FD60B062EE715BA13047`), and
  `captures/b1-vs-ctl-horizon-band.png`, rows 195..228 at 3x, `ctl` above `b1`.
- `probe-patch.diff` — the engine side of the probes. The other half is the
  committed `stapip_probes.hpp`, which carries the macros and the reasoning.
- `summarize_cost.py`, `summarize_classify.py` — the readers used above. The
  fuller renderScene-phase reader lives in
  [`../submission-attribution-2026-09-16/summarize_attrib.py`](../submission-attribution-2026-09-16/summarize_attrib.py).
