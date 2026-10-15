# StaPip bag stamps

A **stamp** lets a static, wholly visible, baked bag skip the generic per-bag
StaPip frontend. The bag re-emits the uniform head and baked-body REF that its
last ordinary submission produced. Only the values that depend on the live view
or lights are patched in:

- MVP;
- fog words (including the "fog off" constant);
- TEX0;
- spot quads;
- the single colour.

Engine: `StaPipQBufferRenderer::tryStamp` / `captureStamp` and the two hooks in
`StaPipCore::render`. Compile switch: `TYRA_STAPIP_STAMP` (default 1). Runtime
switch: `StaPipCore::setStampEnabled` (engine default **off**; generated games
turn it on in the frame pipeline setup).

## Why it exists (the PC-ism it removes)

Physical-PS2 measurements on the Vehicle Playground night scene (2026-10-12) are
in [the night EE investigation](tyrax2-night-ee-2026-10-12.md). The ordinary
path costs about 10 000 EE instructions per bag. It also costs about 330
I-cache and 310 D-cache misses per bag. An I-cache miss costs about 50 EE
cycles and a D-cache miss about 42.

The path re-derives everything on every frame:

- the baked-cache key and its hash lookup;
- the bbox cache;
- the program;
- light, fog, and texture state;
- the whole uniform head.

All of this mostly reproduces the previous frame's bytes. Around 135 bags of
about 95 µs each made up roughly 13 ms of an 18 ms night frame.

The PS2 way is to keep the prepared DMA and stamp it: the EE checks that
nothing changed and appends the chain, and VU1/GS do the work.

## Capture

`StaPipCore::render` arms a capture when the bag meets all of these
conditions:

- it uses TyraMVP;
- frustum culling found it IN, either in the view frustum or through the
  guard-band rule;
- it has a bbox;
- it is not a billboard and is not lit;
- its texture, if any, ends up REPEAT;
- there are no program overrides;
- BLSS and telemetry are off.

`sendObjectData` clears `stampOk` for any block that is not plain data:

- a wrap write that leaves anything but REPEAT in force (a write back to
  REPEAT is captured, and the replay re-applies the packet's wrap state);
- lighting uniforms;
- custom VU parameters;
- the clip block;
- the billboard basis;
- env basis or paint;
- non-inline unpacks.

It records where the MVP, spot, options and TEX0 quadwords sit.
`replayWholeBakedBag` copies `[FLUSHE .. REF]` into a 512-slot table (8 probes)
keyed by bag pointer, right before any flush can send those bytes.

## Replay validity

`tryStamp` falls back to the ordinary path, which re-captures, unless every
one of these checks holds.

Producer facts:

- same vertex pointer, count and `bboxVersion`;
- same content stamps;
- same info pointer AND info contents (a byte compare: fields such as
  `additiveBlendFix` or `zTestType` are changed in place by twinkling stars
  and the reflection probe);
- same model pointer; a MOVED model is accepted and its world box recomputed;
- same texture, single-colour pointer, ST and colour stream pointers and
  strip state.

Renderer facts:

- same depth scale and resident program classes;
- no program overrides and no custom VU params;
- the baked entry still has the same serial and arena;
- the bound baked entry was not destroyed: a 64-entry destroyed log is swept
  each frame; only its overflow (or `clear()`) bumps the global
  `eraseGeneration` and drops every stamp.

Prim state: the same prim key, recomputed exactly like the baked key.

Visibility: the world AABB of the captured object-space box is IN the view
frustum, or inside all eight VU1 clip planes (guard band). This test is
conservative: the world AABB contains the oriented box, so "inside" implies
the ordinary path would also say IN.

Texture: still resident and REPEAT.

Light: the same `pickDynLight` + `buildSpotForBag` + zero-influence test as
the ordinary path. The spot block's presence (active or not) must match the
capture; its numbers are rewritten from the live light.

Wrap state: REPEAT in force, so the ordinary path would emit no CLAMP write.

The replay performs the same side effects as the ordinary path:

- `useTexture`, `updateClutBuffer` and the texture function;
- `setSpot` and `ensureProgramSet`;
- batch candidacy;
- baked-entry lifetime refresh, `lastProgramName` and the hit counter.

Stamps pack into the open packet until the next stamp would not fit. An
ordinary bag still reserves its own worst case before appending.

## Batching (2026-10-14)

A stamp joins the open submission batch whatever its bag's size
(`setStampBatchAnySize`, default true). Only the REPEAT half of the
ordinary rule still applies. The size cap in `batchSized`
(`count <= maxVertCount * 15`) bounds an ordinary bag's packet, which holds one
command group per package. A stamp appends ~20 qwords and a REF to an
immutable arena, so the cap meant nothing for it. It only kept every large road
and terrain strip sending a packet of its own.

The generated game also opens a batch around the main view's terrain chunks.

The cost of one stamp hit was measured on the PS2 (heavy night spot, lean
build, COP0 ticks per segment, 122 hits per frame, ~34 µs each):

| Segment | ms per frame |
| --- | ---: |
| find | 0.29 |
| producer facts | 0.35 |
| frustum | 0.36 |
| texture | 0.14 |
| light pick | 0.74 |
| close the previous packet | 0.70 |
| program + texture use | 0.24 |
| copy + MVP + fog | 0.59 |
| send after the stamp | 0.87 |

With any-size batching:

| | Before | After |
| --- | --- | --- |
| Packets per frame | 81 | 45 |
| Parked work | 13.0 ms | 12.5 ms |
| Heaviest drive window | 14.35 ms | 14.0–14.1 ms |

`submit` itself only fell from 2.39 to 2.18 ms. Its cost follows the bytes it
copies, not the call count (see the night EE investigation).

`RendererCore::pickDynLight` rejects out-of-reach lights by comparing squared
distances before taking the square root. The verdict is the same; the gain
was not measurable.

### Crossing bags batch too (2026-10-14)

`StaPipCore::setPartialBatch` (default on, VU1 clipping only) lets a bag
that crosses the frustum join the open submission batch. Before this, every
crossing bag closed and submitted a packet of its own. On a physical PS2 that
cost ~70 us a bag, mostly the VIF1 queue's per-packet copy and tag walk.

A crossing bag stays pending only when both of these hold:

- **It copied nothing into the slot copy pools** (`StaPipQBuffer::poolTouched`):
  no strip expansion and no merged subpackages. The pools are per slot, and
  only a send flips their side, so the next bag would overwrite a pending
  bag's copies. This is the 1.81.1 slot-pool race in a new shape.
- **No half flush sent the packet mid-bag** (`objectDataPending` still true):
  otherwise nothing would ever send what the bag appends afterwards.

Otherwise the bag sends at its end, exactly as an unbatched one does.

Measured lean, heavy night spot, same ELF:

| | Heaviest window | Late frames |
| --- | --- | --- |
| Off | 14.50 ms | 9 |
| On | 14.31 ms | 7 |

Strip bags usually expand into the pool and still send, which is why the
gain is small. The picture on a TV has not been checked yet.

## Immutable streams (2026-10-14)

The frame recorder copies every DMA REF target that is not a registered
immutable span into the frame arena, on every frame. Only the engine's baked
arenas were registered. So a crossing bag's clip slices, and every other REF
to a bag's own arrays, copied static geometry into the arena each frame. At
the heavy night spot that was 45-55 KB in 170-230 REFs a frame, and none of
it was in the qbuffer copy pool.

A first ceiling arm that treated EVERY REF as immutable measured 13.65 ->
12.60 ms in the heaviest window. That number is an artefact: the arm also
borrowed arrays refilled every frame, so the console drew partly garbage,
the GPU finished sooner and the EE waited less inside `submit`. Do not quote
it. The attribution runs below are the real price.

The safe version is opt-in per bag:

- **The producer sets `StaPipBag::immutableStreams`.** This promises the
  streams (vertices, STs, per-vertex colours, normals) do not change while
  registered.
- **`StaPipCore::render` registers them on the bag's first ordinary draw**
  (`registerImmutableStreams`). A stream another bag already registered, or a
  full table, stays copied.
- **The owner calls `releaseImmutableStreams` before it changes,
  reallocates or frees a stream.** The destructor calls it too. Release waits
  for every DMA reader, so it belongs to rebuilds, not to every frame.

The span table grew from 512 to 1024 entries, since baked arenas share it.

The generated game opts in two producers:

- **static batches:** `rebuildStaticBatch` releases first;
- **procedural chunks, roads included:** `procAddMergedObject` releases a chunk
  before it grows its arrays.

Measured on a physical PS2 (lean, heavy night spot, two cycles each):

- With roads and static batches registered, the arena copies 34-44 KB a
  frame instead of 44-59 KB.
- The heaviest window is 13.59-13.66 ms with 5-6 late frames, the same as
  without it (13.65 ms, 6 late frames).

Who owns the copied bytes, measured by opting producers in for one arm each:

| Opted in | Copied a frame | Heaviest window |
| --- | --- | --- |
| roads + static batches (as shipped) | 34-44 KB | 13.77 ms |
| + every bag drawn through `renderObjectBag` | 34-41 KB | 13.81 ms |
| + every bag drawn through `renderWorldBag` | 2-4 KB | 13.61 ms |
| terrain chunks alone (LOD off, both arms) | no change | no change |

Terrain contributes nothing: its chunks go through stamps and the strip
expansion cache, which are already immutable arenas. The remaining bytes
belong to world bags, and some of those are refilled every frame. Removing
almost the whole copy is worth ~0.15 ms, so no further producer was opted in.

A last-hit cache in `ImmutableSpanTable::borrow` (one binary search instead
of up to four per REF) was also measured: 13.655 against 13.656 ms. Reverted.

The drain is not cheap. An A/B arm that released bags on their first draw
spiked exactly where new roads came into view (25 late frames instead of 6),
so release must never sit on a per-frame path.

## Stamps in runs (2026-10-14, 1.174.1)

**Where a stamp's time goes.** PMU counters on a physical PS2 (heavy night
spot, 107 hits a frame) put one hit at ~28 us: about 58 I-cache and 104 D-cache
line misses, i.e. ~87% of it waiting for memory. The instructions are few. The
game runs more than 10 us of its own code between any two bags - every hit was
"cold" - so the stamp path is evicted from both caches before each one. The
segments, D-misses per hit: copy of the head 26, light pick 15, batch decision
16 and flush 11 (both mostly the packet send they occasionally trigger), find
9, producer facts 9.

