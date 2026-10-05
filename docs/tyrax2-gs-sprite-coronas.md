# Private GS SPRITE corona experiment

This implementation is not promoted. The original trial below failed ordinary
Hybrid admission and separate Bits16 output. A subsequent
[24-bit follow-up](#24-bit-follow-up-isolated-output-and-drawing-pixels-pass)
now passes isolated SPRITE endpoints, triangle fallback controls and identical
textured GS drawing pixels. Ordinary activation, Hybrid scanout and physical
timing benefit remain unqualified.

## Candidate and native qualification

The candidate starts from the private [Pool2 EE producer](tyrax2-pool2-ee-producer.md).
It keeps the original six vertex transforms, lighting, fog and clipping. A typed
corona owner may request a whole-package post-transform reduction: constant
final RGBA, Z/F and Q, exact rectangle topology, safe coordinate spans and clear
ADC bits allow copying two original endpoints per quad. Other packages retain
complete triangles. This saves GS input if accepted; it does not remove the
original EE producer or six VU transforms. Material, texture, depth, blending
and fog state are retained.

V5 assembled and linked, but its full VU1 resident family with billboards needed
2046 instructions against a 2042 limit. It was rejected before execution.
V6 removes redundant lower-pipeline operations without weakening the predicate.
The actual TC image is 506 instructions; full residency is 2040/1840 for the two
clipping families, including billboards. Only two instructions remain spare in
the larger family. Native acceptance does not establish runtime flag semantics.

## Unchanged ordinary night

A kind9 Off / On / Off emulator run completed 5400 loops, 384 raw samples and
15 chunks. Full effects and the Pool2 producer stayed enabled. All six observed
cold windows contained one 12-vertex corona packet, zero eligible/requested
packets and one fallback. The sampled context was Hybrid color output.

Hybrid initializes 24-bit depth; the candidate's admission guard requires
16-bit depth. The original diagnostic ELF independently exposed actual depth
scale 8388607.5 and no request marker. Zero counters alone cannot distinguish
warm replay from inactivity, but the unchanged source depth guard independently
blocks this configuration. The three ordinary phase images look normal because
the original triangle route is retained. They are not positive SPRITE evidence.
No console pricing was run for this inactive candidate. Reducing depth precision
would be a separate quality/configuration change, not a valid ordinary-night gain.

The first strict analysis rejected timestamped emulator records because the new
corona parser expected bare LOG lines. A new parser version uses the qualified
LOG extraction and passes prefix controls and legacy regressions. Separate
reanalysis preserves the original rejected report and launch authority; it does
not invent a new target run or rewrite the old evidence.

## Separate capability attempt and flag hazard

The V2 diagnostic genuinely initializes a 16-bit framebuffer/depth configuration,
uses the original producer's camera orientation, and validates actual signed
spotlight selection and exact nonzero spot/fog uniforms. This is explicitly a
capability fixture, not the ordinary Hybrid scene or a performance comparison.

Both saved VU states contain 72 completed vertices. The candidate request marker
is present, but both outputs remain triangles and their complete vertex payloads
match. All twelve quads in each arm satisfy the completed-output predicate.
Positive SPRITE acceptance therefore failed. Captured diagnostic images are
black and are not accepted as visible texture or pixel-equivalence evidence.
Negative/split cases and later source epochs were not executed after this failure.

The preserved native schedule reads MAC flags with FMAND one issued instruction
after SUB at all six predicate sites. The pinned PCSX2 flag implementation exposes
FMAC results at four cycles and provides no VF dependency stall for FMAND.
Reading older flags is a source-backed explanation for the false negative; the
exact taken rejection branch was not traced. Naively adding three wait words at
six sites needs 18 instructions and exceeds the current two-word resident margin.
Any repair needs new scheduling, native residency and actual output/raster audits.

## 24-bit follow-up: isolated output and drawing pixels pass

The subsequent private experiment keeps the original Hybrid output and its
24-bit vertex-depth scale. It does not switch the scene to Bits16. The original
negative trial above and its archive remain unchanged.

The depth predicate is justified for **reachable FTOI4 output**, not arbitrary
28-bit integers. Exhausting 234,881,024 positive binary32 inputs in
`[1/16, 2^24)` verifies that converting their truncated, sixteen-times-scaled
integer back with ITOF0 is exact. Smaller nonnegative inputs produce zero.
An arbitrary integer collision (`0x1000000` versus `0x1000001`) is a negative
control: widening the guard alone would not establish correctness.

The private TC build changes three discarded SUB destinations to existing
scratch storage and pairs each of six FMAND readers with an upper ABS read of
that SUB result. ABS preserves the arithmetic MAC flags; the VF dependency
provides the required interlock. A strict TC-only postprocessor refuses changed
allocation/scheduling. This adds no instruction words: TC remains 506, full
VU1Clip residency including billboards remains 2,040 of 2,042 available words,
and all fifteen other images match the baseline. Disabling SCE latency handling
globally was tried separately and rejected at 2,064 resident words.

Native V26/V27 bind two 502-input diagnostic fixtures, unchanged ABI, 298 assets
and actual linked microprograms. Eight source-bound emulator captures cover:

- A planar positive case: 72 original vertices become 24 endpoint vertices,
  representing twelve SPRITEs. Completed ST/Q, RGBA and XYZ/F endpoints match
  the original triangles exactly after active fog and spotlight processing.
- Genuinely nonuniform final fog and final color: both retain the complete
  original triangle output byte for byte.
- Split 75/21-vertex packages: both fall back byte for byte.

Two further captures add an **off-clock three-vblank observer** after the final
render. The window remains black; that observation is retained and does not
qualify Hybrid scanout. Instead, saved GS version-9 state provides the actual
PSMCT32 drawing buffer. Saved FRAME0/SCISSOR0 bind its base, format, stride and
448×448 extent; source-pinned GS swizzle tables decode the pixels. Both arms
have identical complete RGBA drawing buffers, including 323 nonblack textured
pixels in the 17×19 corona footprint. The enlarged image is a nearest-neighbor
view of that actual decoded footprint, not a new render.

**Result: the 24-bit isolated SPRITE route and positive drawing raster work.**
This is not ordinary-scene activation, physical raster acceptance or an FPS
measurement. No console deployment, pricing, production promotion or project
format change was made in this follow-up. Correctness observer waits are not
part of a pricing implementation.

The [follow-up archive](tyrax2-corona24-2026-10-05/README.md) preserves exact
source deltas, reconstructable inventories, rejected compiler attempts, actual
packet comparisons, raw protocol records, native reviews and drawing images.

## Remaining work and preservation

A production candidate still needs ordinary-workload acceptance coverage,
Hybrid presentation checks and both-order physical pricing. The isolated
follow-up addresses reachable depth, scheduling, completed output and positive
drawing pixels; it does not cover every camera, depth, clipping or replay path.
Removing guards or switching the pricing scene to Bits16 would not satisfy
these requirements. These trials establish no universal PS2 performance limit.

The [source and evidence archive](tyrax2-gs-sprite-coronas-2026-10-05/README.md)
preserves exact source revisions, native budget failure/success, parser repair,
ordinary raw records, saved-output diagnosis and reconstruction proofs. ELF,
objects, resource blobs and savestates are hash-only. Production code and
project format were not changed.
