# The content version: a second stamp, owned by a type rather than by a rule

`StaPipBag::contentVersion` is a per-stream stamp that says "the bytes in this
array changed", separate from `bboxVersion`, which says "the bounding box
moved". The generated game carries it structurally: every array a bag draws
from is a `BagArray<T>`, whose every mutating member stamps and whose `data()`
is const, so a write that forgets to invalidate the renderer's caches does not
compile. This page is why that is a type instead of a rule, and what it does
not cover.

## The defect it closes

[The baked VIF stream](baked-vif-stream.md) inlines a bag's vertex payload into
a pre-built VIF block and replays it with one DMA `REF` tag. Inlining is what
buys the tag count, and inlining is what creates the exposure: if a caller
rewrites the array afterwards, the block is stale and nothing in the cache key
can see it.

The key held each array's **pointer** plus `bboxVersion` — and `bboxVersion` is
a statement about the bounding BOX. `StapipBagBBoxesCacher` is its only other
consumer. So a caller that re-shades per-vertex colours **in place** changed
what the block must contain without touching anything the key could see.

The adversarial verify arm (`TYRA_STAPIP_BAKED_VERIFY`) caught it **1 438
times** on the Motor District, every one of them the same shape: the colour-only
program class, identical block length, and the first differing quadword landing
on the first colour quadword. It fired only while the camera moved, so the
parked fixture and both hash legs of the acceptance gate agreed — the picture
and the counters were clean and the renderer was wrong.

That is the whole reason the feature shipped at 0 for a round.

## Why a type and not a rule

The obvious fix is a contract: "bump a version whenever you rewrite any of a
bag's arrays, not just its positions." The question that decided the design was
how many places would owe that contract, and who writes them.

The census, exact, over `src/templates.cpp` (criterion: any statement mutating
an array whose pointer is assigned to `StaPipBag::vertices`,
`StaPipColorBag::many`, `StaPipTextureBag::coordinates` or
`StaPipLightingBag::normals`, counted only inside the `R"(...)"` raw strings
that become game code):

| | |
| --- | ---: |
| write sites into a bag-backing array | **280** |
| — of them indexed assignments | 79 |
| — `push_back` / `emplace_back` | 124 |
| — `assign` / `resize` / `clear` | 75 |
| — `memcpy` / `std::copy` into one | 2 |
| generated functions they live in | **42** |
| array declarations behind them | **108** (46 + 46 + 16) |
| sites that bumped `bboxVersion` before this change | 26 |

**Every one of the 280 is generator-emitted**, and that turned out to be the
load-bearing fact. There are **zero** in other editor sources, **zero** in
checked-in example game sources, and **zero** reachable from user-authored code:
`ScriptContext` carries no geometry pointer, `TerrainGame::objectGeometry` is
private, and `flow_graph.gen.cpp` does not include `terrain_game.hpp`. A user's
route to changing geometry is `ctx.objects[i].dirty = true`, which runs
`rebuildObjectGeometry`, which already stamps.

But generator-emitted here does **not** mean computed. Every one of those sites
is fixed template text copied verbatim into every project — no site is assembled
from project data. So the generator cannot *infer* a bump, and the obligation
would fall on whoever next edits `templates.cpp`. That cuts the right way
twice: the population is **closed** (nothing outside one file can add to it),
and a change made once to that text is enforced by the **game's own C++
compiler**, on every project, forever.

Hence a type. `BagArray<T>` (`inc/bag_array.gen.hpp`, one generated header
included by both game-header templates) owns the storage, and:

- `data()` is **const**. No public member hands out a `T*`.
- The four `bind()` overloads are the only way bytes reach a bag, and each aims
  the bag's stream pointer **and** its `contentVersion` in one call — so a bound
  array cannot be bound without its stamp.
- Every mutating member stamps. Non-const `operator[]`, `back()` and
  `begin()/end()` stamp conservatively: asking for a mutable reference is taken
  as intent to write.
- `span(first, n)` is the one writable window, for the handful of callers that
  fill a fixed slot by pointer, and it stamps once on creation rather than
  handing out a raw `T*`.

The 280 write sites did not change. That is the point: they keep their syntax
and gain the obligation.

## The check that makes this a type and not a rule

**A claim about a compile failure is worth nothing until somebody has watched
the compiler refuse.** A wrapper whose `data()` quietly went non-const would
read as enforced and enforce nothing, which is strictly worse than no wrapper.
So the property has a negative test:

```bash
tools/bag-array-enforcement.sh <project>/inc/bag_array.gen.hpp
```

