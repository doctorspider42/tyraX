# The acceptance gate for a restructured static pipeline

An acceptance gate is the set of checks that must agree between two arms of an
A/B before a change to the static render pipeline is allowed to ship. This page
designs the gate for the EE submission rearchitecture
([ee-submission-rearchitecture.md](ee-submission-rearchitecture.md), item 3 of
its order of work), because the gate every previous round used **cannot be
reused here**, and building behind a gate that cannot tell an intended change
from a broken one is how a round produces a fast, wrong renderer.

Nothing on this page is a millisecond. It is a correctness instrument, and a
build that carries it is deliberately slow.

## Why the old gate has to be replaced

Every renderer round on this branch used the same gate, and
[baked-vif-stream.md](baked-vif-stream.md) states it plainly: the picture must
be byte-identical **and** `packetFlushes`, the `FTCLIP` routing counts, the
submitted vertices and the triangles must not move.

That gate is what stopped the spike from delivering anything. Its own "Limits"
section says so:

> The spike is not the prize. It keeps the per-package classification and the
> 16-group qbuffer flush cadence, because `packetFlushes` is one of the counters
> this gate pins.

The mechanism is exact. A run of packages under one DMA `REF` tag cannot cross a
packet flush boundary, so the longer the run the bigger the prize — and the only
way to lengthen runs is to change when the pipeline flushes. **Pinning the flush
cadence pins the prize.** The same objection applies, more weakly, to the
`FTCLIP` routing counts: they count *routed packages*, and a redesign that
replays a baked run without re-walking it package by package will legitimately
stop incrementing them.

So the counters split into three kinds, and the gate must treat them differently:

| counter | kind | in the gate? |
| --- | --- | --- |
| `packetFlushes`, `chainQw`, `REF` tags | **the prize** — the thing being changed | never |
| `FTCLIP` cull / clip / guard / out packages | **structure** — how the work was organised | diagnostic only |
| submitted vertices, triangles | **conserved** — the same geometry either way | implied by the gate below, not pinned separately |
| `ROADSTRIP` / `TERRAINSTRIP` / `stripRun` | **fixture identity** — is this the same game | mandatory, and it is not a correctness check |

## What the gate has to be instead

An **output** check: something that constrains what the GS receives and says
nothing about how the chain that produced it was built. Two candidates exist in
the tree, and only one of them survives inspection.

## The VU1 packet tap: the right seam, the wrong shape

The repo already has a VU1 packet tap
(`vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_vu_tap.hpp`,
[devkit.md](devkit.md)) with two hooks, and the obvious idea is to gate on what
it captures. **The idea is half right**: the packet hook is exactly the correct
seam, and the capture-and-compare-offline shape built on top of it cannot be the
gate. Four reasons, all checked against the code rather than assumed.

**1. The VU1 memory hook sees one package, not a frame.** `VuMemHook` is called
from `StaPipQBufferRenderer::sendPacket` *after* the chain was sent and VU1 went
idle, and it hands over `(const void*)0x1100c000, 1024 * 16` — the whole of VU1
data memory. That memory is the double buffer. The GIF packet a microprogram
stages for `XGKICK` is overwritten by the next package, so the snapshot holds
the **residue of the last package of one chain**. A garage-day frame stages
about 1 050 package GIF packets across 120 chains; the snapshot sees one of
them. And *which* one it sees depends on where the flush landed — so as a gate
it would pin the flush cadence by the back door, which is the exact failure
being designed around.

**2. It costs a hard pipeline stall, by design.** The same block does a
`dma_channel_wait(DMA_CHANNEL_VIF1, 0)` and then spins on `VIF1_STAT` for up to
two million iterations. The header says it outright: the devkit installs it
"only for the frame it captures". Frame-wide it is ~120 stalls a frame plus
16 KB of copying each.

**3. The packet hook alone is a structure check.** `VuPacketHook` receives
`currentPacket->base` and `packet2_get_qw_count(currentPacket)` — the DMA chain,
which is *precisely* what the redesign changes. The spike already measured the
chain going from 9 146 to 6 705 quadwords a frame. Comparing chains between arms
fails by construction.

