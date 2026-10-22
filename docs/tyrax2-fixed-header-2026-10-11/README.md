# A26 fixed-size standard header

This private fix removes dynamic SDK UNPACK-length calculation for the fixed
two-qword standard StaPip header, without changing geometry, clipping or DMA
ownership.

The original writer emits scale/count and one primitive GIFtag, then calls
packet2_utils_vu_close_unpack, whose final packet2_vif_close_unpack_auto computes
the already known two-qword length. Candidate mode 7 retains padding/tag closure
but uses packet2_vif_close_unpack_manual(packet,2). No cache or new timing scope.
Original mapping, count/mask packing, GIFtag and program inputs stay unchanged.

Six same-ELF day/night 0/7/0 drives retain roads, three vehicles, live captures,
real dt/camera sweep and EE paint. Sparse byte oracles exclude offset%128 64/65.
The oracle invokes the actual linked SDK auto/manual closers on the original
open-header state and compares all three finalized chain qwords plus next/open
metadata. Held-pose 0/7/0/0/0 checkpoints require exact main/env images in EVERY
arm, identical ordered clip input witnesses and positive matched header checks.
Gate: positive mean/spatial savings and p95 <= slower adjacent Off +0.15 ms;
reverse-order/lifetime qualification remains necessary before promotion.

## Completed A/B

Native source/mirror binding covers 476 files; ELF SHA256:
`dfb1f95ef27e2dd267972b45b65e44cdeab6ca102099ed1a0389d33299ec8dbf`.
Fixture: `F:/Projects/tyrax2-fixed-header-20261011-v1`.
Both emulator and PS2 complete all six trips, all 60 raw main/env readbacks,
actual linked-SDK byte/state checks and identical held-pose clip input hashes.
All five checkpoint arms have exact pixels. Both readers reject 13 corrupted
controls. Sparse legacy `clipCalls`/`eligible` fields count HEADER closes here,
not clip jobs; candidate eligible equals closes, baseline eligible is zero.
Oracle frames and their successors are excluded from timing.

Physical work (milliseconds; positive saving means faster):

| Condition | Off before | Fixed close | Off after | Mean saving | Equal-distance saving |
| --- | ---: | ---: | ---: | ---: | ---: |
| Day | 18.503496 | 18.532973 | 18.546995 | -0.007728 | +0.079435 |
| Night | 20.687013 | 20.736038 | 20.684828 | -0.050117 | -0.040736 |

Candidate p95 is 24.657511 ms day and 28.164961 ms night; slower adjacent
controls are 25.110375 and 28.146844 ms. Candidate entry cadence is 35.733/33.464
Hz. These are elapsed net moving-frame values, not isolated EE cycle costs.
Do not compare absolute times with other experiment ELFs to price this change.
Follow-up A27 finds ordinary TYRA_LOG remains active in this derived baseline
despite disabled profilers. Both A26 arms have the same log policy; its delta
remains a same-policy result, but cadence is not a no-ordinary-log ceiling.
See [A27 baseline audit](../tyrax2-log-gate-2026-10-11/README.md).

**Decision: first-order gate FAILS.** The known dynamic length calculation is
unnecessary, but removing it has no useful demonstrated gain. Keep this fix
private; no default or version change, no reverse-order qualification needed
for a rejected candidate. Do not reopen this as the larger EE bottleneck.

Sources, logs, ELF, images, reports and tools are preserved in `header-v1/`.
The 637-file inventory passes read-only archive validation; reanalysis of both
platforms writes outside the archive and reproduces each report byte-for-byte.
The console was deployed using the editor network-reset protocol; owned host
client and hidden emulator processes were stopped only after completion.
Console and computer remain powered on.
