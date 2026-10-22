# A38 Current Hybrid Double/Triple Boundary

Review A37 actual physical PC residency and existing frame-pacing.md first:
VBlank31..34%, OFF moving returns49.26/41.18Hz; no single non-wait monopoly.
Known Hybrid triple feature is NOT a new algorithm. Old synthetic PAL load and
emulator correctness do not price current physical roads/reflections/night.

Private same-ELF six trips2/3/2 in day/night, common A31/A30, A35 OFF. No PC
sampler. Actual settings toggle with prior-frame/queued-present/PATH1/PATH3
drain then texture eviction and full permanent-buffer rebuild. Assert actual
buffer grant, retain Hybrid448x448 quality, VU programs and geometry. VRAM
pressure and extra Hybrid FINISH are included in moving net work, not hidden
by reserving three buffers in the reference. Initial rebuild/warm-up excluded.
Moving scene reload recreates shadow owners; held2/3/2 rebuild calls existing
setupProjShadows outside price. Force live held reflection through original
checkpoint contract.32 held frames per arm, exact main/env and actual source/
delivery/header contracts. Restore actual moving configuration after checkpoint.

Bounded1200-row actual DISPFB programming event capture, gas only, per-trip
export stopped. ISR flag distinguishes queued VBlank latches from immediate
double-buffer presentation; no extra ISR IO/allocation/timer. Events indicate
hardware register hand-off, NOT television photons. Pending last job/display
is completed before export. Boundary frames may belong to the preceding parked
job; don't conflate entryHz with displayed real60. No synthetic frames (Hybrid).

Require both target correctness before physical price, route/lights/live capture,
exact raster return, actual2/3/2 grant, no allocation/fallback/lifetime errors,
monotonic valid events and first-order net tails/spatial comparison. Reverse
required before promotion. No production default change. Initial implementation
pending generator/native/emulator/PS2 qualification.

V1 native491/ELF108375b7...df5cf and six hidden trips complete. Initial reader
rejects FRONTORACLE raw head hash equality: VRAM relocation changes live TEX0
addresses (the original SDK head writer is unchanged, explicitly parent-pinned).
Do not reuse A35's same-layout whole-head hash contract across different VRAM
layouts. Retain head counts/lengths, canonical source/residual delivery and ALL
36 exact main/env rasters. Manual all-six raster comparisons PASS. Independent
reader repaired to that relocation-specific contract; complete acceptance pending.

## Completed First Order

[603-file V1 evidence](tyrax2-hybrid-triple-2026-10-11/v1/README.md) now passes
closed both-target six drives, actual grants,36 exact rasters each, unchanged
parent-pinned head writer, geometry/header lengths and14 rejecting controls.
Both copied readers replay byte-identically. Editor-equivalent hardware reset,
owned15024 stopped after complete export; emulator428/Xvfb417 stopped. Devices ON.

Physical actual DISPFB hand-offs: day49.629/58.928/50.422Hz; night41.515/55.217/
41.401Hz. Candidate day9/524 intervals>25ms versus91/438 and84/445 references;
night42/491 versus162/365 and163/364. Day p9516.687ms, night33.366. This is a
substantial net pacing gain in both directions of the first-order bracket,
not stable60 and not exclusive EE instruction savings. Mean non-pacing work
14.097/16.313 candidate is near references13.890..13.963/16.019..16.104.
Heap pressure1.91406 versus1.53125MiB after real rebuild is included in steady
drives. Both net gates PASS, both stable60 gates FAIL. No production promotion.

Next reverse3/2/3 in both day/night before retaining/promoting the feature.
The fixture now accepts `--reverse`, still cloned from the same bound A37
ordinary baseline. Only moving selection/restoration changes to3/2/3; held
checks remain2/3/2. The reader validates actual mode/grants/ISR against the
manifest and compares BOTH triple returns against the middle double control.
It cannot reuse the first-order candidate index for reverse pricing. Separate
reverse-v1 evidence is required; original V1 archive remains immutable.
Then source audit of whether an already completed old job waits to be published
until next-frame EE preparation reaches submission. Existing ordered interleave
attribution report already explains N/N-1 mixing; a nonblocking ready-present
intervention would be a different test, not deleting FINISH or field limits.

## Completed Reverse

[Reverse-v1 evidence](tyrax2-hybrid-triple-2026-10-11/reverse-v1/README.md)
passes native491/ELF6f6ea95f...daae3, both full six-trip/36-raster/14-control
gates and actual3/2/3 moving grants. Only two bound selection/restoration files
differ from V1. Reader compatibility reproduces the old physical report exactly.
603-file reverse archive copied readers replay both new reports byte-identically.
Physical DISPFB day58.818/48.836/58.815Hz; night55.104/40.832/54.889Hz.
BOTH triple endpoints improve versus double and retain p95, both net gates PASS.
Still10/10 late day intervals and43/45 night; stable60 remains FAIL.
Mean non-pacing work barely changes; no packet-code savings claim. Normal editor
network reset succeeds; client6428/emulator26/Xvfb15 stopped after complete export.
Devices stay ON. No production default change; common private A31/A30 baseline
does not replace normal-production integration/boot qualification.