**4. The capture buffers truncate silently, and today's frame is already at the
edge.** `VU_CAP_MAX_QW` is 2 048 quadwords, `VU_CAP_MAX_BLOCKS` is 64, and both
clamps are silent (`if (qw > VU_CAP_MAX_QW) qw = VU_CAP_MAX_QW;` and
`if (take > room) take = room;`). Garage day submits ~1 050 packages over 120
flushes, i.e. **8.75 packages per flush**: 26 referenced blocks (fine) carrying
about **1 986 payload quadwords against a 2 048 limit — 97% full**. A flush
holding the full 16 groups overflows it and the file says nothing. A gate that
compares truncated data is worse than no gate.

**But the packet hook is still the right place to stand.** It runs after the
chain is finished and before `dma_channel_send_packet2`, i.e. looking at exactly
the bytes the DMAC is about to read. The fix is not to ship those bytes to the
host and decode them there; it is to **decode and fold them on the console**.

## Leg 1 — the canonical VIF1 word stream hash

The DMAC does not interpret DMA tags inside referenced data; it feeds every word
of a `REF` payload to VIF1, and with tag-transfer enabled it feeds VIF1 the upper
eight bytes of every chain tag as well. So what VIF1 actually receives is one
**undifferentiated word stream**, and that stream — not the chain that delivered
it — is what determines the picture. This is the same observation
[baked-vif-stream.md](baked-vif-stream.md) uses to argue the format is legal;
here it is turned into a check.

The gate build walks the chain at the packet hook, **flattens** it (inline data
for `CNT`, the referenced quadwords for `REF`), **decodes** the resulting word
stream the way VIF1 does — read a VIFcode, consume the data words it takes, read
the next — and folds a canonical form of it into a per-frame rolling hash:

- control VIFcodes are folded as `(cmd, num, imm)`;
- `NOP` is **dropped**, because a NOP genuinely does nothing to VIF1 and the
  baked block's two-word alignment padding is made of them (that is 40 bytes of
  difference per package the spike already documents, and it must not count as a
  difference);
- unpack data words are folded verbatim, in order.

The decoder needs a closed and very small instruction set. Grepping every
`packet2_*` call under `src/renderer/3d/pipeline/static/` gives exactly:
`NOP`, `STCYCL`, `FLUSHE`, `FLUSH` + `MSCAL`, `FLUSH` + `MSCNT`, and
`UNPACK V4_32`. Anything else must **fail the gate loudly** rather than be
skipped — an unknown VIFcode means the decoder no longer knows how many data
words follow, and from that point on it is hashing noise.

This is a structure-independent output check, and that is the whole point:

- a chain of 7 quadwords per package and a single `REF` covering 6.4 packages
  produce the **same** word stream and therefore the same hash;
- the flush cadence is invisible to it — a flush boundary contributes nothing to
  the stream but an end tag;
- but a package dropped, reordered, given the wrong GIFtag, kicked with the wrong
  program, or fed one stale vertex **changes the hash**.

### All of that is demonstrated, not argued

It is a property of the decoder, so it needs no PS2 to check.
[`ee-rearchitecture-2026-09-16/gate-selftest.cpp`](../examples/vehicle-playground/authoring/ee-rearchitecture-2026-09-16/README.md)
builds both chain shapes over the same synthetic bag, folds them through **the
engine's own translation unit**, and checks every claim above plus six
deliberate defects. A 36-quadword control chain and a 2-quadword baked chain
hash identically; three packets hash as one; two *baked* packets hash as one
*unbaked* packet — which is exactly the case the counter gate forbade. Six extra
`NOP`s change nothing, an unknown VIFcode latches and names itself, and a
misaligned `REF` address is refused rather than hashed.

**And it narrowed the cadence claim, which had been asserted too loosely.** The
cadence is invisible only because **two** facts hold together: `StaPipCore::
render` calls `clearLastProgramName()` **once per bag, not per packet**, so a
bag split across packets emits `MSCNT` at the head of the second rather than
re-kicking; and the end tag's two VIFcode slots are `NOP`. The first version of
the self-test reset the kick per packet — a plausible engine, not this one — and
three packets then hashed *differently* from one. If either fact ever stops
holding, the self-test fails and the gate starts reporting the cadence as
visible, which at that point is the correct answer rather than a defect in the
gate.

### The texture interlock, folded into the same hash

