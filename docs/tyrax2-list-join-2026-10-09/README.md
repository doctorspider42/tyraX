# A19: contiguous list gathering

Private offline continuation of [A18](../tyrax2-three-path-2026-10-09/README.md).
The original A19 session was offline while the user could not reset PS2.
A22 below has renewed console-test authorization. Neither trial modifies the
production renderer/default or authored map; no power-off is authorized.

## Source audit

The audit follows `StaPipCore::render` through package classification,
`StaPipQBufferRenderer::addBuffersDataToPacket`, `Vif1Queue::submit` and
`FrameChainArena::copyKnownRefsCounted`.

- Whole/partial baked replay already bypasses substantial writing. Diagnostic
  fresh-writer validation is gated; it is not an unconditional ordinary cost.
- The native queue uses counted snapshot metadata. A separate sizing walk is
  a fallback, not work performed on every eligible submission. Inline payload
  is subsequently referenced from its owned snapshot, not copied a second time
  by native translation. Snapshot preflight/copy and tag translation remain
  distinct passes with ownership/format responsibilities.
- Whole-bag transforms have an exact-value reuse check. Inactive spot lighting
  returns before matrix inversion; it is not an unconditional day-time inverse.
  A17 already excluded recurring normal normalization in the measured workload.
- A failed whole-bag guard-band shortcut can calculate the same eight clip
  planes twice inside one `render` call: once for the shortcut, then again for
  package classification (`stapip_core.cpp`, calls near 749/761). The method
  itself has no memo. A local readiness flag could avoid the second call, but
  A22 reconciliation found this same-call reuse already tested in H07
  (`tyrax2-clip-reuse-2026-10-02.json`), with a 0.73-0.89 ms regression and an
  unresolved interleave confound. Do not reopen it as a new finding or confuse
  it with P03's cross-road exact-key memo (only 0.023-0.035 ms benefit).
- Strip boundary geometry still multiplies small clip dispatches. Expansion
  payload caching does not remove those dispatches, but P08's per-chunk command
  memo was slower. Its rejection is not permission to remove capacity bounds.
- The list route first gathers accepted subpackages in groups of three/two,
  then submits the remaining boundary subpackages. It uses two reused index
  vectors and membership searches; it does NOT allocate both vectors afresh
  every call. Changing that bookkeeping alone is not an architectural remedy.
- A concrete avoidable representation change remains: `fillByCopyMax` and
  `fillByCopy1By2` copy every accepted group, including adjacent slices of the
  same original arrays. Existing grouping can instead describe that contiguous
  range without changing triangle order, group size or clipping policy.

These are source facts, not prices for the individual operations. A18's
roughly 16 ms interim endpoint difference cannot be assigned to one item above.

## Private qualification

`tools/tyrax2-list-join.py` derives a fresh fixture from the bound A18 BagTape
V5 sources using the current clean pricing generator. It leaves ordinary
controls and the captured command replay unchanged. In descriptor-list stages
2 and 8, it runs the ORIGINAL gather first, then:

1. Requires two/three nonempty triangle-list slices from the same bag.
2. Checks contiguity of positions AND each active UV/colour/normal stream.
3. Preserves the exact combined count and OR of clip-plane masks.
4. Compares every active byte against the original gathered output and checks
   metadata. A mismatch aborts; it is not hidden by falling back.
5. Replaces that qbuffer's gathered pointers with the original range. All
   existing packet writing, queue snapshots, immutable leases and fences run.

Noncontiguous groups retain the original copy. No cache is introduced. Since
the original copy and oracle deliberately remain, this build cannot price a
copy-removal optimization. The candidate runs on the owned descriptor arrays,
not ordinary producer storage; the latter's lifetime/registration consequences
are explicitly NOT qualified here. All scene state is frozen, not a moving FPS
test. Counts are the final checked frame in each list stage, not a population
estimate over gameplay.

Host tests compile the exact helper under ASan/UBSan and cover all eight active
stream combinations, two/three slices, mismatched owners, topology, gaps/nulls,
invalid counts, disabled operation and refreshed mutable content. 224 checks
pass; a deliberately corrupted gathered colour terminates with SIGABRT.
The reader passes positive controls and seven negative controls (lost stages,
wrong activation, impossible counts, missing bytes/completion, changed pixels
and a disabled qualification manifest). Inherited A18 validation also checks
bound source/ELF identity, startup program, complete samples and all GS images.

Native binding: 477 files, ELF
`608534ef15dee36a63c1017a32f067d7fa43285177e340dac92d3bb3be219c08`.

