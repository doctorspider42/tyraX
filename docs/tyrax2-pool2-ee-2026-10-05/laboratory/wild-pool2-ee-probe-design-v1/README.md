# Pool2 lazy-source output probe: source-only design

This private workshop adapts the prior fixed-case correctness probe to the
current `wild-pool2-ee-physical-v2` engine. **It has not been compiled or run.**
Run `python prepare.py` only to regenerate this workshop's files from pinned
source inputs; it does not alter any source fixture, build or device.

The normal `source/pool2_static_probe.hpp` retains the original strict
`host:pool2-probe.cfg` three-integer parser: arm0/1, case96/192, repeat1/2/3.
Twelve complete config files are provided. It retains the prior renderer setup,
texture, camera, geometry and bag settings, including package75 and flag kind7
(`NightAblation::setPoolTableEnabled`). The lifecycle remains
beginFrame → renderer3D.usePipeline → render → endFrame → synchronizeFrame →
READY → owned nop halt. No new target wait, fence or register read is inserted.

Arm0 binds the persistent preexpanded baseline Color array with no expanded
View backing. Arm1 binds a separate persistent 16-byte Color array initially
poisoned with 0xa5 and a uint32 ready mask initially zero. View generation and
color contentVersion share the typed colorGeneration word; geometry/ST and
bbox retain the original generation word. The candidate backing is not
preexpanded. Counter collection (including its cold expansion oracle) stays
disabled on all repetitions; unchanged counter bytes are asserted. After normal
synchronization the probe scans ready words, compares only materialized colors
against current baseline coefficients and checks that every unready color/ready
word matches its pre-render snapshot. Cold unready bytes start poisoned; older
fallback generations may remain unchanged outside this epoch's consumed range.
These are correctness diagnostics, not timings.

Every iteration authors an explicit identical color epoch in both arms:
member0 red is31/32/33 and member1 red is95/94/93 on repetitions1/2/3.
colorGeneration advances1/2/3, View generation follows it and colorVersion
points to it. Baseline expansion is rewritten; lazy backing is never rewritten
by the probe after its initial poison fill. Final raw VU RGBA must reflect the
current epoch, independently of prior-bank equality.

The ordinary inside case requires candidate readyCount0 after each render.
This independently checks that the expected table route did not materialize its
source. A real fallback would cause rejection here and require route analysis;
it must not be silently described as an eligible compact run.

`source/pool2_static_probe_clip.hpp` selects the separate compile-time clip
case. Its include replaces the ordinary probe include in a *new private*
correctness fixture, or define POOL2_EE_PROBE_CLIP=1 for that separate build.
The original config format and ordinary geometry are unchanged. Each triangle
uses finite x=-1000/+1000/0 coordinates at z=0, straddling both side planes from
the existing z=-20 camera. Existing VU1 clipping remains disabled, aiming to
exercise the EE classification/copy/clip fallback. Candidate readyCount must
be nonzero and all materialized source Color bytes must match the baseline.
This intended route needs target validation; source preparation does not prove
that clipping ran, which fallback subroute ran, or the output-buffer equality.

For root's later serial native preparation, use the current V2 engine unchanged,
copy the selected workshop header(s) into the new fixture's game/inc, retain
the prior dedicated init/loop hooks and bind all engine/source hashes before
building. Root should capture both arm outputs for both96/192 cases and all
repeats through the established paused SaveState VU1/capture workflow. Verify
EE READY and halt PC, output epoch/freshness limitations, packet/VU bytes and
source-ready evidence separately. Captured epoch color words must correspond to
the final requested repetition; capture logs alone do not prove output freshness.

Prior helpers require adaptation:

- `wild-pool2-static-probe-root-v1/finish-probe-v2.py` currently requires
  eligible/applied/compared counts per package in every FRAME record. Those
  fields now remain zero, and new SOURCE records must prove generation1..N,
  readyCount0 for ordinary candidate or readyCount>0 for clip candidate.
  Its `warmFreshnessIndependentEpoch=False` metadata must be qualified using
  independently decoded final epoch output, not changed unconditionally.
- `wild-pool2-static-probe-design-v2/verify-capture.py` pins a500-file manifest
  and describes warm counters retaining cold values. Refresh the exact source
  closure expectation and limit text; add final epoch RGBA validation.
- `wild-pool2-static-probe-design-v2/decode-vu1-output.py` can retain the ordinary
  inside package/marker/header checks and paired-byte comparison, but needs
  explicit expected epoch RGBA validation. Clip fallback has marker0 in both
  arms and different package/output topology: its inside-only case decoder is
  unsuitable for that separate fixture.
- `wild-pool2-static-probe-root-v1/capture-ready.py` hardcodes those verifier and
  finish helper paths. Retain READY/halt/SaveState mechanics in a private adapted
  copy, refreshing source/ELF pins and selecting the matching epoch/clip verifier.

`source-proof.json` records input/output SHA-256 values and source-only status.
No native compiler, emulator, console or shared build/cache resource is used.
No candidate/production files are edited. No performance claim follows.
