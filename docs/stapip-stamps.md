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
