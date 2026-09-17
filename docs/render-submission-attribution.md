# Where render submission really goes: attributing the unmeasured 7.5 ms

Render submission is the largest phase of a Motor District frame, and until now
three quarters of it was measured and a quarter was not. The static pipeline's
`bounds`, `prepare` and `dispatch` brackets account for 22.2 ms of the garage
pose's 29.7, and nothing named the other 7.5 — which is bigger than any single
saving the five previous rounds of this work produced. This page is the
instrument that names it, and what the instrument found.

The short answer is that **97% of the gap was never in the static pipeline at
all**. The `submit` bucket every previous page quotes is the whole
`beginFrame()`..`endFrame()` block — post-processing and the 2D HUD included —
while the three brackets only ever covered `StaPipCore::render`. Reading one
against the other compares a frame to a function. Measured in the emulator, only
0.172 ms of a 5.552 ms gap is inside `StaPipCore::render` and outside its three
brackets; the other 5.38 is the generated game's own renderScene, `beginFrame`,
and the HUD.

Three of the suspects that made the gap look like pipeline overhead are
falsified, and what the numbers name instead is not submission: the per-object
visibility, distance, LOD and split-band tests the Objects loop runs for every
object in the scene cost **0.148 ms**, and `renderVehicleWheels` costs
**2.962**, of which **1.970** is the generated game rebuilding every wheel
vertex on the EE, every frame.