VIF1 path 1 is not the only thing this pipeline sends the GS. Textures are
uploaded on the GIF channel and the ordering is enforced by
`StaPipQBufferRenderer::beforeTextureMutation`, which **flushes the pending
packet** and syncs before a texture is allowed to change. So a redesign that
changes the flush cadence can change which draws a given texture upload sits
between — a real correctness hazard that a pure VIF-word hash is blind to.

The fix is one line in the same instrument: fold a marker for each texture
mutation into the rolling hash, in sequence, at `beforeTextureMutation`. The hash
then models the pipeline's whole contribution to GS state as an **ordered
sequence of draws and texture changes**. The flush cadence stays invisible; the
relative order of draws and texture changes — which is a correctness
requirement, not a structural detail — does not.

### What it costs

Garage day submits 55 836 vertices a frame across roughly three streams, so the
fold reads about **167 000 quadwords, ~2.7 MB, per frame**, cold, out of the
same memory the DMAC is about to read. Expect **10–25 ms a frame** and a frame
rate around 15 Hz.

**That is acceptable, and the reason is the most important sentence on this
page: the gate build is never the build you time.** Correctness and milliseconds
are separate arms with separate ELFs. The gate has to be deterministic and
identical across arms; it does not have to be cheap.

It ships behind `TYRA_STAPIP_VIFHASH`, default 0, in the style of
`TYRA_STAPIP_ATTRIB` and `TYRA_STAPIP_BAKED_STREAM` — one null-branch when off,
and per [devkit.md](devkit.md)'s zero-cost rule, nothing linked into a release
build. The decoder is
`vendor/tyra/engine/{inc,src}/renderer/3d/pipeline/static/core/stapip_vif_hash.*`;
it folds at the same seam the devkit tap stands on in
`StaPipQBufferRenderer::sendPacket`, at `beforeTextureMutation` for the texture
half, and `StaPipCore::onFrameEnd` prints the ring.

### The determinism caveat

The shared reflection probe alternates every other frame (garage day reads
45 638 / 35 186 triangles on alternate frames — see
[ee-probes-2026-09-16](../examples/vehicle-playground/authoring/ee-probes-2026-09-16/README.md)),
so a per-frame hash has period two. The readout therefore prints the **last K
frame hashes** and the arms are compared as sequences, not as single values. The
night poses have authored lamp flicker and twinkling stars and never settle;
nothing is read from them, exactly as every previous round on this branch.

### MEASURED: two of its three parts are exact, and the third is not usable

Leg 1 has now been run. Two boots of **one ELF** in PCSX2, compared at equal
frame numbers, with the picture byte-identical in both and `chainQw` and the
word count identical to the unit
([ee-rearchitecture-2026-09-16](../examples/vehicle-playground/authoring/ee-rearchitecture-2026-09-16/README.md)):

| part of the stream | across two boots of one ELF |
| --- | --- |
| the VIFcodes | **identical**, including the reflection probe's period-2 cadence |
| the payload of unpacks to the absolute region (MVP, lights, options, ALPHA) | **identical** |
| the payload of unpacks with `usetop`, i.e. everything in the VU1 double buffer | **differs, every frame** |

**So something the static pipeline hands VIF1 is not a function of the frozen
scene** — the first reading of this was "bytes on the wire that nobody wrote",
and the next section shows that reading is wrong. No instrument had ever looked
at this wire before, which is why it took two runs to say what it is.

### The copy pools were the obvious suspect, and they are EXONERATED

The first reading of this pointed at the qbuffer copy pools — `fillByCopyMax`
and friends keep a slot's arrays between bags and only rewrite the first `size`
vertices, so anything transferred past that would be whatever the pool held
last. It is a good theory. It is also wrong, and one run settled it.

`StaPipQBuffer::isPoolAddress` lets the fold split the geometry payload by
**which buffer it came out of**: a `REF` naming a pool against a `REF` naming a
bag's own array. Two boots of one ELF, garage day, held pose:

| the geometry payload, split by source | across two boots |
| --- | --- |
| out of a **copy pool** (clip, guard-band, strip-expanded buffers) | **identical**, and period-2 with the reflection probe |
| out of a **bag's own arrays** | differs, every frame |

So nothing is being transferred past what was written, and the pools are exactly
as reproducible as everything else. **The bytes that move are inside bags that
are genuinely being rewritten every frame.**