**What changed:**

- **Deferral** (`StaPipCore::setStampDeferral`, default on). Inside a
  submission batch, a bag that has a stamp is queued instead of replayed. The
  queue is replayed in one run, prefetching the next bag's stamp, bag, info and
  matrix while one replays. It is flushed before any bag that takes the
  ordinary path, when the view-projection changes, when the same bag comes
  again, at the batch's end, on a program override or VU parameter change,
  on telemetry and at frame end. StaPip's own draw order is unchanged, and the
  batch already sent StaPip's packets after other pipelines' work. Textures need
  no hook: a queued stamp checks residency when it replays and takes the
  ordinary path if its texture was evicted meanwhile.
- **Packed keys.** The probe reads a 2 KB array of bag pointers instead of
  ~700-byte entries, one line per probe. The entry keeps its scalar facts
  first and the copied head last.

Measured on a physical PS2, same ELF, night, heavy spot:

| Window | Deferral off | Deferral on |
| --- | ---: | ---: |
| Heaviest drive | 12.68-12.70 ms | 12.38-12.44 ms |
| Lighter drive | 10.39 ms | 10.19-10.26 ms |
| Parked | 12.48 ms | 12.38-12.43 ms |

Less than the miss counts promised: the runs are short, because ordinary bags
and other passes interleave with the stamps. The VRAM grab is the same within
the noise between two cycles of one arm.

## Crossing bags: tried and removed (1.175.0 -> 1.175.1)

A head stamp for frustum-crossing bags (texture, light, batch decision and
uniform block replayed, packages still routed per frame) hit 89% of the time
in PCSX2 and changed nothing on the PS2 (heaviest window 12.84 ms either
way). The head costs memory traffic, not instructions, and the replay reads
about as much as the ordinary path does. See the night EE notes, "Where the EE
frame actually goes".

## Not covered

- **Partial bags:** they need per-package classification and clip programs.
- **Lit bags, env/paint bags, billboards:** these classes are excluded at
  capture.
- **Moving bags** replay too: the MVP is patched from the live model matrix,
  and visibility uses the recomputed world box.

## Status

The A/B fixture is the `C:\tx\rig` private night drive from the investigation
page: parked night 18.3 -> 13.5 ms (with the audio idle sleep). Generated games
enable stamps; the engine default stays off. Visual equivalence on a TV is
still pending.