## A39 Ready-Presentation Source Audit

No runtime intervention implemented yet. Unlike H01 sealed replay, C05 chain
fusion or H05 lookup work, this changes publication timing under the SAME moving
workload and three-buffer baseline. Source establishes one pending old job,
completed at the next flush callback, not a measured count of avoidable deadlines.
`Vif1Queue::waitFor` unconditionally calls `flushRecording`: calling the ordinary
completion routine mid-prefix can prematurely submit the current frame.
Existing completeBeforeSubmission suspends recording/protects its bank, but its
caller has ALREADY cleared nativePendingSequence. A new early-ready path must
preserve that unsubmitted reservation too, not reuse the callback blindly.

Try only after old DMA sequence AND exclusive final GS FINISH are ready and the
Hybrid display queue has a free target. No flush/wait on a not-ready old job;
retain normal fallback submission completion. Protect current native writer,
prefix reservation, HUD fence and logical recording/raster state. Initially
restrict Hybrid3/no field/BLSS/raster redirect; restore live state on return.
Do not run copy against already-submitted current geometry, remove FINISH, or
add an asynchronous worker that races renderer state. Positive actual activation,
host prefix/sequence/lifecycle controls, native binding, exact held source/main/
env gates and same-ELF physical Off/On/Off required before pricing/promotion.

A39 V1 fixture now implements that protected path privately. All moving/held
arms allocate actual Hybrid3; candidate is middle Off/On/Off in day/night.
Core checks old pending/Hybrid/no field/BLSS/redirect, free display target and
exclusive GS FINISH; queue polls actual old DMA without flush. RAII suspends
recording/protects bank/HUD and hides/restores current nativePendingSequence.
Target asserts native writer/checkpoint/arena usage/bank unchanged across old
presentation and restores recording context. Polls at recording start and
StaPip entry, never worker/ISR. Bounded gas/checkpoint activation witnesses only;
no new timer/scoped clocks. ready-check adds positive protected-prefix activation
to complete inherited travel/source/36-raster/presentation gates. No target
qualification or price yet; host guard matrix is component-only evidence.

V1 native fails missing TYRA_ASSERT include; V2 fixes it and builds, but source
review finds inherited held per-frame synchronization would never activate the
candidate. Neither version is priced or deployed. V3 held checkpoints use normal
overlap and an identical40ms qualification-only pause after new clear recording,
before geometry, in ALL arms. This allows old DMA/FINISH/display ownership to
become ready while a current prefix remains unsubmitted. Require positive
protected activation in ALL six held candidate arms as well as both moving
ones, with exact output; pause never enters moving price. Actual Core/queue
methods extracted by verify-ready-runtime.py pass4096 modeled state/sequence/
wrap/lifecycle controls optimized and sanitized; hardware flags are mocked,
not target completion/timing proof. V3 native/full target gates pending.

V3 native then fails unqualified FrameProfile name in checkpoint pause. V4 uses
direct CP0 Count; V5 adds explicit try-ready after the common held pause, before
reflection barriers could consume the old pending job. V4 not deployed/priced.
V5 native build succeeds; actual-method modeled host4099 controls pass optimized/
sanitized (plus32 guard-state cases). Source-pin reports and compiler/runtime
logs retained. Native mirror binding/full target gates still required. Do not
accept held rasters without the positive protected-prefix witness.

V5 binds493 native sources/ELFcc4eaae5...a5f5b. Initial hidden held candidate
executes30..31 protected-prefix presentations, moving first candidate checks42263
but executes ZERO. Emulator scheduling is not physical activation evidence.
Reader explicitly allows zero emulator MOVING presentations only (still positive
checks), while ALL emulator held candidates must actually present protected
prefixes with exact rasters; PS2 moving candidate phases must both activate too.
Inactive hardware candidate cannot be priced/promoted. Earlier statement of
required emulator moving activation is superseded only by this platform-specific
correctness distinction, not by weakened PS2 or held gates. Full hidden pending.

V5 closes full hidden six-trip/36 exact-image/17-negative gates. Every held
candidate executes30..31 protected presentations; moving day0/night1 only.
Owned26/15 stopped after DRIVEDONE/export. Same ELF is now on PS2 via editor
network reset, client5168. Physical first moving candidate activates190 protected
presentations/37106 pending checks and held31; full return/night/reader still
pending, no physical performance claim. Do not infer emulator activation rate
or host simulated flags represent physical scheduling.

A39 V5 then closes BOTH targets/full gates with actual PS2 moving190/340 and
held31 protected presentations;36 exact rasters each/17 controls. Net FAIL:
day58.815/58.701/58.813Hz, night54.338/54.013/54.450. Candidate slightly worse
against both controls; no stable60/promotion/reverse. [A39 result](tyrax2-ready-present-2026-10-11.md)
and621-file copied-reader exact archive preserve the rejected candidate. Devices
ON; owned5168 stopped after export. Return to ordinary three-buffer publication.
