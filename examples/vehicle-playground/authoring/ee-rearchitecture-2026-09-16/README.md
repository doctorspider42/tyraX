# Raw evidence: the EE submission rearchitecture

Item 3 of [docs/ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md)'s
order of work, built behind the gate designed in
[docs/baked-stream-acceptance-gate.md](../../../../docs/baked-stream-acceptance-gate.md).

**No console was touched for anything in this directory.** Everything here is
host arithmetic or PCSX2 counts and pixels. **No number here is a millisecond
and none may be turned into one** — PCSX2 emulates no EE data cache, and these
arms carry the gate, which alone costs about 2.7 MB of folding a frame.

Headline, garage day, held pose, control against candidate:

| | |
| --- | ---: |
| DMA chain quadwords a frame | **8 221 -> 5 784, −29.6 %** |
| packet flushes a frame | **120 -> 109** — the counter the old gate pinned |
| the VIFcode stream VIF1 receives (`ctrl`) | **bit-for-bit identical** |
| the absolute-address uniforms (`uni`) | **bit-for-bit identical** |
| `--capture-frame` sha256, six captures | **one value** |
| routing counts, submitted vertices | identical |
| package blocks rebuilt a frame | 64 (spike) -> **2–3** |

And two negative results, both of which matter more than the table above.

**The adversarial verify mode FAILS, 1438 times, and it is a shipping blocker.**
The cache key holds the colour array's *pointer* and `bboxVersion`, and
`bboxVersion` is about positions — so a caller that re-shades per-vertex colours
in place changes what the block must contain without changing anything the key
can see. Neither the picture nor either hash can see it on a fixture whose
camera is frozen. See "The two adversarial modes".

**The geometry payload is not byte-reproducible between two boots of one ELF**,
though the structure, the uniforms, the copy pools and the picture all are. See
"What the gate could NOT do".

## The gate's self-test, and why it is the first thing in this directory

The gate's whole claim is one sentence: **two chains that hand VIF1 the same
words hash the same, however they were built.** Everything the redesign is
allowed to do rests on it, so it is demonstrated rather than argued — and it
does not need a PS2 to demonstrate, because it is a property of the decoder.

```bash
g++ -O2 -std=c++17 -I stub -I ../../../../vendor/tyra/engine/inc \
    -DTYRA_STAPIP_VIFHASH=1 -o gate-selftest gate-selftest.cpp && ./gate-selftest
```

`gate-selftest.cpp` **includes the engine's own translation unit**
(`vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vif_hash.cpp`),
so it exercises the shipped decoder and cannot drift away from what the console
folds. `stub/` holds the four SDK headers it needs — `tamtypes.h`, a `qword_t`,
a `dma_tag_t` and an empty `debug.hpp`. Nothing is read from the tree, so it
keeps working with the gate compiled out.

Output: [`selftest-output.txt`](selftest-output.txt). Every line is `ok`.

| what it checks | result |
| --- | --- |
| a 36-quadword control chain and a 2-quadword baked chain over the same geometry | **hash identically** |
| three packets against one, over the same bag | **hash identically** |
| two baked packets against one unbaked packet — *the case the counter gate forbade* | **hash identically** |
| one vertex word flipped inside a `REF` payload | caught |
| one package dropped from the run | caught |
| the same packages in the wrong order | caught |
| `MSCAL` naming a different microprogram address | caught |
| the prim GIFtag's `NLOOP` off by one | caught |
| a texture upload moved between a different pair of draws | caught |
| six extra VIF `NOP`s | **not** caught, correctly |
| an unknown VIFcode | latches `broken` and names the code |
| a `REF` address that is not quadword aligned | refused, not hashed |

### Three things the self-test settled that the design page had only asserted

**The cadence really is invisible, and the reason is narrower than "NOPs".**
Two facts have to hold together. `StaPipCore::render` calls
`clearLastProgramName()` **once per bag, not per packet** (`stapip_core.cpp`
line 693), so a bag split across two packets emits `MSCNT` at the head of the
second and does not re-kick; and the end tag's two VIFcode slots are `NOP`, so a
packet boundary contributes nothing to the stream. The first version of this
test reset the kick state per packet — modelling a plausible engine rather than
this one — and three packets then hashed *differently* from one. **If either
fact ever stops holding, this test fails and the gate says the cadence is
visible, which is the correct answer at that point rather than a bug in the
gate.**