That lands on the same caller the bake cache has been complaining about all
along. `STAPIPMISS` reads `bbox=1` per frame at this pose — exactly one bag a
frame claiming its *contents* changed on a scene that is not moving — and names
it as **96 vertices in 2 packages**. The generated game has 26 unconditional
`bag->bboxVersion = ++g_bboxStamp` sites, most of them in the lamp, beam and
flashlight family, and several of those rebuild their vertex or colour arrays
from wall-clock-driven fade terms — which is not frame-deterministic under an
emulator, and is why the same frame number gives different bytes on a second
boot. The outer-road pose, where `STAPIPMISS` reads `bbox=0`, is correspondingly
cleaner.

**So this is not uninitialised memory crossing a bus.** It is one small bag
whose contents really do change every frame, imperceptibly — the picture is
byte-identical — while defeating the bake cache and the bbox cacher at the same
time. Naming the exact submitter means following that bag through the generated
game, which is a change to a *caller's* contract and is the open item in
[backlog.md](backlog.md).

**What it costs the gate, exactly.** The VIFcode and uniform hashes stand and are
exact, and they are what this round needed: a submission *restructuring* changes
structure, and those two are the structure. The geometry payload falls back to
leg 2, the byte-identical picture, and to nothing else — so a future change that
touched vertex data would be resting its whole argument on pixels again. The
readout therefore prints the three separately: a gate that could only say "these
two runs differ" and never say *where* would have been useless here.

## Leg 2 — byte-identical pixels, over a pose sweep

Leg 1 is computed from EE-visible memory **at chain-build time**. The DMAC reads
that memory later. Everything that lives in the gap — a block freed or
overwritten while a `REF` to it is still live, a cache line the DMAC sees stale —
is invisible to leg 1 **by construction**, and that gap is precisely where
Probe B's corruption lived: with the packet itself uncached and the copy pools
still flushed, dropping `FlushCache` still tore a 14-row band at the horizon.

So the pixel check stays, and the strong suggestion behind this round is correct:
**byte-identical pixels remain valid under any restructuring, because they are an
output check and not a structure check.** It is the only leg with teeth against
the lifetime and coherency class of defect.

Its weakness is coverage, not soundness. The spike hashed three captures of
**one** pose. Two cheap extensions:

- **Sweep the poses.** `district-benchmark-pose.txt` already selects a held pose
  and is re-read every 30 frames, so a sweep is a harness loop — write pose,
  wait, capture, repeat — with no game-side change. Garage day and outer-road day
  are both still; the two night poses are not and are excluded.
- **Add poses that exercise the routes the current two do not.** Garage day
  already covers all four routes (775 cull, 36.5 clip, 238.5 guard, 922.5
  rejected, and 53.5 direct bags against 59 partial), which is the property that
  makes it a good gate pose at all. What it does not cover is a mesh straddling a
  flush boundary at a *different* place, which is what changes when the cadence
  changes — so the sweep wants at least one pose whose bag order differs.

## The two adversarial modes, because a parked fixture flatters a cache

`benchmark-district.py` says it in its own docstring, and
[wheel-rebake-skip.md](wheel-rebake-skip.md) is the worked example: the traffic
is parked, so **any change that skips work when an input did not change scores
100% on this fixture**. A per-frame rebake that writes the same bytes every frame
leaves stale cache lines indistinguishable from fresh ones. Legs 1 and 2 both
pass a renderer whose invalidation is broken in a way this fixture never opens.

Two modes close most of that, and both are cheap:

**`TYRA_STAPIP_BAKED_VERIFY`** — before replaying a cached block, rebuild it from
scratch by the ordinary path and assert the two are byte-identical. This catches
"the cache served a block that no longer matches what the ordinary path would
produce", i.e. every missing or wrong invalidation, and it needs no control arm,
so it runs under `--keep-routes` with the traffic **moving** — the one fixture
where a skip-when-unchanged bug can actually fire. (A `--keep-routes` A/B is
impossible: the two arms run at different speeds and never share a frame.)

**Poison on retire** — when a block is evicted, overwrite it with a recognisable
pattern *immediately* instead of letting the two-frame graveyard hide it. Anything
still referencing it then corrupts the picture loudly on the next capture rather
than silently on some future content. This converts the latent Probe-B class of
defect into one leg 2 can see, on the parked fixture. Running the sweep with
`StaPipBakedStreams::kMaxQwords` cut far below the 1 431 KB the arena actually
holds forces eviction and the graveyard to cycle, so the poison has something to
catch.

