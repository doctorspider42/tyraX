# Main-only night geometry batching experiment

The eleven night-only boxes in Motor District add eight illuminated windows
and three garage trim/blade objects. The original mood script toggles their
visibility, so disabling lighting effects does not make day and night equal
geometry workloads. These objects are individually submitted and reflected;
forcing the existing static-batch flag would lose their separately owned
reflection carriers.

The private kind29 experiment kept those carriers and copied their original
world-space positions and RGBA into three independently owned main-view
groups of 4, 4 and 3 members. It retained the authored visibility/reflection
flags, original object-loop eligibility and heavy interleave work. Replacement
required contiguous opaque draws, an already exhausted deferred heavy queue,
fully inside frustum bounds, matching info/depth/alpha semantics and identical
actual dynamic-light selection and effective spotlight filtering for every
member and the combined bag. Dirty geometry, unsupported modes or a changed
exact key fell back to the original main draws.

## Result

Both native-built emulator trials completed 5,400 loops using one ELF per
trial, full night, the ordinary sampler and Off/On/Off phases. V2 deferred
the entire private preparation/replacement until global loop 600 and added
sparse refusal diagnostics. Neither version accepted a candidate group in
any of its six cold windows. Each window retained 11 original main
submissions and 11 individual carriers, with 396 position/RGBA vertex
comparisons and zero mismatches. All three proposed groups remained demoted.

The parent inspected the three owned phase images from each trial and found
the car, map, lamps, shadows and HUD normal without obvious stretched
triangles. This is a qualitative emulator observation of the original
fallback path, not visual qualification of active merged rendering.

These are completed **zero-activation diagnostics**, not batching speed
results. No PS2 pricing was performed, no gain is claimed, and the candidate
was not promoted to production. The experiment ends here.

## Why the guards refused

V2 accumulated `differentPick` for the two window groups and
`differentEffectiveSpot` for the garage group. Their captured `keyAdmitted=0`
means the initial lighting qualification had already failed. A larger merged
sphere can change the engine's chosen dynamic lamp, and its larger bounds
can change whether the selected spotlight contributes. `dynLit=0` does not
disable that dynamic spotlight path.

All three groups later reported exact-key word 147 changing. The source
decodes this as dynamic scene light slot 6's red channel. In the full-night
eight-lamp inventory this is District night lamp 7, authored with flicker 0.025.
Its two-sine intensity multiplier updates each gameplay frame; pausing the
sky cycle does not pause this lamp. Delaying initial preparation therefore
cannot make the field settle. The exact key conservatively included every
scene light, so this changing lamp invalidated all groups.

The diagnostics do not identify the offending member or prove that these
objects can never be batched in another view or declared lighting policy.
Removing the guards would change the rendering contract. A future optional
player-only dynamic-light receiver policy is separate work, with an explicit
visual tradeoff; it is not part of this equivalence experiment or evidence
of a batching gain.

## Evidence and limits

[The dated archive](tyrax2-main11-batch-2026-10-06/README.md) preserves both
frozen 501-file source postimages, native/ABI/source/parser proofs, independent
reviews, repaired fallback accounting, preflight rejection, helpers, owned
launch/capture records, six images, root summaries and the source diagnosis.
ELF/symbol/native-object/executable files are hash-only; generated runtime
assets are identified by their manifests/provenance. No prior archive was
overwritten.

The verifier can check current files or the staged Git index and all 1,002
source references. It verifies archival identity, not execution replay or
the omitted binary bytes. Source/host controls demonstrate guard/schema
behavior; native proofs establish the recorded target build, not active
merged output. Cold activation does not establish timed-window activation.
Reflection packet cadence and active merged VU/GS output remain unqualified.

Both arms paid common carrier/group preparation, key checks, additional
eligibility work and memory footprint. Initial carrier preparation can clear
the dirty bit before the reflection content key hashes it, so common setup
is not an untouched-production baseline. Ready-but-demoted changed geometry
can safely use original draws while invalidating the equality capture.
None of these common costs or fallback-only timings is presented as a
production saving.