## Results

All four hidden-emulator runs completed ten stages each, including ordinary
returns. All ten main and ten reflection readbacks are byte-identical WITHIN
each run: 80 actual GS images in total. Both list check frames agree in each
state. No physical performance result exists.

| Frozen state | Gather calls | Contiguous groups | Bytes eligible per checked frame |
| --- | ---: | ---: | ---: |
| Day drive, phase 0 / offset 1380 | 22 | 20 | 20,448 |
| Night drive, phase 3 / offset 1380 | 22 | 20 | 20,448 |
| Later drive/view, phase 0 / offset 1500 | 20 | 15 | 17,616 |
| Parked, phase 0 / offset 1250 | 11 | 11 | 13,392 |

The later-view fixture is named `turn` on disk by the inherited A18 convention;
it is a frozen later driving snapshot, not a live camera-turn benchmark. Do not
compare this ELF's times or night geometry totals against A18 V4: pre-freeze
simulation uses real dt and the night capture has 795 VU starts versus V4's
798. Same-run ordinary/list/command comparisons are the raster controls.

Evidence: [day](day/archive-sha256.json), [night](night/archive-sha256.json),
[later view](later-view/archive-sha256.json), [parked](parked/archive-sha256.json).
Each archive has 540 hash-verified files, including bound sources, ELF, logs,
raw GS data, reports and diagnostic tools; assets are hash-listed rather than
duplicated. Read-only archived reanalysis passes all four states. The day
archive includes the seven analyzer rejection controls. Native build, Python
compilation and `git diff --check` pass. Both authored map hashes remain those
recorded by A18. All four owned emulator/Xvfb pairs were stopped after completion;
PS2 and its waiting host were not touched.

## Decision gates

Do not enable this candidate based on the source simplification or emulator
clocks. Its narrow 20 KB daytime opportunity does not establish that it explains
the A18 gap. A production candidate would need original-producer/mutable data,
non-recording and retirement/fallback qualification, and counter-off physical
controls. Keep it private and lower priority; these data establish avoidable
work, not a large bottleneck or a reason to start another cache-tuning series.
Preserve the clean A18 hardware test as the main next measurement
after the user returns. Do not repeat resets while the console is unavailable.
## A22: actual copy bypass (2026-10-10)

A22 uses ordinary scene producers in every arm, not A19's owned descriptor
arrays. Candidate stages 2/4/6/8 skip the existing 2/3-part gather only when
every active stream is contiguous, with the same bag, count and clipping mask.
Noncontiguous groups retain the original copy. No queue ownership, snapshot,
lease or fence is removed. Setup review found no new qualified shortcut:
same-call clip-plane reuse was already tried, and normal preparation already
hits its cache. The concrete intervention is therefore Geometry's redundant
gather, not another set of timers or an assumed multi-millisecond saving.

The private generator `tools/tyrax2-list-join-fast.py` derives from bound A18 V5.
All stages render normally; no A20/A21 scopes are included. Timed frames are
64..191. Frame 222 runs the original gather and byte oracle outside sampling;
frame 223 captures the actual copy-bypass path, without the gather oracle.
Off stages surround each candidate in the same ELF. Host checks, emulator
rasters and complete physical A/B are required before accepting a price.
This frozen test does not qualify moving mutable producers or non-recording
lifetimes for default production enablement.

### A22 source and offline verification

Fixture: `F:/Projects/tyrax2-list-join-fast-20261010-v1`, derived from A18 V5.
Native source/mirror agreement covers 479 files; ELF SHA-256:
`f953266749466db93ef72907e5e05118fa79d3100373006c8234606af93bcdd4`.

- The call sites are the 2/3-subpackage groups in `renderSubpkgs`, followed
  immediately by `qbufferRenderer.cull`, never the writable legacy EE clipper.
- `fillByPointer` copies pointer values/size/mask into the qbuffer, not the
  temporary package's address. It uses the same source storage contract as the
  already-existing one-subpackage pointer path. Queue snapshot/lease handling
  is untouched, and group count/order/VU capacity checks are retained.
- Host ASan/UBSan: 304 checks of the exact predicate/bypass, all eight stream
  combinations, fallback, active/oracle gates and metadata. Deliberately
  corrupted gathered colour exits with SIGABRT (-6), as required.
- Emulator NIGHT: 10 stages, 1152 samples, all 20 main/env images exactly equal.
  Every candidate oracle finds 20 accepted groups out of 22, 20,448 bytes;
  every Off oracle reports zero. Capture frame is the actual bypass, not the
  copy-and-compare oracle. No performance is inferred from emulator timings.