It compiles the real generated header seven times — twice positively (the
sanctioned API must build, and the stamp must MOVE on every mutation) and five
times adversarially, one per escape route: a raw write through `data()`, taking
a writable pointer from it, writing through a const array, handing the storage
to `memcpy` as a destination, and writing through the stamp word. Each must be
**refused**. No PS2 toolchain and no emulator: the header is guarded with
`TYRAX_BAG_ARRAY_NO_TYRA`, which drops only the four `bind()` overloads, so the
storage and every access path is the shipped one.

**And it was falsified before it was believed.** Make `data()` non-const in a
copy of the header and the run goes red on exactly the two cases that property
guards (`passed 5, failed 2`), which is what says the green run means something.

The positive control is not decoration either: without it, a header that failed
to compile at all would "pass" every negative case and the whole run would be
meaningless.

## What it does NOT cover, said out loud

**One engine mechanism writes into an array a live bag points at, and no
generated wrapper can own it.** `SkelInstance::skinParts`
(`vendor/tyra/engine/src/renderer/3d/mesh/dynamic/skel_instance.cpp`) skins LOD 0
**in place** into the mesh frame's own `vertices` and `normals` arrays, which
the engine owns. That one stays a rule.

It is a sound rule, and the reason is worth keeping: skinning moves **positions
and normals**, which is exactly what `bboxVersion` is a statement about, and
`updateAndRenderAnimObjects` already stamps it on `reskinned`. So the exception
is not an unguarded hole — it is the one case the *original* version field
already covers correctly. Deeper LOD tiers write into `SkelInstance`-owned
scratch and never touch a caller's array at all.

Everything else in the static pipeline copies **out of** bag arrays, never into
them: every qbuffer copy path calls `allocateDynamicData()` first, which
re-points at a pool, and `StaPipClipper` copies spot-light colours to a stack
array precisely because "`buffer->colors` points at caller-owned data".

The other two exceptions are not enforceable by anything and are not pretending
to be: `ScriptContext::engine` is a raw `Tyra::Engine*`, and a user may delete a
generated file's ownership marker and take the file over. Both are deliberate
acts that already void stronger invariants than this one.

## Why the retained cache does NOT read it

`StaPipRetainedEntry` deliberately carries no `contentVersion`. The retained
cache stores the **chain** — tags and `REF`s that still name the bag's own
arrays — so a re-written array is followed at DMA time and is always fresh.
Folding the stamp into its key would rebuild its blocks for nothing. The
exposure belongs to the baked stream *because* it inlines the payload, and the
fix belongs there too.

## What it costs

One read-modify-write of a global (`g_contentStamp`) per mutating call. The
write sites are overwhelmingly build-time — terrain chunks, roads, the sky dome
— and the per-frame ones (light pools, beams, wheels, coronas) touch tens of
vertices.

It is deliberately **not** optimised into "stamp once per run of writes". Every
cheap version of that is unsound, because the engine may have read the stamp
between two writes: array A written, A submitted and cached, A written again
with nothing in between is exactly the case a "did the global move?" guard
would miss, and it is the case this whole page exists for.

## Reading the instrument

`STAPIPMISS` now splits the two versions into their own columns, which is what
the separation buys at the readout as well as in the key:

```
STAPIPMISS new=0 bbox=300 content=600 prim=0 streams=0 program=0 size=0
           incomplete=0 over 300 frames; loudest bag count=2280 packages=31
           reason=contentVersion
```

`bbox=N content=0` is a mesh that MOVED. `bbox=0 content=N` is one that was
RE-SHADED. Before the split both read as `bbox`, and "the cache churns" was not
actionable because the field that moved was not named.

## The caller this found, which was not the one it was built for

The contract closed the 1 438 colour-class mismatches on the first run. The arm
then found a **second** caller of a different shape, which is the argument for
running it rather than reasoning about it:

```
STAPIPVERIFY MISMATCH StaPip - Cull - TCE count=2280 packages=31
             cachedQw=7057 freshQw=7057 firstDiffQw=156 ofBlock0Qw=232
             cached qw[156]=43.000,43.000,43.000,1.0606
             fresh  qw[156]=43.012,43.012,43.012,1.0589
```

The vehicle **paint pass** (docs/vehicles.md, "A shiny body") recomputes a
per-vertex fresnel rim and a Blinn-Phong specular from the camera **every
frame**, for ~1 100 vertices of a visible car — and it wrote them by
`const_cast`-ing the bag's own `many` pointer, which bypassed the array
altogether. Three equal RGB lanes drifting by 0.012 and an alpha drifting by
0.002 are what a camera-dependent shade looks like in the block.

It now writes through the `BagArray` the env colour bag is currently aimed at
(selected by `shownLod`, because the LOD tiers re-aim it), via one `span()` for
the whole run so it stamps once rather than 1 100 times. That is the same fix
shape the type is designed to make natural, and the `const_cast` idiom no longer
exists anywhere in the generated game.