**A stub `dma_tag_t` with the wrong bitfield layout silently turns every `REF`
into a `CNT`.** The hardware puts `ID` at bits 28..30; a stub that put it at
26..28 made the decoder walk off the end of every chain and segfault. Recorded
because it is the kind of thing that, in a version that did not crash, would
have produced a confident and wrong hash. `stub/dma_tags.h` now carries the
ps2sdk layout and a comment saying so.

**A DMA `REF` address is 32 bits, and a 64-bit host has to be honest about it.**
The test reserves a sub-4 GB arena and places everything a `REF` can name inside
it, because the shipped decoder resolves the address field exactly as the DMAC
does. That is the test being faithful, not a workaround.

## The A/B, PCSX2, garage day, held pose

One worktree, one project directory, one knob: `TYRA_STAPIP_BAKED_STREAM` 0
against 1. Both arms carry `TYRA_FRAME_PROFILE`, `TYRA_STAPIP_BAKED_REPORT` and
`TYRA_STAPIP_VIFHASH` at 1, identically. Two ELFs, hashed and distinct
(`console/ctl.json`, `console/cand.json`). Raw windows: `console/arms.txt`.

| garage day, held pose | control (0) | candidate (1) | |
| --- | ---: | ---: | --- |
| **`ctrl`** — the VIFcode stream, NOP-stripped | `2570965368:1748567305` / `1292331401:3193271857` | **identical, both parities** | the gate |
| **`uni`** — the absolute-address uniforms | `3421674724:2216829733` | **identical** | the gate |
| **`--capture-frame` sha256** (x3 each) | `415f970f…73f1e` | **identical** | the gate |
| `ROADSTRIP` / `TERRAINSTRIP` | 470 / 588,8 | identical | fixture identity |
| `stripRun` | `75u` | `75u` | fixture identity |
| `FTCLIP` cull / clip / guard / out packages | 775 / 36.5 / 238.5 / 922.5 | identical | diagnostic |
| `FTCLIP` submitted vertices | 55 836 | identical | diagnostic |
| **DMA chain quadwords a frame** | **8 221** | **5 784** | **−2 437, −29.6 %** |
| **packet flushes a frame** | **120** | **109** | **−9.2 %, and legitimately** |
| VIF words a frame | 623 634 | 627 236 | +3 602, +0.58 % |
| `REF` tags a frame | 0 | 48–52 | one per direct bag |
| blocks rebuilt a frame | 0 | **2–3** (spike: 64) | |
| arena | 0 | **1 541 KB** (spike: 1 431) | |
| `STAPIPMISS prim` | — | **0** (spike: 2 a frame) | fixed |
| `STAPIPMISS bbox` | — | 1 a frame (spike: 1) | caller-side, unfixed |

Four things in that table are worth reading twice.

**`ctrl` and `uni` are bit-for-bit identical between the arms, on both parities
of the reflection probe.** That is the gate doing exactly what it was designed
for: the redesign hands VIF1 the same VIFcodes in the same order and the same
uniforms, through a chain that is 29.6 % smaller.

**`packet flushes` moved, 120 to 109.** That is the counter the old gate pinned,
moving legitimately, with the picture byte-identical. It is the whole reason the
gate had to be replaced, demonstrated in one row.

**+3 602 VIF words is the `NOP` padding, and the arithmetic lands on the nose.**
The format is 40 bytes — ten words — of alignment padding per replayed package
([docs/baked-vif-stream.md](../../../../docs/baked-vif-stream.md)), so 3 602
words is **360.2 packages replayed**. At 48–52 `REF` tags that is about 7.2
packages a tag, and 360 of the frame's 775 cull-route packages — i.e. very
nearly every bag that takes the direct route at all (`dsDirectBags` 53.5 against
`dsPartialBags` 59). The direct route is the ceiling, and the redesign is at it.