- Reader: 18 rejection controls pass, including absent/zero/drifting candidate
  activation. The previous A21 reader's 28 controls still pass after extension.
- PS2 deployment uses the editor network-reset procedure once; owned client
  PID 22152, with a fresh ELF load and full physical completion confirmed.

The helper tools remain in the repository's ignored local tooling area, as
earlier experiments do; the final evidence archive preserves their exact
sources. No production default is changed by this private experiment.

### A22 accepted physical result

The NIGHT run completes all 10 stages and 1152 timed samples. All 20 main/env
GS readbacks match exactly, every descriptor/state check passes, and bound
source/ELF plus physical launch-input pins remain unchanged. Every On oracle
accepts 17 of 18 groups (17,040 bytes); all Off rows are zero. Scene: 121 bags,
125 events, 8 lights, 805 captured VU starts, z=-56.974152, speed=14.771075.

| Stage | Path | Mean non-pacing work (ms) | Preparation (ms) |
| --- | --- | ---: | ---: |
| 1 | Copy | 28.918714 | 18.242722 |
| 2 | Joined | 28.631542 | 17.955707 |
| 3 | Copy | 28.917182 | 18.233338 |
| 4 | Joined | 28.633966 | 17.955350 |
| 5 | Copy | 28.913810 | 18.232987 |
| 6 | Joined | 28.635650 | 17.959272 |
| 7 | Copy | 28.911600 | 18.230068 |
| 8 | Joined | 28.634096 | 17.955074 |
| 9 | Copy | 28.918719 | 18.233564 |

All eight adjacent-control comparisons save **0.275950..0.287172 ms**, roughly
1% of this workload. Preparation falls by about 0.27..0.29 ms; this is an
end-to-end net effect, not an isolated memcpy instruction price. There are no
new timing scopes. Checks/oracles stay outside the sampled window, and the
actual bypass path (not the oracle) supplies the checked images.

**Decision:** accept a small, repeatable frozen-scene improvement. Keep the
private candidate for moving-scene and mutable-source/lifetime qualification;
do not enable it in production yet or claim stable 60 FPS. This operation is
real redundant work but does not explain the broad StaPip preparation cost.
Do not reopen it as unpriced or add more counters to this same hypothesis.

Immutable evidence: [fast-night-v1-20261010](fast-night-v1-20261010/), 587 hashed
files including the modified source, ELF, host/reader controls, logs, raw
rasters and samples. Read-only emulator and physical archive reanalysis pass.
Owned ps2client PID 22152 remains available; the owned emulator was stopped.
Neither console nor computer was powered off.

## A23 moving qualification (completed; no default promotion)

The user authorizes driving qualification and default enablement if A/B passes
without regression, followed by continued investigation of the larger cost.
`tyrax2-list-join-drive.py` derives from the bound lean A16 drive fixture; its
qbuffer copy implementation is byte-identical to the current production file.
The rejected target gate stays off and paint stays EE in all arms.

One ELF runs twelve full-throttle trips: Off/On/Off and On/Off/On in daylight,
then the same two triples at night. Roads and live environment capture remain
active, with three vehicles and the existing camera sweep. Real dt is retained.
No new scope clocks are added. Sparse moving On frames at offset%128==64 run
the original gather and byte oracle; those frames and the next frame are
excluded from pricing. Witness counters run only on these cold checks.

After each trip, the current scene is held for three ordinary-source renders
Off/On/Off. Eight settling frames per arm force current reflection capture,
pin the existing interleave and GS target, and read main/env VRAM. These 72
images are correctness evidence outside the timed route. No descriptor replay
or command replay is used. Existing queue snapshots, leases and fences remain.

The predeclared promotion gate requires complete valid travel, equal rasters,
moving byte-oracle activation, unchanged source/ELF/resources, positive mean
and equal-distance savings in all four triples, and candidate p95 within
0.15 ms of the slower bracketing control. This tolerance covers small timing
jitter; raw values must be published. An emulator run qualifies behavior only.
Physical pricing and the gate determine promotion; failures stay preserved.

The complete A23 emulator run now passes twelve trips, 72 main/environment
readbacks (all within-checkpoint comparisons exact), moving gather byte
oracles, source/native/ELF binding and eleven reader rejection controls.
The physical run uses the same ELF, editor-network-reset deployment and
ordinary moving producers. All twelve trips, 72 readbacks, moving byte
oracles and eleven reader rejection controls pass there too. The console
completed without allocation/assertion/DMA errors. Both runs use ELF
`9b7e06f69a56b265ac966b368d2ffedd1dd513b54428f21c16b874dbf6cd29a7`,
bound to 474 source files. Emulator timings are not used to promote the change.