## The sampled verify

The exhaustive arm rebuilds **every** block every frame and replays **none** of
them, so it deletes the entire saving by construction. It is a correctness ELF,
like `TYRA_STAPIP_VIFHASH` — fine for an acceptance run, useless as a routine
check. And the failure mode it guards is invisible: stale colours, only while
something moves, no crash and no counter.

`TYRA_STAPIP_BAKED_SAMPLE_VERIFY` is the same comparison priced to ship. It
verifies **one entry per frame, round-robin**, and replays everything else — on
the garage-day frame that is one block of ~360, about 1/360 of the arm's cost —
so a missing invalidation on any bag surfaces within a few seconds of play. It
prints the same `STAPIPVERIFY checked=… failed=…` line with a much smaller
`checked`, so a devkit log reads exactly the way an acceptance log does.

The two arms differ in exactly one predicate, `StaPipBakedStreams::verifying()`:
the exhaustive one answers yes for every entry, the sampled one for this frame's
victim, and with neither compiled in it folds to a compile-time `false` so a
release build carries none of it. Enabling both is a `#error`.

**It is an engine flag (default 0), not a project setting, and that is a
limitation rather than a choice.** `libtyra.a` is archived once per checkout
from engine sources with no per-project flags, so an engine macro cannot follow
a project's devkit profile without giving the engine build its own stamp file —
and doing that badly recreates the "an image swap rebuilt nothing" trap
(`tyra-testing`). Turning it on is a header flip plus a `libtyra` rebuild.
Wiring it to the devkit profile is on [the backlog](backlog.md) with the
mechanism named. Note that `#ifndef NDEBUG` is **not** the devkit gate in this
engine — a game build never defines `NDEBUG`, which is how a census once shipped
live at ~1 ms a frame.

## How this was verified

**The acceptance arm, the one that found the defect.** Motor District under
`--keep-routes` with the traffic **moving**, `TYRA_STAPIP_BAKED_STREAM=1` and
`TYRA_STAPIP_BAKED_VERIFY=1`, `--profile quiet-debug`, PCSX2:

> **169 843 blocks checked, `failed=0`**, over 42 consecutive 300-frame windows
> (~12 600 frames) covering the whole four-pose route.

A `--keep-routes` A/B is impossible — two arms run at different speeds and never
share a frame — which is exactly why this arm needs no control: it compares the
cache against the ordinary writers **inside one run**.

**The counters and the picture, against the control.** Parked fixture, one
project directory, two real builds (ELF SHA-256 `827C5B90…` against
`9385E57D…` — a null result from one ELF would prove nothing), garage-day pose:

| garage day | stream 1 | stream 0 | |
| --- | ---: | ---: | --- |
| `cull` | 33300/1830850 | 33300/1830850 | 0 |
| `clip` | 1100/12550 | 1100/12550 | 0 |
| `guard` | 10700/559650 | 10700/559650 | 0 |
| `out` | 37100 | 37100 | 0 |
| `strip` | 22450 | 22450 | 0 |
| `sexp` | 100 | 100 | 0 |
| `verts` | 48348 | 48348 | 0 |
| **`flush`** (50 frames) | 4950 | 5250 | **−300** |
| **`chainQw`** | 5945 | 8033 | **−26.0%** |

Everything that describes **what** is drawn is identical to the digit; only the
flush cadence and the DMA chain move, which is what the change is for. The
captures are **byte-identical across both arms** — five `--capture-frame`
captures, three in the candidate and two in the control, all one SHA-256, with
the within-arm repeats checked first so the between-arm zero means something.

**And the cache now converges.** `STAPIPMISS` reads all zeros on the parked
outer-road pose and `bbox=300 content=600` at garage day, where the 600 are the
paint pass's two cars — correctly attributed to the new field and naming the
2 280-vertex bag by size.

## What this page does not establish

**No hardware milliseconds for this change.** The −1.287 ms of garage-day `work`
is [the baked stream's own measurement](baked-vif-stream.md), taken on the
physical PS2 before this contract existed. What the contract adds is one global
RMW per mutating call and one extra `u32` in the key; neither has been measured
on hardware, and PCSX2 cannot price either (no EE data cache). A console round
should re-take the four-pose table and confirm the −1.287 survives — and check
`prepare`, which rose 0.315 ms in the original measurement and was hypothesised
to be rebake pressure from a caller stamping a frozen scene. That hypothesis is
now testable: `STAPIPMISS content=` names those callers.

**The sampled arm's cost is an estimate, not a measurement.** "About 1/360" is
arithmetic from the block count, not a timing.

**One pose for the pixel gate.** The night poses have authored lamp flicker and
twinkling stars and never settle, so they are not comparable; only garage day is.