**The churn is fixed on the half that was this cache's fault.** `prim` is 0
where the spike read 2 a frame, and rebuilt blocks fell from 64 a frame to 2–3.
`bbox` is unchanged at 1 a frame, exactly as predicted: that is a caller
claiming its contents changed, and no key can route around it.

## What the gate could NOT do, which is the other half of this round

**Leg 1's geometry half is not usable as an equality check on this fixture, and
that is a measured result rather than a design choice.**

Two boots of **one ELF**, compared at equal frame numbers, with the picture
byte-identical in both and `chainQw` and `words` identical to the unit:

| | across two boots of one ELF |
| --- | --- |
| `ctrl` (the VIFcodes) | **identical**, and period-2 with the reflection probe |
| `uni` (the absolute-address uniforms) | **identical** |
| `geo` (everything unpacked into the VU1 double buffer) | **differs, every frame** |

So the static pipeline puts bytes on the wire that are **not a function of the
scene**. VU1 never reads them — the picture is byte-identical and matches the
value the whole branch publishes for this pose — but they are transferred.

**The copy pools were the obvious suspect and they are EXONERATED.** They keep a
slot's arrays between bags and rewrite only the first `size` vertices, so a
transfer past that would read whatever the pool held last — a good theory, and
wrong. `StaPipQBuffer::isPoolAddress` lets the fold split the geometry payload
by **which buffer it came out of**, and two more boots settled it:

| the geometry payload, split by source | across two boots of one ELF |
| --- | --- |
| out of a **copy pool** (clip, guard-band, strip-expanded) | **identical**, and period-2 with the probe |
| out of a **bag's own arrays** | differs, every frame |

```
pool1 f=1796 pool=2053892469:3585860574   pool2 f=1796 pool=2053892469:3585860574
pool1 f=1797 pool=1141554779:833468758    pool2 f=1797 pool=1141554779:833468758
pool1 f=1798 pool=2053892469:3585860574   pool2 f=1798 pool=2053892469:3585860574
```

So nothing is transferred past what was written. **The bytes that move are inside
bags that are genuinely rewritten every frame**, and that lands on the caller the
bake cache has been complaining about all along: `STAPIPMISS` reads `bbox=1` a
frame at this pose and names the bag as **96 vertices in 2 packages**. The
generated game has 26 unconditional `bboxVersion = ++g_bboxStamp` sites, mostly
in the lamp, beam and flashlight family, several of which rebuild their vertex or
colour arrays from wall-clock-driven fade terms — not frame-deterministic under
an emulator, which is exactly why a second boot gives different bytes at the same
frame number. The outer-road pose, which reads `bbox=0`, is correspondingly
cleaner.

**So it is not uninitialised memory crossing a bus.** It is one small bag whose
contents really do change every frame, imperceptibly — the picture is
byte-identical — while defeating the bake cache and the bbox cacher at once.
Naming the submitter is a caller-side change in a file this branch does not own;
the experiment that names it is written up in
[docs/backlog.md](../../../../docs/backlog.md).

**What this costs the gate, exactly.** Legs 1a (`ctrl`) and 1b (`uni`) stand and
are exact. The geometry payload is checked by leg 2, the byte-identical picture,
and by nothing else. For this round that is enough — the redesign changes
*structure*, and `ctrl` is precisely the structure hash — but a future change
that touched vertex data would be resting the whole argument on pixels again.

## The two adversarial modes: one passes, one FAILS

These exist because this fixture's traffic is parked AND its camera is frozen,
which flatters any change that skips work when an input did not change - every
car is still, every frame, for ever, and a skip test that would never fire in a
real district scores 100% here. Legs 1 and 2 of the gate both pass a renderer
whose invalidation is broken in a way this fixture never opens. Raw output:
`console/adversarial.txt`.

### Poison on retire: PASS

`TYRA_STAPIP_BAKED_POISON` overwrites an evicted arena with `0xDE` the instant it
is retired, instead of letting the two-frame graveyard hide it, and
`TYRA_STAPIP_BAKED_BUDGET_QW` was cut from 262 144 quadwords to **16 384** - 256
KB against the 1 541 KB the garage wants - so eviction thrashes continuously and
the poison has something to catch.

