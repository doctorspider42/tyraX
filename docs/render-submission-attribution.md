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
  largest single unopened box left in the frame. Nothing here looks inside it.
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