Physical differences below are Off minus On. Work excludes pacing but still
includes rendering waits/preemption; it is not pure EE instruction time.
Spatial means give equal weight to eight 20-unit route bins, not frame numbers.

| Mood / order | Mean saving ms | Spatial saving ms | On p95 ms | Slower Off p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Day Off/On/Off | 0.017487 | 0.050224 | 24.301453 | 24.971439 |
| Day On/Off/On | 0.022709 | -0.049211 | 25.318329 | 25.139476 |
| Night Off/On/Off | 0.241963 | 0.248621 | 27.215363 | 27.799496 |
| Night On/Off/On | 0.129396 | 0.328657 | 27.339060 | 27.523922 |

**Promotion gate fails.** The reversed day triple has a negative spatial
difference and p95 is 0.178853 ms above its control, beyond the predeclared
0.15 ms tolerance. This small difference need not establish a causal regression,
but it does not establish the requested regression-free moving gain either.
Do not relax the gate after seeing the data or promote from night alone.
The candidate remains private; production/version/defaults are unchanged.
The earlier frozen A22 gain remains valid for that workload.

Day entries are about 35.9..36.5 Hz and night 33.6..34.0 Hz in this diagnostic
route; neither arm sustains 60 Hz. Each On trip has three sparse oracle frames,
20..35 successful joins and 25,152..38,544 compared bytes across those frames,
not per-frame hot-path counters. Physical rows total 5,181; emulator rows 7,567.
The console client is stopped only after completion to settle its log before
archival; the PS2 and computer remain powered on. Subsequent deployment must
not treat that stopped client as a live owner or skip the editor reset.

Evidence is preserved in the 655-file `drive-v1-20261010` archive, including exact private tools,
sources/binding, both ELFs, CSVs, 144 readbacks, logs, asset hashes and the
initial missing-helper build failure. Use `tyrax2-list-join-drive-analyze.py`
with `--archive --output` outside the archive for read-only reanalysis.
Both archived-platform reanalyses pass with the same decision and immutable
inventory; the rejected promotion is part of the preserved result.

## Next source lead: clip output reserve

`StaPipCore::clipPackageSize()` applies a fixed sixfold input reduction to
every VU-clipped package. In `renderStrippedPkgs`, refined `Cull` ranges can
merge, but adjacent true `Clip` ranges cannot. A textured many-colour class
with a nominal 75-vertex capacity consequently submits four triangles per
clip job, even if the conservative parent mask has only one active plane.
This repeats EE buffer fill/header/emission work; it does not necessarily
create a DMA launch per job. It is a different mechanism from C20's nominal
road repacking and the closed subrange-cache trials.

The current generated clip TC image reads the packed six-plane mask and
skips inactive planes. Each active plane can add at most one vertex to the
convex polygon of an input triangle: p planes bound the output to p+1
triangles. For TC many-colour, the half-buffer footprint is bounded by
`2 + 3*N + 9 + 3*(p+1)*N` quadwords, where N is the input list vertex count.
The usable half is 460 quadwords. With a deliberately retained 91-quadword
reserve, p=1 permits 39 input vertices, versus today's 12; p=6 still permits
only 12. This is a capacity proof for the inspected class, not a speed result.

Any intervention must retain the existing outer strip packages and subrange
classification, merge only consecutive Clip ranges, preserve winding/order
and the conservative parent mask, and leave the existing Cull/Outside route
unchanged. `fillCachedStrip` copies the current package descriptor before
replacing its streams; its runtime mask is not the all-plane mask used only
when prewarming static geometry. A zero mask is all six planes in the program
writer and must remain all six for capacity calculation. Billboard, legacy
EE clipping and unknown program overrides require the old cap. Audit every
supported upload/output class and its tag block before implementation; do
not simply change the global divisor or assume the TC proof covers overrides.

This was the A23 source-only lead. The subsequent
[A24 test](../tyrax2-clip-capacity-2026-10-11/README.md) implements refined and
ordinary strip grouping, verifies output bounds and exact rasters, and prices
day/night driving. Its mixed -0.018/+0.148 ms result does not justify a default
or the larger fix. Destructive complete NoClip saves about 3 ms but changes
geometry/appearance and still fails sustained 60 Hz. Do not call this lead
untested or reopen another grouping variant without a new mechanism.