The capture is still `415f970f...73f1e`, and the VIFcode, uniform and pool
hashes are unchanged from the control. **No in-flight packet ever names a freed
block.** The two-frame graveyard and the flush-before-eviction ordering hold
under maximum pressure; that is the DMA-lifetime leg - the Probe B class -
passing its adversarial test.

### Verify on replay: FAIL, 1438 times, and it is a shipping blocker

`TYRA_STAPIP_BAKED_VERIFY` never replays: it rebuilds every block with the
ordinary writers and compares byte for byte against what the cache holds. Every
one of the 1438 mismatches is the same shape.

```
STAPIPVERIFY MISMATCH StaPip - Cull - C count=180 packages=2
             cachedQw=372 freshQw=372 firstDiffQw=116 ofBlock0Qw=228
```

| | |
| --- | --- |
| program class | **all 1438 are `Cull - C`**, colour-only |
| length | `cachedQw == freshQw` every time |
| first differing quadword | **116** of a 228-quadword block - the first COLOUR quadword |
| parked fixture | 1438 mismatches |
| moving fixture (`--keep-routes`) | 1451 |
| when | during the warm-up camera sweep; never once the camera freezes |

**The key does not cover what the block contains.** It holds the colour array's
POINTER and `bboxVersion`, and `bboxVersion` is a statement about the bounding
box, i.e. about positions - `StapipBagBBoxesCacher` is its only other consumer. A
caller that re-shades per-vertex colours in place, which this district does for
its dynamic lights, changes what the block must contain without touching
anything the key can see. The baked stream would replay stale lighting.

**The retained cache is immune, and the reason is the whole trade.** It stores
the chain - tags and `REF`s that still name the bag's own arrays - so a re-shaded
colour array is followed at DMA time and is always fresh. This exposure belongs
to the baked stream BECAUSE it inlines the payload: copying is what buys the tag
count, and copying is what creates this.

Neither leg 1 nor leg 2 can see any of it here, because once the camera stops
the colours stop too. That is precisely the class `benchmark-district.py`'s own
docstring warns about, and it is the whole reason these two modes exist.

## The fixture check, before any measurement in this directory is quotable

Mandatory on every arm, and it is not implied by anything else:

```
ROADSTRIP scene 0 strips 1 packages 470 triangles 31050
TERRAINSTRIP scene 0 chunk 2,2 strips 1 vertices 588 packages 8 triangles 512
grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp   ->   75u
```

**A matching capture hash is a PICTURE check and never a FIXTURE check.** Both
round-one failures matched the expected capture hash while measuring a 72-run
scene. And the editor is rebuilt from this worktree before the fixture is
regenerated: `--refresh-gen` regenerates faithfully with a stale baker, and an
editor binary sitting in `build/` is not the editor at the tree's commit.

### One profile note, deliberately against the usual rule

The arms here are built with `liveDebug` **on**, not `--profile quiet-debug`.
The live tools cost 6.44 ms of `work` and that rule exists for a reason — but
`--capture-frame` reads the devkit's self-screenshot, which needs `liveDebug`,
and **a gate arm produces no milliseconds at all**. The hash fold reads 2.7 MB a
frame and these builds run at about 11 fps. Nothing in this directory is a
timing measurement and none of it may be turned into one. The hardware round
will use `quiet-debug` and no hash.

## Files

- `gate-selftest.cpp` — the host self-test above, over the engine's own decoder.
- `selftest-output.txt` — its output.
- `stub/` — the four SDK headers the host build needs.
- `build-arm.ps1` — one arm: set the switches, `--refresh-gen`, `--build`, hash
  the ELF, print the fixture check.
- `run-arm.ps1` — one PCSX2 boot: hold a pose, capture it, keep the log.
- `console/arms.txt` — every arm's fixture lines, `FTCLIP`, `STAPIPBAKE`,
  `STAPIPMISS` and `STAPIPVIFHASH` windows.
- `console/ctl.json`, `console/cand.json` — ELF hash, switches, commit, time.
- `captures/ctl-garage-day.png` — the reference frame. All six captures across
  both arms hash to
  `415f970fe840f3880c48f4bab7119d8a6cc1d55aacdf533d23522dac61273f1e`, so they
  are not committed a second time.