## Leg 3 — fixture identity, which is not a correctness check

Mandatory on every arm, and it is not optional or implied:

```
ROADSTRIP scene 0 strips 1 packages 470 triangles 31050
TERRAINSTRIP scene 0 chunk 2,2 strips 1 vertices 588 packages 8 triangles 512
grep "stripRun = 7" <fixture>/src/terrain_game.cpp   ->   75u
```

**A matching capture hash is a PICTURE check and never a FIXTURE check.** Both
round-one failures matched the expected capture hash while measuring a 72-run
scene, because changing the strip run changes how a surface is cut into runs and
not which pixels it covers. The producer lines and the baked constant are what
catch it. And the editor must be rebuilt from the measuring worktree first:
`--refresh-gen` regenerates faithfully with a stale baker, and an editor binary
sitting in `build/` is not the editor at the tree's commit.

## Is the gate sound?

Yes, with one named hole.

**The claim.** If (a) the canonical VIF1 word stream with texture mutations
interleaved in order is identical between arms, (b) the captured pixels are
byte-identical at every pose of the sweep, and (c) the fixture identity checks
match, then the two arms deliver the GS the same thing.

**Why (a) is sufficient for identical GS input.** VU1 is a deterministic function
of the word stream it receives and the program it runs; the program is named
inside the stream by its `MSCAL` address; the GS output is a deterministic
function of VU1's staged packets and of GS state, and the only GS state this
pipeline mutates outside the stream is the texture, which (a) orders. The one
proviso is that the DMAC actually delivers what the decoder read.

**Why (b) is necessary.** (b) *is* that proviso. Leg 1 reads at build time and
cannot see the delivery.

**The hole, stated plainly.** A DMA-lifetime or cache-coherency defect whose
window never opens on this fixture passes both legs. The adversarial modes above
narrow it — poison-on-retire, an undersized arena, and the verify mode on a
moving fixture — but they do not eliminate it, and no gate that runs on a frozen
scene can.

**And PCSX2 sharpens that hole rather than covering it.** PCSX2 emulates no EE
data cache, so the entire Probe-B failure mode — the one that actually corrupted
a picture on this branch — **is invisible in PCSX2 pixels**. Leg 2 in PCSX2
checks logic. Leg 2 on the console checks coherency. They are different checks
wearing the same name, and a redesign that touches DMA lifetime is not accepted
until the sweep has run **on hardware**.

## What the gate cannot see, in full

- **DMA lifetime and cache coherency on a frozen fixture** (above). The residual.
- **Coherency at all, in PCSX2** (above). Hardware only.
- **Anything outside the static pipeline's VIF1 path and its texture barrier** —
  other pipelines, GS registers set by `RendererCore`, the display mode.
- **A misaligned `REF` address.** The DMAC requires quadword alignment; the
  decoder reads the same pointer with `memcpy` and would not notice. This wants
  an explicit assert in the gate build, not a hash.
- **Performance.** By construction — the gate build is 15 Hz. A gate arm never
  produces a millisecond, and a timing arm never carries the gate.
- **Whether the change is worth having.** The gate says "not broken". It has no
  opinion about `chainQw` falling, and that is the point.

## If the gate had failed to be soundable

It did not, and it is worth recording what the answer would have been, because a
cheap stop is a result: had there been no way to check the GS's input without
pinning the chain that produced it, the correct move was to **stop the round and
say so**, not to build behind the old counter gate and discover later that a
byte-identical picture on one still pose had been carrying the whole argument.

## What this page does not establish

The costs here are arithmetic, not measurement: the 167 000 quadwords a frame is
derived from the submitted-vertex count and the stream counts per program class,
and the 10–25 ms follows from it. The 97%-full figure for `VU_CAP_MAX_QW` is the
same arithmetic against the spike's measured per-frame package count.

**The self-test is a host result and proves the decoder, not the console.** It
shows that the fold has the property the gate needs; it does not show that the
engine's real chains stay inside the VIFcode set it decodes, and it cannot — the
only thing that settles that is a run whose readout does not say `BROKEN`.

Leg 2 has not been run on hardware, which is the gate's own open item rather
than an omission: leg 2 in PCSX2 checks logic and leg 2 on the console checks
coherency, and `192.168.100.150` belonged to another agent this round.