A second round then split the two buckets that survived this one —
[`bounds` and the package box](#round-two-inside-bounds-and-inside-the-package-box).
It exonerates the bbox cacher that this page nominated as the next suspect, and
finds 22% of `bounds` in a per-bag fan-out of one `u32` to thirty-two qbuffers
that had never been looked at because it reads as three stores. Removing it is
worth **−0.34 ms of `bounds`**, ten times what the two previous attacks on that
bucket managed between them.

A third round then asked the question every per-package term raises —
[can a package hold more vertices?](#round-three-the-package-size-is-at-its-ceiling-and-the-ceiling-is-81)
The answer is no, and it is arithmetic rather than judgement: **72 is 96% of
what the class this frame runs can derive and 89% of what the whole of VU1 data
memory allows**, and the 144 that would halve the package count wants 1.73x that
memory. The per-class pin is exonerated too — every class in the frame derives
exactly 72, by two independent routes.

## The two instruments, and the one macro that must not ship

Both halves are opt-in and both default to **off**.

**The engine half** is `TYRA_STAPIP_ATTRIB` in
`vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_attrib.hpp`,
defaulting to 0. At 0 no field, no COP0 read and no branch of it is compiled;
at 1 it adds a `StaPipAttrib` block to `StaPipTelemetry`, which the existing
`takeTelemetry()` returns and clears like everything else. The counters still
respect `setTelemetryEnabled`, so arming is the same one line it always was.

**`#ifndef NDEBUG` is NOT the devkit gate in this engine, and reaching for it is
a documented way to ship a diagnostic live.** A game build never defines NDEBUG
— `release:` in `vendor/tyra/Makefile.base` adds `-DNDEBUG`, and nothing
invokes that target; `tools/toolchain/native-build.sh` runs plain `make`, and
the generated project's release profile only drops `-g` and clears `KEEPSYM`.
A GS VRAM census keyed that way once and cost about 1 ms a frame in a shipped
release build before it was caught (docs/gs-vram.md). Hence an explicit macro
that defaults to 0, like `TYRA_FRAME_PROFILE`.

**The game half** is `examples/vehicle-playground/authoring/instrument-frame-cost.py
--attribute`, which patches an isolated benchmark fixture's generated
`terrain_game.cpp` and writes a second file, `bin/frame-attrib.csv`, beside the
existing `bin/frame-cost.csv`. It brackets every `renderScene` phase, splits the
object loop, and splits the post-process and HUD blocks out of `submit`.

Nothing in either half drains. The render-cost capture the generated game
already carries (*Debugger > Render cost*) brackets the same phases but calls
`sync.align3D()` around each one, which serialises the frame on purpose — that
is attribution evidence, not a frame-time sample, and its milliseconds are not
comparable with these.

## What each bracket covers

Reading is a subtraction at every level, and every level is a whole minus its
measured parts.

```
submit  =  beginFrame() .. endFrame()          the frame-cost CSV's submit_ms
  dmHud       updateHudMotion .. endFrame      HUD motion, sprites, menus,
                                               bloom/grading/grain
  dmPost      updateSunFx .. updateHudMotion   depth of field, god rays, flare
  scene       the remainder                    beginFrame, the day/night tick,
                                               the BLSS bracket and renderScene
    rsTotal     renderScene, entry to return
      rsHead      .. renderCameraFeed()        split band, sky retint, the env
                                               basis, the shared probe, the feed
        rsEnvProbe    the shared reflection-probe pass alone
      rsPortal    renderPortalView
      rsSky       the dome, the stars, the discs
      rsTerrain   updateTerrainChunks + renderTerrain
        rsTerrainDraw renderTerrain alone, i.e. not the chunk streaming
      rsBatches   renderStaticBatches
      rsProc      renderProcChunks
      rsObjects   the whole Objects loop
        rsObjSubmit   the per-object part loop (every stapip.core.render of an
                      object's base, AO, emissive and env bags)
        the rest      the per-object TESTS: impostor, dirty, matrix refresh,
                      visible, draw distance, coarse-outside, split band,
                      mesh LOD, highlight, reflected probe
      rsWheels    renderVehicleWheels
        rsWheelSubmit the ONE bag submit per vehicle definition, i.e. not the
                      per-frame EE rebake of every wheel vertex
      rsAnim / rsDecorate / rsLightFx / rsHighlight / rsParticles
```

Four of those are deliberately SUBSETS of the phase above them (`rsEnvProbe`,
`rsTerrainDraw`, `rsObjSubmit`, `rsWheelSubmit`), because the question a phase
raises is always the same one: how much of it is the game's own EE work and how
much is the submission it wraps.

and inside the engine:

```
spRender   StaPipCore::render, first instruction to last, summed over the frame
  spHead     entry .. the bounds bracket: the fog decision, the frustum-culling
             read, and the thirteen TYRA_ASSERTs
  bounds     the shipped bracket (the package-size derivation, the transform
             cache's two matrix memcmps, the bbox cache, the main AABB test)
  prepare    the shipped bracket, now split six ways:
    spPkgr     packager setup, the MVP multiply, the transform-cache store,
               the object-space clip planes
    spTex      the batch-candidacy test, the VRAM residency lookup, the wrap
               settings, useTexture(), the clamped-wrap drain
    spProg     ensureProgramSet + clearLastProgramName
    spLight    the world bounding sphere, pickDynLight, setBagLight
    spBlss     the BLSS bag-proxy feed
    spObjData  sendObjectData + setClipperMVP + setInfo
  dispatch   the shipped bracket, of which packet_included, dma_included and
             vif_wait_included were already split out, plus one hole that was
             not:
    spGifWait  dma_channel_wait(GIF) in sendPacket - "wait for texture",
               between the VIF1-wait bracket and the submit bracket and inside
               neither
  spTail     the dispatch bracket's end .. return
```

`spCalls` counts entries to `render()` and `spCulled` the ones that left early
on an outside-frustum box. They are the denominator for every per-bag figure —
"about 120 bags" was an estimate and is now a number.

## The recipe

```powershell
# 1. an isolated fixture, REGENERATED by the editor under test (a fixture built
#    from an example's committed generated sources is a different game - see
#    docs/vu1-and-dma-cache-cost.md)
python examples\vehicle-playground\authoring\benchmark-district.py `
    $env:TEMP\tyra-editor-test\dattr --profile release
build\tyrax-editor.exe --build $env:TEMP\tyra-editor-test\dattr

# 2. instrument it (an editor --build from here on would regenerate this away)
python examples\vehicle-playground\authoring\instrument-frame-cost.py `
    $env:TEMP\tyra-editor-test\dattr --attribute

# 3. arm the engine half, then compile with the toolchain directly
#    (TYRA_STAPIP_ATTRIB 1 in stapip_attrib.hpp)
tools\toolchain\native-build.ps1 -Project ... -Engine ... -Cache ... -Toolchain ...

# 4. boot it, wait for four 360-frame poses, read bin/frame-attrib.csv
```

The four poses are the benchmark script's own: garage day, garage night, outer
road day, outer road night, 120 warm-up frames and 240 recorded rows each. The
raw CSVs, the summariser and the exact arms are archived in
[authoring/submission-attribution-2026-09-16](../examples/vehicle-playground/authoring/submission-attribution-2026-09-16/README.md).

## What the hooks cost

Instrumentation has its own price and a table that does not state it is not
finished. Three arms of one fixture, one knob each:

| arm | game half | engine half |
| --- | --- | --- |
| C | plain `instrument-frame-cost.py` | `TYRA_STAPIP_ATTRIB` 0 |
| B | `--attribute` | `TYRA_STAPIP_ATTRIB` 0 |
| A | `--attribute` | `TYRA_STAPIP_ATTRIB` 1 |
| A2 | `--attribute` + the four subset brackets | `TYRA_STAPIP_ATTRIB` 1 |

B − C prices the game-side phase brackets; A − B prices the engine's per-bag
brackets.

Measured, garage day, mean of 240 warmed rows each, `submit_ms`:

| arm | submit ms |
| --- | ---: |
| C, boot 1 | 14.633 |
| C, boot 2 | 14.689 |
| B | 14.536 |
| A | 14.592 |
| A2 | 14.595 |

**Same-ELF repeatability is 0.056 ms**, and the three shipped brackets are
steadier still: `bounds` reads 1.958 / 1.959 / 1.962 / 1.966 and `dispatch`
5.966 / 5.967 / 5.966 / 5.975 across all four boots. Against that floor, the
fully instrumented arm A is **0.069 ms faster** than the control's midpoint and
the game-only arm B is 0.125 ms faster — so **neither set of hooks is
measurable, and the sign is wrong for a cost.** Read that as "under 0.1 ms",
not as a saving: the instrumented translation unit is a different size and lays
out differently, and PCSX2 models no instruction cache either. The engine half
adds fourteen `mfc0` per submitted bag (226.5 bags here, ~3 200 reads a frame)
and it does not reach the noise floor.

## Measured (PCSX2, release profile, garage day)

Arm A2. Every level is a whole minus its measured parts, and **the renderScene
level closes with a residual of 0.000 ms** — the thirteen phases sum to 13.953
of 13.953, and so do all four poses of all three attributed arms.

| bucket | ms | share of submit |
| --- | ---: | ---: |
| **submit — `beginFrame()`..`endFrame()`** | **14.595** | 100% |
| ├ HUD block (motion, sprites, menus, bloom/grading/grain) | 0.065 | 0.4% |
| ├ post-fx block (depth of field, god rays, flare) | 0.000 | 0.0% |
| └ scene block | 14.530 | 99.6% |
| &nbsp;&nbsp;├ beginFrame + day/night tick + adaptive + BLSS entry points | 0.577 | 4.0% |
| &nbsp;&nbsp;└ **renderScene** | **13.953** | 95.6% |
| &nbsp;&nbsp;&nbsp;&nbsp;├ head — split band, sky retint, env basis, shared probe, camera feed | 0.820 | 5.6% |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;└ *of which the shared reflection probe* | *0.820* | *5.6%* |
| &nbsp;&nbsp;&nbsp;&nbsp;├ portal through-view | 0.000 | — |
| &nbsp;&nbsp;&nbsp;&nbsp;├ sky dome + star field + sun/moon | 0.134 | 0.9% |
| &nbsp;&nbsp;&nbsp;&nbsp;├ terrain | 1.006 | 6.9% |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;├ `renderTerrain` | 1.002 | 6.9% |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;└ *the chunk streaming* | *0.004* | — |
| &nbsp;&nbsp;&nbsp;&nbsp;├ static batches | 1.262 | 8.6% |
| &nbsp;&nbsp;&nbsp;&nbsp;├ procedural chunks + roads | 1.341 | 9.2% |
| &nbsp;&nbsp;&nbsp;&nbsp;├ **Objects loop** | **5.049** | 34.6% |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;├ the per-object part submits | 4.901 | 33.6% |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;└ **every per-object test, for every object** | **0.148** | **1.0%** |
| &nbsp;&nbsp;&nbsp;&nbsp;├ **vehicle wheels** | **2.962** | **20.3%** |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;├ **the per-frame EE rebake of every wheel vertex** | **1.970** | **13.5%** |
| &nbsp;&nbsp;&nbsp;&nbsp;│&nbsp;&nbsp;└ the one bag submit per vehicle definition | 0.992 | 6.8% |
| &nbsp;&nbsp;&nbsp;&nbsp;├ animation / shadow decals / mirrors / portal surfaces / highlights | 0.000 | — |
| &nbsp;&nbsp;&nbsp;&nbsp;├ light pools + projected & blob shadows + beams | 1.302 | 8.9% |
| &nbsp;&nbsp;&nbsp;&nbsp;├ particles | 0.076 | 0.5% |
| &nbsp;&nbsp;&nbsp;&nbsp;└ *residual* | *0.000* | — |

And the engine, the same frame — **226.5 calls to `StaPipCore::render`, of which
114 leave at the bounding-box test**:

| bucket | ms |
| --- | ---: |
| **`StaPipCore::render`, whole function** | **9.228** |
| ├ head — the fog decision and the thirteen live `TYRA_ASSERT`s | 0.072 |
| ├ `bounds` (shipped bracket) | 1.966 |
| ├ `prepare` (shipped bracket) | 1.106 |
| │&nbsp;&nbsp;├ packager + MVP + transform-cache store + clip planes | 0.291 |
| │&nbsp;&nbsp;├ batch candidacy + VRAM residency + `useTexture` + wrap | 0.199 |
| │&nbsp;&nbsp;├ `ensureProgramSet` + program-name compares | 0.010 |
| │&nbsp;&nbsp;├ world bounding sphere + `pickDynLight` | 0.077 |
| │&nbsp;&nbsp;├ BLSS bag proxy | 0.002 |
| │&nbsp;&nbsp;├ **`sendObjectData` + `setClipperMVP` + `setInfo`** | **0.507** |
| │&nbsp;&nbsp;└ *residual* | *0.020* |
| ├ `dispatch` (shipped bracket) | 5.970 |
| │&nbsp;&nbsp;├ packet construction | 1.287 |
| │&nbsp;&nbsp;├ the `send_packet2` bracket | 1.105 |
| │&nbsp;&nbsp;├ VIF1 wait | 0.215 |
| │&nbsp;&nbsp;├ GIF wait (previously in no bracket) | 0.017 |
| │&nbsp;&nbsp;└ **package creation and classification** | **3.346** |
| └ tail — the clamped-wrap restore | 0.100 |

**The gap closes exactly.** `submit` minus the three shipped brackets is
**5.552 ms**, and it is:

| what it was | ms |
| --- | ---: |
| the generated game's own EE work inside renderScene (13.953 − 9.228) | 4.725 |
| `beginFrame` + day/night + adaptive + the BLSS entry points | 0.577 |
| `StaPipCore::render`'s own head and tail | 0.172 |
| the HUD block | 0.065 |
| the post-fx block | 0.000 |
| the attribution hooks' own COP0 reads | 0.013 |
| **total** | **5.552** |

Two arms of that table were taken from two different ELFs — one with the four
subset brackets and one without — and they agree to **0.003 ms of submit**.

### All four poses

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| submit | 14.595 | 18.655 | 4.854 | 6.468 |
| `bounds` + `prepare` + `dispatch` | 9.042 | 11.240 | 3.728 | 4.602 |
| **the gap** | **5.552** | **7.414** | **1.125** | **1.866** |
| — the game's own EE work in renderScene | 4.725 | 5.791 | 0.417 | 0.864 |
| — beginFrame + day/night + adaptive + BLSS | 0.577 | 0.577 | 0.577 | 0.577 |
| — `StaPipCore::render` head + tail | 0.172 | 0.961 | 0.055 | 0.363 |
| — the HUD block | 0.065 | 0.068 | 0.069 | 0.056 |
| — the post-fx block | 0.000 | 0.000 | 0.000 | 0.000 |
| — the hooks' own COP0 reads (remainder) | 0.013 | 0.017 | 0.007 | 0.006 |
| bags submitted / rejected at the bbox | 226.5 / 114 | 270 / 129 | 167.5 / 134 | 211 / 173.5 |
| packet flushes | 120 | 142.5 | 44 | 48 |

**The beginFrame block is 0.577 ms in every pose**, to the millisecond's third
digit. It is a fixed per-frame cost, not something that scales with the view,
and at the outer-road poses it is half the gap.

### What that falsifies, and what it names

Three of the suspects are dead:

- **The per-object tests are 0.149 ms** — visibility, draw distance, the coarse
  merged-box reject, the split band, the impostor decision and the mesh-LOD
  tier, run for **every** object in the scene, are 1.0% of the Objects loop and
  1.0% of render submission. Half the bags that do reach the pipeline are
  rejected by its own bounding-box test (114 of 226.5), which is the coarse
  reject working.
- **The thirteen `TYRA_ASSERT`s are 0.072 ms.** They really do execute in a
  release game — no game build defines NDEBUG — and they are still nothing.
- **`ensureProgramSet` is 0.010 ms.** The program-name comparisons are noise.

Four things the numbers name instead, in order of size:

- **`renderVehicleWheels` is 2.962 ms, a fifth of render submission, and
  1.970 ms of that is the generated game rebuilding every wheel vertex on the EE
  every frame.** Four wheels per car, nine multiplies and three adds per vertex,
  `push_back` into a `std::vector` that is cleared and refilled each frame. It
  is bigger than the terrain, bigger than the static batches, bigger than the
  procedural chunks, and it scales with cars rather than with triangles. The
  remaining 0.992 ms is **one bag**, and it is expensive for a reason the
  attribution makes visible: the bag is handed `bboxVersion = ++g_bboxStamp`
  unconditionally, which invalidates its package bounding boxes (so `bounds`
  recomputes them) and its retained command blocks (so `dispatch` rebuilds
  them) on every single frame, by construction.
- **Package creation and classification is 3.346 ms**, 56% of `dispatch` and the
  largest single unopened box left in the frame. Nothing here looks inside it —
  "Round two" below does, and the first thing it finds is that **the name is
  half wrong**: the wholly-visible route, which is about half the submitted
  bags, runs no classification at all.
- **The shared reflection probe is the whole of renderScene's head** — 0.820 ms
  of 0.820. The split band, the sky retint, the env basis and the camera feed
  together are 0.001 ms. The probe refreshes every second frame, so on the
  frames it runs it is about 1.64 ms.
- **`sendObjectData` is 46% of `prepare`** (0.507 of 1.106). The per-bag uniform
  packet — the MVP, the options block, the TEST and ALPHA registers, the texture
  binding — is the single largest thing in that bracket, and inside a submission
  batch the MVP is written a float at a time because the unpack helper would
  otherwise emit a DMA reference to caller memory.

### What is still not attributed, and what it is not

Of the 4.725 ms of game-side EE work inside renderScene, this instrument names
**1.970** (the wheel rebake), **0.148** (the per-object tests) and **0.004** (the
terrain chunk streaming) outright. The remaining **~2.6 ms** is inside the
static-batch, procedural, light-effects, reflection-probe, sky and particle
phases, mixed with the submission those phases perform, and **nothing here
separates the two** — that is one more subset bracket per phase and it is the
obvious next step.

What that 2.6 ms is **not**: it is not in the static pipeline (the whole of
`StaPipCore::render` is measured at 9.228 and accounted for to 0.020 ms), it is
not post-processing (0.000), it is not the HUD (0.065), it is not the per-object
tests (0.148), and it is not `beginFrame` (the scene block minus renderScene is
0.577 and is identical in all four poses, so it is a fixed per-frame cost rather
than anything that scales with the view).

### The night poses say something the day poses cannot

| bucket | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| submit | 14.595 | 18.655 | 4.854 | 6.468 |
| `prepare` → texture (residency, `useTexture`, wrap) | 0.199 | **0.853** | 0.027 | **0.333** |
| `StaPipCore::render` tail (the clamped-wrap restore) | 0.100 | **0.877** | 0.001 | **0.296** |
| `prepare` → dynamic light pick | 0.077 | 0.348 | 0.021 | 0.072 |
| light pools + projected & blob shadows + beams | 1.302 | 3.700 | 0.057 | 1.005 |
| the shared reflection probe | 0.820 | 1.212 | 0.495 | 0.523 |

**A bag whose texture does not wrap REPEAT costs two pipeline drains**, one
before the draw (`sync.align3D()` + `setTextureWrap`, charged to `spTex`) and
one after it (the restore, charged to `spTail`). At garage night that is
**1.730 ms of 18.655**, at outer night 0.629 of 6.468, and in daylight 0.299 and
0.028. It is the only term in the whole attribution that moves by roughly 9x
between day and night, which is what makes it identifiable at all — the night
scene draws the lamps' projected shadows and pools, and those sample clamped
render targets. The engine's own comment says an ordinary mesh "pays one pointer
comparison"; that is true, and it is the bags which *do* clamp that pay for a
serialisation of the whole pipeline, twice each.

## Two things the release ELF still carries, found while reading this

Neither is the subject of this page and neither is fixed here, because changing
what a game logs changes what it is being measured doing. Both are the same
class of defect as the VRAM census, and both are live in the **release** fixture
these numbers were taken on:

- **`VRAMSTAT` still prints every 120 frames in a release build.** The census
  underneath it (`VRAMRES` / `VRAMEVICT`) was correctly moved behind
  `TYRA_VRAM_CENSUS`, but the `#endif` closes *above* the summary line, and the
  comment right over it says the line "compiles to almost nothing in release
  (TYRA_LOG is a no-op under NDEBUG)" — which the same file contradicts sixty
  lines earlier. Fifteen host-filesystem writes in a 1 440-frame run here.
- **`STAPIPRET` prints every 300 frames in a release build**, for the same
  reason: its gate is `#if TYRA_STAPIP_RETAINED_COMMANDS && !defined(NDEBUG)`,
  and `!defined(NDEBUG)` is always true in a game build.

They are small — five and fifteen lines per run rather than the census's
thousand — but "a debug channel a release build still pays for" is exactly what
docs/devkit.md forbids, and `--audit-release` does not catch either, because
neither is devkit code. Two things make them worth a line here rather than a
shrug. Both fire from `endFrame`, so the spike lands in the frame-cost CSV's
`finish` bucket rather than in `submit` — which is where anyone reading these
tables would look for it. And **on a ps2link deploy a `host:` write is a network
round trip**, so what costs microseconds in the emulator is a different size on
the console the hardware numbers come from; `benchmark-district.py`'s own
contract is "no sample-time host writes", and at 120- and 300-frame periods
these land inside every 240-row pose.

They are deliberately **not fixed in the same commit as this page**: every number
above was taken with them present, and quietly changing the code after measuring
it breaks the correspondence between the two. They are in
[the backlog](backlog.md) instead.

## Round two: inside `bounds`, and inside the package box

The two buckets the table above left as the largest unopened boxes have now
been split the same way. Both close, and **both answers are somewhere nobody
had looked** — including the place this page itself nominated.

`bounds` had already been attacked twice, plausibly and carefully, for a
combined **2%**: a branchless `frustumCheckAABB` (4.132 → 4.073 ms on the
console) and a compacted `partBounds` stride (4.073 → 4.051). The remaining
suspect was the bbox cacher's hashed lookup, its entry bookkeeping and its
250-frame expiry. **It is none of those.**

### `bounds`, garage day

Instrumented arm, so every figure carries the hooks' own 6.3% (below).

| bucket | ms | of `bounds` |
| --- | ---: | ---: |
| **`bounds` (shipped bracket)** | **2.055** | 100% |
| ├ package-size derivation (`bdSize`) | 0.577 | 28% |
| │&nbsp;&nbsp;├ resolving the VU1 program | 0.072 | 3.5% |
| │&nbsp;&nbsp;├ `getMaxVertCount`'s three integer divisions | 0.057 | 2.8% |
| │&nbsp;&nbsp;└ **the `packageSize` pin + `setMaxVertCount`** | **0.448** | **22%** |
| ├ **the bbox cacher** (`bdCache`) | **0.737** | **36%** |
| │&nbsp;&nbsp;├ **recomputing invalidated boxes** | **0.618** | **30%** |
| │&nbsp;&nbsp;└ *the hashed lookup, all 226.5 of them* | *0.118* | *5.7%* |
| ├ the transform cache's two 64-byte `memcmp`s | 0.341 | 17% |
| ├ the main bounding-box AABB test | 0.215 | 10% |
| ├ the six object-space frustum planes | 0.135 | 6.6% |
| └ *residual* | *0.050* | *2.4%* |

and the cacher's own counters, which are what make that table readable:

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| lookups (= bags with precise culling) | 226.5 | 267 | 167.5 | 208 |
| hits | 216.0 | 232.0 | 162.0 | 178.5 |
| **recalculations** | **10.5** | **35.0** | **5.5** | **29.5** |
| fresh allocations | **0** | **0** | **0** | **0** |
| entries held (250-frame retention) | 220 | 256 | 225 | 220 |
| probes per lookup | 1.44 | 1.51 | 1.50 | 1.45 |
| the per-frame expiry scan | 0.011 ms | 0.013 | 0.019 | 0.011 |

**The cacher is exonerated and the caller is not.** The hash is doing its job
(1.44 probes per lookup against a 256-bucket index holding 220 entries), the
250-frame expiry scan is 0.011 ms, and **nothing allocates at all** — zero
fresh entries in every pose. All 226.5 lookups together cost **0.118 ms**,
0.52 µs each. What costs 0.618 ms is **10.5 forced recalculations at 58.8 µs
each**: bags whose caller bumped `bboxVersion`, so `recalculate()` rescans the
whole vertex buffer and rebuilds every part box.

That is the `renderVehicleWheels` finding above, priced from the other side.
The bag is handed `bboxVersion = ++g_bboxStamp` unconditionally every frame, so
on top of its 1.970 ms of EE rebake it also spends **0.618 ms of `bounds`**
rebuilding bounding boxes for geometry it could have declared unchanged. The
fix is a `bboxVersion` contract change in the generated game, not in the engine
— the engine's part already works. Note the shape of the evidence: garage day
spends **5x more per lookup** than outer day while the probe depth is identical
(1.44 against 1.50), which is a recompute signature and not a lookup one.

### The package box, garage day

`dispatch` minus the four nested buckets — the figure this page called "the
largest single unopened box left in the frame" at 3.346 ms — is 3.206 here, and
it is **not one thing, and not mostly what its name says**:

| | ms | note |
| --- | ---: | --- |
| **`dispatch` (shipped bracket)** | **5.835** | |
| the four nested buckets (packet, send, VIF1 wait, GIF wait) | 2.629 | |
| **= "package creation and classification"** | **3.206** | |
| ├ **classification** (`checkFrustum`, EXCLUSIVE) | **1.401** | 572.5 packages, 2.45 µs each |
| ├ building the package descriptors | 0.387 | |
| ├ the wholly-visible direct fill-and-cull loop | 1.041 | *inclusive of its flushes* |
| ├ the partial route's per-package submission | 1.058 | *inclusive* |
| ├ the end-of-bag `flushBuffers` | 1.757 | *inclusive* |
| ├ the retained-command key test | 0.169 | |
| └ *residual (the route decision)* | *0.023* | |

Three things fall out of it. **Half the submitted bags never reach a
classification at all** — 53.5 take the wholly-visible direct route against
59.0 partial — so for those bags the box is the fill-and-cull loop and the name
is simply wrong. **The classification is arithmetic, not the walk**: the merge
loop averages **2.31 parts per package**, which is why compacting its stride
bought 0.5%, and 269 of 572.5 packages additionally run the eight-plane clip
mask (0.47 per package). And the three inclusive brackets overlap the nested
2.629, so they may not be summed with it.

### What the numbers named, and what was done about it

**The `packageSize` pin plus `setMaxVertCount` was 0.448 ms — bigger than the
program lookup and the three integer divisions put together, and it is the part
that looked like three stores.** `StaPipQBufferRenderer::setMaxVertCount` fans
one `u32` out to all **32** qbuffers and the clipper, once per bag: 33
out-of-line stores, 7 474 per garage-day frame, to write the number that was
already there. The package size is a property of the PROGRAM CLASS, so
consecutive bags of one class — most of a frame — ask for the size that is
already set.

It returns early now when the value has not moved. Every leaf setter is a pure
store, so the skip is exact; the one thing it depends on is that the cached
value cannot outlive the buffers, so `allocateOnUse()` resets it to 0 (never a
legal package size) and `StaPipQBuffer`'s constructor now initialises its own
copy instead of leaving it uninitialised.

Shipped configuration, counters compiled out, two boots per arm:

| pose | `bounds` before | after | delta | per bag |
| --- | ---: | ---: | ---: | ---: |
| garage day | 1.933 / 1.933 | 1.593 / 1.593 | **−0.340** (−17.6%) | 1.50 µs |
| garage night | 2.368 / 2.371 | 1.994 / 1.998 | **−0.374** (−15.8%) | 1.38 µs |
| outer day | 0.951 / 0.950 | 0.699 / 0.699 | **−0.252** (−26.5%) | 1.50 µs |
| outer night | 1.331 / 1.333 | 1.039 / 1.039 | **−0.293** (−22.0%) | 1.39 µs |

**Same-ELF repeatability on `bounds` is 0.000–0.004 ms**, so the delta is two
orders of magnitude above the floor. `prepare`, `dispatch`, `finish` and every
count — bags submitted, bags culled, packages created, packet flushes — are
unchanged, and **twelve captures across both day poses and both arms are
byte-identical**. That is about **ten times what the two previous attacks on
this bucket achieved between them**, and the reason is not cleverness: it is
that the previous two were aimed by a plausible story and this one was aimed by
a measurement.

The two the numbers name and that were NOT acted on here, deliberately:
**0.618 ms of forced bbox recalculation**, which is a `bboxVersion` contract
question in the generated game's wheel path and belongs with whoever owns it;
and **1.401 ms of package classification**, which is 6-plus-8 planes of honest
arithmetic over 572.5 packages with a 2.31-part merge walk — there is no
obvious redundancy left in it, and saying so is a result.

### What these hooks cost

| bracket | counters out | counters in | hook cost |
| --- | ---: | ---: | ---: |
| `bounds` | 1.933 | 2.055 | **+0.122** |
| `prepare` | 1.089 | 1.113 | +0.025 |
| `dispatch` | 5.773 | 5.835 | **+0.062** |
| `submit` | 14.457 | 14.609 | +0.152 |

Unlike the first round's engine half, **this one is measurable**: the `bounds`
children over-report by **6.3%** and the `dispatch` children by 1.1%, so
subtract that before quoting a child as a share of a shipped bracket. `bounds`
carries seven bracket pairs per bag — fourteen `mfc0` reads — which prices one
COP0 read at about **11 cycles**. The raw rows, the arms and the capture
comparison are archived in
[authoring/bounds-attribution-2026-09-16](../examples/vehicle-playground/authoring/bounds-attribution-2026-09-16/README.md).

## Round three: the package size is at its ceiling, and the ceiling is 81

Round two left almost every term of the package box scaling with the number of
packages: classification at 2.45 µs each, the descriptor construction, both
submission loops and the flush count. The garage-day frame is cut into **572.5
packages**, and static geometry ships as triangle strips chopped into runs of
exactly 72 vertices with `StaPipBag::packageSize` pinned to that, so a run *is*
a package. The obvious question follows: **if a package held twice as many
vertices there would be half as many packages.** So what sets 72, and can it be
raised?

**It cannot. 72 is 96% of what the binding program class can derive, and 89% of
what the whole of VU1 data memory allows.** A doubling is not expensive, it is
arithmetically impossible.

### Where 72 comes from

Two functions, and no other input.
`StaPipQBufferRenderer::setDoubleBuffer` splits the memory between
`VU1_STAPIP_LAST_ITEM_ADDR + 1` (22, above the per-mesh constants) and
`VU1_STAPIP_DBUFFER_END` (944, the clipping scratch floor) into two halves:
`(944 − 22) / 2 − 1` = **460 quadwords**. `StaPipVU1Program::getMaxVertCount`
takes nine of those for the GIF tag block and divides the remaining **451** by
the per-vertex footprint, which is `elementsPerVertex + reglistCount` — what the
EE uploads plus what the program writes — then rounds down to a multiple of 9.

Per class, from each program's own constructor arguments, verified against a
native harness that runs both functions verbatim — it is archived with this
round in
[authoring/package-ceiling-2026-09-16](../examples/vehicle-playground/authoring/package-ceiling-2026-09-16/README.md),
and it is the check to re-run after any edit to either function:

| class | uploads | writes | qw/vertex | 451 / that | after the /9 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `cull_c` + per-vertex colours | 2 | 2 | 4 | 112 | 108 |
| `cull_c` + one colour | 1 | 2 | 3 | 150 | 144 |
| `cull_d` + one colour | 2 | 2 | 4 | 112 | 108 |
| **`cull_tc` / `cull_tce` + per-vertex colours** | **3** | **3** | **6** | **75** | **72** |
| `cull_tc` + one colour | 2 | 3 | 5 | 90 | 90 |
| **`cull_td` + one colour** | **3** | **3** | **6** | **75** | **72** |

`cull_td` with per-vertex colours would derive 63, and it is unreachable:
`StaPipCore::render` asserts that a bag never carries both `color->many` and
`lighting`. So the floor over every reachable combination is **72**, which is
what `TerrainGame::minPackageSize()` computes and what `meshstrip::kRun` is
baked to.

### The per-class pin is not costing this frame anything

"72 is the smallest package any static program class derives" reads like a tax
the base pass pays for its companions. **Here it is not**: the district's static
parts carry per-vertex colours on every pass (`part.colorBag->many`,
`envColorBag->many`, `aoColorBag->many`, `emisColorBag->many`) and all of them
are textured, so base, AO, emissive and env all derive 72 through `cull_tc` /
`cull_tce`. The one pass that is different — the dynamically lit bag, which sets
`litColorBag->many = nullptr` and carries directional lights — reaches 72 the
other way, through `cull_td` with a single colour. Two independent classes,
the same number. Roads and terrain chunks are textured with per-vertex colours
too. **So a per-class run length would buy this frame nothing**: there is no bag
in it whose class could take a longer one.

### What the 944 floor is actually worth

The clipping scratch occupies 944..1023 — 80 quadwords, of which 72 are used
(12 for the six clip planes, 30 each for the two Sutherland–Hodgman polygons).
Sweeping `VU1_STAPIP_DBUFFER_END` through the two functions:

| `DBUFFER_END` | half | max vertices | |
| ---: | ---: | ---: | --- |
| 906 | 441 | 72 | the smallest value that still yields 72 |
| **944** | **460** | **72** | **shipping** |
| 1004 | 490 | 72 | |
| 1014 | 495 | **81** | the smallest value that yields 81 |
| 1024 | 500 | 81 | the whole memory, scratch deleted |

Three things fall out of that column. **The shipping floor has 38 quadwords of
slack that buy nothing** — anything from 906 to 1013 derives the same 72. **The
absolute ceiling is 81**, reached only by reclaiming essentially all 80
quadwords; 1014 leaves ten, and the plane table alone is twelve. And **moving
the plane table down into the constants block buys exactly zero**, which is an
identity rather than a near miss: the double buffer pays twice for a quadword
below it and gains twice for one above it, so `(1024 − 34) / 2` and
`(1012 − 22) / 2` are both 495.

### Why a doubling is impossible, not merely hard

At six quadwords per vertex, one double-buffer half holding N vertices needs
`9 + 6N` quadwords and the buffer needs twice that, on top of the 22 quadwords
of per-mesh constants:

| vertices per package | double buffer | total of 1024 | |
| ---: | ---: | ---: | --- |
| 72 | 884 | 906 | fits |
| 81 | 992 | 1014 | fits |
| 90 | 1100 | 1122 | **does not fit** |
| 144 | 1748 | **1770** | **does not fit** |

**A 144-vertex package wants 1.73x the whole of VU1 data memory.** Even 90 — the
next step up the /9 ladder — is 98 quadwords past the end. The double buffer is
exactly the factor of two that makes it impossible (144 vertices fit in a
*single* buffer at 895 quadwords), and giving that up trades the DMA/compute
overlap the pipeline is built on.

**So the only lever on the package count is the six quadwords per vertex**, and
that is a microprogram ABI change: the three the EE uploads (position, ST,
colour) and the three the program writes (ST, RGBAQ, XYZF2 — the GS reglist for
a textured, per-vertex-coloured primitive, which cannot be shortened while the
surface is both). Note what this does *not* argue for: the September 15 probe
that added 16 bytes per vertex to the DMA payload moved the garage-day VIF1 wait
by 0.067 ms, so **shrinking the per-vertex footprint would pay by fitting more
vertices per package and cutting the EE's per-package work, never by moving
fewer bytes.** Measure the packages, not the bandwidth.

### The measured baseline, so a later arm has something to beat

Arm `attrib-after` of the round-two archive, 240 warmed rows per pose:

| | garage day | garage night | outer day | outer night |
| --- | ---: | ---: | ---: | ---: |
| **packages** | **572.5** | 599.5 | 224.5 | 230.5 |
| packet flushes | 120.0 | 142.5 | 44.0 | 48.0 |
| GS primitives | 40 502 | 41 176 | 16 386 | 16 720 |
| primitives per package | 70.75 | 68.68 | 72.99 | 72.54 |
| bags direct / partial | 53.5 / 59.0 | 76.0 / 65.0 | 13.0 / 20.5 | 16.0 / 21.5 |
| implied vertices at 72 | 41 220 | 43 164 | 16 164 | 16 596 |

A full 72-vertex strip run is 70 GS primitives and a 72-vertex *list* package is
24. **Every pose reads about 70**, so essentially every package in the frame is
a full strip run — the count really is `vertices / 72` and nothing is being lost
to short tails. (The runs are padded at bake time, and the two counters
`recordGuardBandPackage` / `recordOutsideBag` over-report for strips, which is
why the number sits slightly above 70 rather than on it.)

### The two things that would move it, costed rather than taken

Neither is implemented here, because both are larger than their payoff justifies
on their own and this round was asked for a bound, not for a change.

- **Relax the multiple-of-9 rounding to a multiple of 3: 72 → 75, −4.0% of the
  packages, with no memory change at all.** The 9 is documented as "the /3
  subpackage split divisible by 3 again", and the two places that actually cut
  triangles already round for themselves (`clipPackageSize()` is `(size / 3) * 3`
  and the qbuffer chunk is `(maxVertCount / 3) * 3`); `maxVertCount / 3` survives
  elsewhere only as the 1/3-bbox granularity, which is conservative by
  construction. It is cheap in the engine and **not** cheap outside it: the runs
  are baked, so `meshstrip::kRun` and the road/terrain run constants move with
  it and every example project has to be re-baked and pixel-compared. 4% of the
  per-package terms is not worth that on its own; fold it into the next change
  that re-bakes anyway.
- **Reclaim the clipping scratch: 72 → 81, −11.1% of the packages.** This needs
  `DBUFFER_END` at 1014 or above, i.e. both Sutherland–Hodgman polygons *and*
  the plane table out of absolute addresses. There is room for them: a clip
  buffer's layout is **dynamic**, not sized by `maxVertCount` — the program
  computes `stqData = vertexData + vertexCount` and its destination from the
  real count — so a 12-vertex clip package occupies 2 + 36 + 9 = 47 quadwords of
  its 461-quadword half, leaving 414 against a worst-case 252-quadword fan-out
  (4 triangles x 7 output triangles x 3 vertices x 3 quadwords). **162 quadwords
  spare, against the 72 the scratch uses**, and the margin survives at 81
  (15-vertex packages, 130 spare). What it costs is the real objection: three
  clip images rewritten to xtop-relative scratch addressing plus their `vugen.cpp`
  twins, the plane upload moved from per-mesh to per-clip-package in the packet
  writer and the retained-command key, VI register pressure in the hottest loop
  in the pipeline (`clip_tc` is 269 cycles per triangle), `kRun` re-baked to 81,
  and a full console A/B. It is a session of its own and it is in
  [the backlog](backlog.md).

## Round four: the ceiling moves to 75, and the clipper pays for it

Round three costed two ways past 72 and took neither. This round takes the
cheap one — **the rounding step, 72 → 75, −4.0% of the packages** — and it
turned out not to be free in the way round three assumed. The expensive one
(reclaiming the clipping scratch, 81) is **not** done, and the reasons are at
the end of this section.

### What changed

`StaPipVU1Program::getMaxVertCount` rounded its result down to a multiple of
**nine** so that the 1/3 subpackage split came out divisible by three as well.
That second condition is not required by any live path, which round three
argued and this round verified by reading every consumer of `maxVertCount`:

- `StaPipCore::clipPackageSize()` is `(maxVertCount / clipDivisor / 3) * 3` —
  it rounds for itself;
- the clip-drain chunk in `StaPipQBufferRenderer` is `(maxVertCount / 3) * 3` —
  it rounds for itself, and equals `maxVertCount` whenever that is a multiple
  of 3;
- `StaPipBagPackagesBBox` uses `maxVertCount / 3` as a **bbox granularity**,
  with a ceiling division and a remainder part, so it is conservative for any
  value;
- `StaPipClipper` and `StaPipQBuffer` only ever *assert* `size <= maxVertCount / 3`.

So the step became `(res / 3) * 3`, and `StaPipCore::getMaxVertCountByBag`'s
pin became `(packageSize / 3) * 3` to match. `minPackageSize()` over every
combination `StaPipCore::render` can reach goes **72 → 75**, and
`meshstrip::kRun`, `roadgen::kStripRun` and the generated game's road and
terrain run constants go with it, because the runs *are* the packages.

### What round three did not price: the clip buffer moves too

`maxVertCount` feeds `clipPackageSize()`, so raising the ceiling makes clip
packages bigger, and a clip package has to fit its double-buffer half together
with its entire Sutherland–Hodgman fan-out. Round three did this calculation
for one class with the constructor's `elementsPerVertex` / `reglistCount`
pair. **That pair is the sizing budget, not the layout**: the `d` and `td`
classes spend part of it on an uploaded normal stream rather than on output
registers, and using it here reports false overruns on classes that ship today.

Done properly — uploaded streams, the real per-output-vertex store count and
the real GIF tag block, all read off the `.vclpp` sources — the tightest
**reachable** margin over every class is:

| | tightest clip-buffer margin |
| --- | ---: |
| before this round (rounding /9, `clipDivisor` 5) | 35 quadwords |
| rounding /3 alone, `clipDivisor` still 5 | **1 quadword** |
| shipped (rounding /3, `clipDivisor` 6) | **91 quadwords** |

The one-quadword row is `clip_c` with a single colour: relaxing the rounding
takes it from 144 to 150 vertices, so its clip package goes 27 → 30, so it
occupies 459 of 460 quadwords. It *fits* — the 7-output-triangles-per-input
bound is exact, not a safety factor — but one quadword of headroom on the one
path PCSX2 cannot verify is not a thing to ship. `clipDivisor` therefore went
**5 → 6** in the same commit.

**That is close to free where this frame lives.** The three classes a textured
scene actually takes — `cull_tc` and `cull_tce` with per-vertex colours,
`cull_td` with one — derive a clip package of **12 under both** the old
`/9`-with-1/5 and the new `/3`-with-1/6. Their clip route is unchanged; only
the cull route's packages get 4% fewer. The classes that do get smaller clip
packages (untextured, or single-colour textured) are the ones that gained the
margin.

The derivation is runnable, per class, and is the check to re-run after any
edit to either function or to a clip image's layout:
[authoring/package-ceiling-75-2026-09-16](../examples/vehicle-playground/authoring/package-ceiling-75-2026-09-16/README.md).

### What it measured

Two arms of the frozen-camera Motor District fixture
(`authoring/benchmark-district.py`, parked traffic, player pinned), both built
with `TYRA_FRAME_PROFILE 1`, each regenerated by the editor that built it.
**The ELFs hash differently** (`E4F2D59D…` against `F1BDF0E1…`) and the marker
that must differ does — `stripRun = 75u` against `72u` in each arm's own
`terrain_game.cpp` — so the two arms are two arms. PCSX2, `FTCLIP` window
totals over 50 frames, which repeated **identically across every window of
each run**.

| pose | cull packages | clip packages | flushes | verts/frame |
| --- | ---: | ---: | ---: | ---: |
| garage day | 40 925 → **38 750** (−5.3%) | 1 525 → 1 825 | 6 000 → 6 000 | 55 602 → 55 836 |
| garage night | 43 350 → **41 325** (−4.7%) | 1 875 → 2 125 | 7 125 → 7 125 | 57 624 → 57 840 |
| outer day | 12 075 → **11 050** (−8.5%) | 1 475 → 1 650 | 2 200 → 2 150 | 18 057 → 17 695 |
| outer night | 12 825 → **11 800** (−8.0%) | 1 575 → 1 750 | 2 400 → 2 350 | 19 059 → 18 691 |

Counting both routes, the packages a pose submits fall **3.9% to 6.3%**. The
cull route loses more than the 4.0% the arithmetic predicts because a longer
run also pads less often; the clip route gains 11–20% because `clipDivisor` 6
cuts a crossing package into smaller subpackages, which is the priced cost of
the margin. `flush` counts BAGS, so it barely moves, exactly as documented.

The procedural emitters report their own package counts at scene load, and
they are the cleanest read because the **surface** triangle count is printed
next to them and must be equal in both arms:

| | before | after | |
| --- | ---: | ---: | --- |
| `ROADSTRIP` packages | 526 | **470** | **−10.6%** |
| `ROADSTRIP` triangles | 31 050 | 31 050 | equal — the arms are the same scene |
| `TERRAINSTRIP` packages per chunk | 9 | **8** | −11.1% |
| `TERRAINSTRIP` triangles per chunk | 512 | 512 | equal |

Do **not** read the `FTCLIP` triangle halves across these arms: a 75-vertex
strip run reports 73 triangles including degenerate joins and padding against
a 72-vertex run's 70, so they are not comparable across a run-length change.
`verts` is, and it moves less than 2% in either direction.

### The picture is byte-identical

Three captures per arm per pose, `--capture-frame`, on the two poses that hold
still (the night poses twinkle — stars and lamp flicker — so repeats there are
**not** byte-identical and nothing may be read from them, which is why they are
not used here).

| pose | repeats within an arm | before vs after |
| --- | --- | --- |
| garage day | byte-identical, both arms | **byte-identical** (`415F970F…`) |
| outer road day | byte-identical, both arms | **byte-identical** (`9839B5B6…`) |

Twelve captures, two distinct hashes, one per pose. That is the result a
package-boundary change should produce: the same picture out of a different
number of packages. The garage-day pose carries 1 825 clip packages per window
and a non-zero `sexp` (stripped packages the clipper expanded back into a
list), so the clip route — the part this change actually moves — is exercised
in the pose that came out identical.

### Why every consumer was already safe against a mismatched run

A `.tmdl` carries its own `stripRun` per part (format version 4), and every
consumer **guards** rather than trusts: static models take the strip only when
`src.stripRun <= minPackageSize()`, and the road and terrain emitters both test
`minPackageSize() >= stripRun` before choosing the strip representation. So an
older `.tmdl` on this engine is correct and merely slower, and a 75-run
`.tmdl` on an older engine falls back to the triangle list rather than letting
the package-size clamp move package boundaries off the run boundaries — which
is the one way a baked run can render wrong. No format version bump was needed;
the run length was already data.

### What was NOT done

**The clipping scratch was not reclaimed and the ceiling is not 81.** The two
levers do not add up — at full reclamation the rounding step is irrelevant,
because a 500-quadword half derives 81 under both `/9` and `/3` — so this round
bought the 4% and left the remaining 8% on the table. Reaching 81 still needs
`DBUFFER_END` at 1014 or above, which means the plane table and *both*
Sutherland–Hodgman polygons out of their fixed addresses and into the clip
programs' own buffer half, in five clip images plus their `src/vugen.cpp`
twins, with the plane upload moved to per-clip-package in the packet writer and
in the retained-command key. It is still in [the backlog](backlog.md), with one
correction: re-do the margin arithmetic with this round's harness before
starting, because the scratch would then be paid for out of the same half it is
moved into, and round three's figure does not account for that.

## Limits

These are parked poses of one scene. The included buckets overlap by
construction — every bracket contains its children — so nothing here may be
summed across levels or inverted into a frame rate. `vif_wait_included_ms`
brackets `dma_channel_wait(VIF1)` and is not a measurement of VU1 execution.
Every number on this page below the recipe was taken in **PCSX2**, which
emulates no EE data cache: it over-states work that computes and under-states
work that reads cold memory, so the SHARES are usable and the milliseconds are
directional until a console repeats them.

That difference is visible in the totals and worth stating plainly. The physical
console's garage-day render submission is 29.653 ms with 7.5 in no bucket (25%);
the same view here is 14.595 with 5.552 in no bucket (38%). The emulator is
about twice as fast overall, and it is the three *pipeline* brackets that shrink
most — which is what a missing data cache predicts, because `bounds`, the
package classification and the retained-command replay are the memory-bound
parts. So expect the hardware's gap to be the same *terms* in the same order,
with the pipeline share larger around them. **The one claim that is independent
of the cache is structural**: `submit` always contained the post-process passes,
the HUD and the whole of the generated game's renderScene, and the three
brackets never did.

The fixture is also not the one the 29.653 ms figure came from: it was
regenerated by the editor under test, so its baked models differ (28 of 105
asset hashes moved) and its geometry is the stripped representation — 40 502 GS
primitives for the same 25 650-surface view. Arms are comparable with each
other, not with the September-15 table.

**Provenance, so a re-run that disagrees can be explained rather than argued
about:** every arm was built from `vehicles` at `ab4a34d0`. The branchless
`CoreBBox::frustumCheckAABB` landed one commit later, and it is inside the
`bounds` bracket — so a repeat on a current tree should read a smaller `bounds`
than the 1.966 above, and the attribution around it should not move at all.
