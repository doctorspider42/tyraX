# Private GS SPRITE corona experiment

This implementation is not promoted. It cannot admit the unchanged ordinary
night workload, and its separate 16-bit capability probe did not produce a
SPRITE even when every completed quad satisfied the intended predicate.
No physical timing benefit or positive SPRITE raster result is accepted.

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

## Remaining work and preservation

A future version needs a proved 24-bit depth-domain predicate, correct flag
scheduling within the full resident budget, positive completed SPRITE output,
visible texture/pixel controls, ordinary workload activation and both-order
physical pricing. Removing guards or switching the pricing scene to Bits16
would not satisfy these requirements. This trial says nothing about a universal
PS2 limit or every possible GS SPRITE implementation.

The [source and evidence archive](tyrax2-gs-sprite-coronas-2026-10-05/README.md)
preserves exact source revisions, native budget failure/success, parser repair,
ordinary raw records, saved-output diagnosis and reconstruction proofs. ELF,
objects, resource blobs and savestates are hash-only. Production code and
project format were not changed.
