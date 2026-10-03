# Physical PS2 hardware timeline

The hardware profiler records bounded EE scopes and pipeline-state observations
in RAM, then exports an interactive timeline and Perfetto-compatible JSON.
It measures the ordinary asynchronous renderer without adding per-draw drains.

## Hierarchy and observer controls (1.170)

Coarse capture records the main loop, pad/game/info, refreshed generated
Update/Scene scopes, frame submission, existing completion waits and presentation
pacing. Detailed capture adds package, snapshot/preflight/fixup/mutable-copy,
native sizing/emission and other fine scopes. Register snapshots are a separate
option. Refresh generated game sources when upgrading: Update now reserves its
parent scope before nested work and closes explicitly before rendering.

The editor retains its five-column `hardware-trace.csv`. The separate
`hardware-trace-v2.csv` and `hardware-trace-v2-<first>.csv` provide explicit scope
parents, typed intervals and actual recording/presentation source jobs:

```sh
python tools/hardware-trace.py arm PROJECT --start 1100 --frames 4 --no-states
python tools/hardware-trace.py arm PROJECT --start 1100 --frames 2 --detailed --no-states
python tools/hardware-trace.py export PROJECT/bin/hardware-trace-v2.csv -o REPORT
```

`--capacity` selects 128..32768 slots (default8192). Event density depends on
scene and detail mode. The first Motor District emulator calibration overflowed
8192 slots during a32-frame detailed window; its result is rejected. The revised
private fixture captures8 frames with32768 retained slots. Capacity and timing
still require physical acceptance; shorten the window on overflow. Export
occurs after capture, but its host I/O can delay the enclosing/following loop.

V2 analysis partitions elapsed frame intervals once into authoritative scope
self-time, existing waits, pacing and uncovered time. Inclusive totals remain
separate. Raw compatibility spans have unknown parents and remain inclusive-only;
they cannot silently enter the exclusive ledger. COP0 spans include preemption
and any unclassified waiting: self-time is not useful CPU instructions or VU/GS
utilization. `present_job` identifies the CPU presentation source, not an ISR
display latch or TV photons. Pending jobs from before a capture are explicitly
unknown. Requested mode differs from effective recording after fallback.

Define `TYRA_HARDWARE_TRACE=0` consistently in engine and game to compile out
trace APIs: inline no-ops and constant false gates remove trace clocks, file
access, allocation, state reads and implementation calls. The default is1 to
retain the existing opt-in boot feature; a missing config still leaves compiled
guards. Runtime-off and compiled-out controls therefore answer different
questions. Never label the default build zero-overhead.

The private calibration compares Off/coarse/Off/detail/Off/detail+states/Off
with the same ELF and a ring reserved before all phases. Common independent
clock samples measure the full engine loop plus game/render work. Of8 samples,
the last includes automatic export and is excluded in every arm, leaving7.
Small single-boot windows are exploratory; repeat boots and compare both adjacent
controls. The independent sampler/FrameProfile also cost work. A separate
compiled-out ELF changes code layout and held heap, so its difference is not
pure dispatch cost. Wall time includes pacing; unchanged wall time does not
establish free instrumentation. Do not subtract one universal overhead constant.

`hardware-trace.py controls MANIFEST.json -o REPORT.json` checks repeated explicit
A/B/A controls with matching scene/camera/replay/clock/order/window identities,
one instrumented ELF and declared separate compiled-out layout. See
[host analysis protocol](../tools/hardware-trace-analysis.md) and the
[validation record](hardware-timeline-v2-2026-10-02.json). Physical observer cost
requires repeated valid matched controls on PS2.

The first instrumented physical calibration completes 15,400 frames and all
seven arms. Coarse/detail/detail-with-states captures pass strict export with
104/19,685/23,064 events, each 8 frames, no drops, invalid entries or ambiguities.
Independent FrameProfile work means are 17.697577/17.913900/17.702240/20.898795/
17.775104/21.412268/17.798146 ms for Off/Coarse/Off/Detailed/Off/States/Off.
Against both adjacent controls, observed work increases are 0.211660–0.216324 ms
coarse, 3.123690–3.196555 ms detailed and 3.614122–3.637163 ms detailed with states.
The whole-engine mean remains around 33.36 ms: added work mostly consumes pacing
headroom, and detail activation changes individual frame pacing. Unchanged FPS
therefore does not establish zero observer cost. Seven accepted samples per arm
in one boot remain exploratory; repeated matched runs are open.
States-minus-detail is not an isolated register-read
cost, and none of these ranges is a constant correction for production timing.

The separate compiled-out physical ELF also completes 15,400 frames, with
56 valid samples and 49 accepted. No trace file or recording is generated.
Work means at matching phase offsets 0/2/4/6 are
16.796231/16.815342/16.848844/16.831848 ms. The preceding instrumented ELF's
runtime-off means are 0.886898–0.966298 ms higher; whole-engine wall remains
around 33.36 ms in both runs. This compares complete configurations: inactive
hooks, code layout, a retained 32,768-event ring versus no ring, and independent
boot order. It does not isolate the cost of flag checks. Alternating boot
repeats remain required before assigning a stable configuration cost.

An identical instrumented-ELF repeat completes another 15,400 frames and passes
all raw controls and three strict captures (104/19,688/23,064 events, no drops or
invalid entries). In Instrumented/Compiled-out/Identical-instrumented boot order,
matching runtime-off work is 0.901345/0.886898/0.926261/0.966298 ms above the middle
boot in the first run and 0.928505/0.915934/0.922133/0.930526 ms above it in the
repeat. The two instrumented control means differ by only 0.004128–0.035772 ms.
This supports a repeatable combined configuration difference, not an isolated
inactive-hook bill. The middle compiled-out boot is still single; the retained
ring is allocated before scene assets and can change their memory placement.
Next isolate ring reservation using the same ELF and separate boot configuration,
with capture disabled in every arm and matching independent sample windows.

The [private ring-only control](hardware-timeline-ring-control-2026-10-02.json)
is built and passes both explicit boot settings in the emulator: one identical
instrumented ELF, reservation 0 or 1 before assets, three 1,800-frame blocks and
128 independent samples per block. Trace remains inactive and never exports.
Configuration parsing, geometry-address inspection and state digests occur
outside sampled windows. Both runs provide all 384 whole-engine/work samples;
declared state digests match within and between modes. Real physics dt remains
live, so this is not full vehicle-state or pixel equivalence. Final screenshots
look normal but differ in HUD speed; emulator timings are structural validation
only. The physical fresh-boot 0/1/0 results are recorded below. This probe
isolates a reservation configuration in one ELF, not compiler-pruned hook cost.

The first physical no-reservation boot also passes: 5,400 frames, all 384
independent samples, six matching state controls and valid accessible-address
provenance, with no trace recording or exports. Phase work means are
17.556390/17.664239/17.682027 ms and whole-engine means remain about 33.366 ms.
This first boot is a baseline only; the completed bracket follows below.
Older binaries cannot substitute for same-ELF reservation controls.

The complete same-ELF physical reservation 0/1/0 bracket now passes all three
5,400-frame boots and 384 samples per boot. Reserved-ring work means are
17.486194/17.588678/17.580696 ms; return no-ring means are
17.555806/17.658048/17.662766 ms. The reserved boot is 0.069370–0.101331 ms lower
than both matching controls, whose spreads are 0.000584–0.019261 ms. Declared
state fields and accessible vector/selected-part sizes and capacities match;
all 45 observed addresses move with the ring and restore in the return boot.
This is one bracket under this executable layout, with live physics and partial
state/address coverage. It does not establish a general ring cost or isolate
inactive hook dispatch, and its effect cannot be subtracted from an older
different ELF's configuration gap. The reserved boot's original cfg bytes were
overwritten after completion before archival: launch/runtime records establish
mode1/reserved1, but no original cfg byte hash is claimed. The return cfg was
archived explicitly before launch. The selected inactive dispatch controls
below retain the ring while testing routing separately.

### Inactive dispatch: state and phase controls

A private same-ELF selector bypasses selected inactive diagnostic callers and
metadata paths while retaining the ring, trace lifecycle counters, sampler,
profiling and real renderer operations. Its result is net routing performance,
including existing waits, rather than a pure function-call bill or a
compiler-pruned equivalent.

The [live-dt controls](hardware-timeline-dispatch-control-2026-10-02.json)
retain both rejected normal/bypass/normal boots: the first phase has a different
camera digest. Only the reverse bypass/normal/bypass boot passes all 384 samples;
its local bypass-minus-normal work difference is -0.185 to -0.174 ms. A separate
raw-camera ELF reconciles all 13 camera words with their hashes. Its physical
and emulator controls differ only in the camera position Y by one float ULP;
this maps the two observed hashes in that diagnostic, without recovering exact
values from the older hash-only ELF or repairing its rejected runs.

The [fixed-game-dt controls](hardware-timeline-dispatch-fixed-control-2026-10-02.json)
use the same nominal 1/30 s game step in every arm while measurement clocks and
pacing remain real. Both orders pass all 384 samples, exact state/camera/dt and
provenance controls, but the work delta reverses sign: +0.698 to +0.762 ms in
normal/bypass/normal, and -1.013 to -0.946 ms in bypass/normal/bypass. The middle
phase is heavier in both boots. No stable dispatch cost or gain is accepted.

The [continuous-scene controls](hardware-timeline-dispatch-continuous-control-2026-10-02.json)
retain scene/resources across phases and reuse one 128-sample buffer, exported
after its final whole-loop timestamp and before reuse. Both orders complete
5400 loops and pass all 384 samples, exact state/camera/dt checks, constant
sample and accessible resource addresses, and frozen provenance. Bypass lowers
measured work by 0.130383–0.134239 ms; outer-control spreads are 0.003855 ms
and 0.001551 ms. Whole-engine time remains approximately 33.36 ms with pacing.
The earlier middle-phase increase is absent in these two boots.

This is a local net routing effect from one normal/bypass/normal and one reverse
boot under a fixed game workload. Common sampling, lexical scopes and lifecycle
counters remain. It is not a total inactive-observer bill, a compiler-pruned
comparison or a universal correction for older ELFs. Removing reloads and
changing sample-buffer layout together does not identify which caused the
earlier phase effect. Broader repeats remain open; fresh coarse Scene
attribution is recorded below.

The immutable calibration fixture still has the older generated `Scene` scope,
which is detail-only. Its coarse renderer work therefore remains under `Game`;
this is a coverage limit of that ELF, not evidence that the remainder is scalar
math. The refreshed generator and examples explicitly enable coarse `Scene`.
Changing the calibration fixture requires new binary identities and controls.
In the instrumented detailed trace, the largest exclusive residuals are static
bag preparation, scene assembly, dispatch and bounds. Snapshot mutable copying
and preflight also have repeated activation. This ranking locates candidate
paths; detailed observer cost prevents treating it as a production cost bill.

### Fresh coarse Scene envelope

A new private fixture transplants the exact current generator's existing
`Scene` scope (`Kind::Span, false`) into the preserved game. This activates one
scope per main-view frame in coarse mode; it does not regenerate the complete
project or add per-object clocks. All remaining game/engine rendering code
stays unchanged. The same ELF uses Off/Coarse/Off and Coarse/Off/Coarse, continuous
scene/resources, a common sample buffer, fixed game dt, and normal inactive
routing in every arm. Detail and register snapshots remain off.

The unchanged capture API caps a window at 32 frames. Each arm records 32 raw
samples; index31 is excluded uniformly from every metric in every arm because
the last captured whole-engine loop includes automatic file export. The other
31 samples are aligned to actual engine frames and recording jobs. The ring
stays reserved before assets in all arms, and configuration bytes are frozen
before each launch. Camera/state/dt, accessible addresses and export-before-reuse
controls are required alongside strict complete CSV hierarchy acceptance.

[Two physical boot orders](hardware-timeline-scene-coarse-2026-10-02.json)
complete 5400 loops each, with 192 raw/186 uniformly selected independent
samples in total. All three active captures pass 32 frames/448 events each,
exactly one authoritative Scene per frame, zero drops/invalid hierarchy or
ambiguities, and matching actual source jobs. The first 31 frames of each
capture provide 93 attributed frames; full final capture frames remain valid
but are excluded from every quantitative comparison. Exact named state,
13 camera words, game dt, accessible vector/part addresses and the common
sample address match within and across both boots. ELF/config/source/asset
and matching-symbol provenance is frozen independently. Native compilation,
complete emulator replay and independent source/parser controls also pass.

| Coarse observation | Mean range across three accepted windows |
|---|---:|
| Scene inclusive/exclusive elapsed EE span | 17.680436–17.704852 ms |
| Update exclusive span | 1.439001–1.451992 ms |
| Remaining Game exclusive span | 0.385932–0.399354 ms |
| Named VIF/GS completion waits, outside Scene | 0.003058–0.003096 ms |
| Presentation pacing | 13.646139–13.681891 ms |

Coarse activation increases independent renderer work by 0.074674–0.096925 ms
in Off/Coarse/Off and 0.068905–0.089334 ms in reverse order. Outer-control
spreads are 0.022251/0.020429 ms. Whole-engine periods stay approximately
33.36 ms: pacing absorbs the observed increase. This is one boot per order
under the fixed parked workload, not a universal correction or a pure Scope
call cost. The new ELF's inactive timings are not interchangeable with older
calibration ELFs.

The dominant observed envelope is now Scene, rather than unexplained Game.
Scene has no typed children in this coarse capture, so its inclusive and
exclusive ledger durations coincide; that does not make it pure arithmetic.
Synchronous queue snapshotting/conversion reached during scene submission is
included. The envelope excludes only later work after `renderScene` returns;
EndFrame is not a blanket owner of all snapshot/native preparation.
The pooled CPU-labelled frame ledger is 19.666059 ms, known waits 0.003082 ms,
pacing 13.660563 ms and uncovered time 0.035484 ms, with zero ambiguity. These
categories partition elapsed frame time; the CPU label still includes any
unclassified waits or interruptions. Do not subtract the EndFrame completion
waits from Scene: they are outside it. The aggregate census below subsequently
subdivides bag preparation and dispatch; pricing any actual candidate with
capture disabled remains open. No production optimization or VU/GS utilization
claim follows.

### Aggregate bag preparation and dispatch census

A private follow-up splits each `StaPipCore::render` call into eleven contiguous,
exclusive intervals. It preserves rendering order and measures Head, Bounds,
packager/MVP, texture/wrap, program selection, light sphere/pick, BLSS/fog,
object data, cache opening/replay, dispatch routing and finalization/tail.
Early returns retain exact coverage. Route counters distinguish empty, culled,
direct and partial bags, with replay/retention as separate flags. Partial
stripped/list/subpackage work remains combined inside DispatchRoute.

The same ELF compares Off/On/Off and On/Off/On with continuous resources,
fixed game dt and common 128-row sample and metrics buffers. A common Scene
envelope exists in every arm; the detailed trace remains inactive. Active bag
observation reads Count twelve times per ordinary call, twice for an empty
call and three times for a culled call. Aggregation occurs after the final bag
timestamp, inside Scene, so independent renderer-work controls include its cost.
Runtime Off retains common observers, branches and layout; it is not equivalent
to removing the instrumentation at compile time.

Two UDP export attempts were rejected for missing records. Splitting the burst
across frames still lost data. The accepted export design writes RAW/BAG/WINDOW
records to a separate `host:` file during offsets 1200–1327, after the sampled
offsets 900–1027 and the final state control at 1100. It closes the file before
DONE. Startup/control stdout and the exact file are validated separately;
missing rows are never reconstructed or deduplicated. The next phase cannot
reuse the buffers until the previous export completes.

[Both physical boot orders](hardware-bag-dispatch-census-2026-10-03.json) pass
5400 loops each and all 768 raw/metrics pairs, with 384 active frames.
Exact state/camera/dt, accessible addresses and common buffer addresses match
within and across boots. Every active sample has 135 calls: zero empty, one
culled, 123 direct and 11 partial; 122 replayed and 11 retained flags. Every bag is
inside the one Scene and its eleven intervals telescope exactly to its total.
Native build, full emulator replay, independent source/algebra and negative
acceptance controls pass. Earlier UDP attempts remain rejected evidence.

| Active observation | Mean range across three 128-frame windows |
|---|---:|
| Head | 0.308874–0.313884 ms |
| Bounds | 2.044088–2.050814 ms |
| Packager/MVP | 0.830883–0.832784 ms |
| Texture/wrap | 0.582511–0.587716 ms |
| Program selection | 0.116315–0.121862 ms |
| Light sphere/pick | 0.945445–0.957241 ms |
| BLSS/fog | 0.084924–0.090435 ms |
| Object data | 2.179928–2.192450 ms |
| Cache opening and actual replay | 3.526090–3.534135 ms |
| Dispatch routing and package work | 2.847574–2.858901 ms |
| Finalization/tail | 1.937671–1.942483 ms |
| Sum of selected bag intervals | 15.431570–15.449663 ms |
| Common Scene envelope | 18.514274–18.518139 ms |
| Scene minus selected bag intervals | 3.067463–3.082704 ms |

Off Scene means are 17.731714–17.734488 ms. Independent renderer work increases
by 0.781749–0.784440 ms in Off/On/Off and 0.778792–0.780136 ms in On/Off/On;
outer-control spreads are 0.002691/0.001344 ms. Pacing leaves the whole-engine
period approximately 33.365 ms. This measures net activation under one boot per
order, retaining common instrumentation; do not distribute that cost uniformly
among buckets or subtract it from each bucket. Elapsed intervals include waits,
interruptions and synchronous snapshot/native preparation. The Scene residual
includes work outside selected bags and aggregation after their terminal stamps.
It is not all bookkeeping, nor a pure CPU bill.

The next concrete hypothesis is the largest group: distinguish baked lookup
from actual replay/submission, including any snapshot and native conversion
performed synchronously inside it. DispatchRoute's partial package paths are
also still combined. A measured candidate must preserve output and be compared
with bag observation disabled. No production speedup or VU/GS utilization claim
is established by this census.

### Replay lookup and conditional flush census

The next private fixture replaces CacheOpenReplay with three contiguous
siblings: DispatchBakedOpen, DispatchWholeReplay and DispatchRetainedOpen.
All other bag intervals retain their boundaries, giving thirteen siblings and
fourteen Count reads per ordinary bag. Existing short-circuit calls, cache
decisions, packet construction and DMA ordering remain unchanged. Attempt and
acceptance counters use existing booleans: a non-null baked entry is accepted,
not necessarily complete or ready to replay.

ReplayFlushInclusive wraps only the existing conditional `flushPendingPacket`
call inside `replayWholeBakedBag`, adding two Count reads per actual flush.
Singleton Scene/bag/replay depth and the current WholeReplay bucket establish
ownership; orphan, nested or wrong-owner captures are rejected. This is a
nested subset of WholeReplay, never an additional sibling. The flush submits
the shared pending packet, potentially containing several bags; its elapsed
cost belongs to the triggering call, not only that bag's geometry. Child
aggregation occurs after its terminal timestamp inside WholeReplay, so the
parent-minus-child difference includes observer guards/commit/glue as well as
replay work outside the flush. It is not a pure REF-append bill.

The same ELF retains Off/On/Off and reverse, continuous resources, normal
inactive trace routing, common Scene and 128-row buffers, fixed game dt and
separate authoritative file export after sampling. Metrics now occupy 33792
bytes. The strict checks include all thirteen sibling intervals, nested child
containment, call-attempt identities and unchanged state/camera/address controls.

These parked fixtures advance simulation by a fixed 1/30 second per frame under
NTSC. PCSX2 may execute more game frames per real second than this PS2 night
workload, so simulation can visibly run faster even at the emulator's normal
1x setting. This is diagnostic fixed-workload behavior, not ordinary generated
game timing or an emulator-derived hardware performance result.

[Both physical orders](hardware-replay-census-2026-10-03.json) complete 5400
loops each with all 768 raw/metric pairs; the three active windows provide 384
observations. Exact selected state/raw-camera/address/common-buffer controls
match within/across boots, and every active frame has 135 calls: one culled,
123 direct and 11 partial. Baked opening is attempted 123 times, accepts 122
entries and leads to 122 successful replay calls. Retained opening is attempted
12 times and accepts 11. The child observes 86 real conditional flush calls per
frame, with zero ownership errors. It does not count every flush in the frame
or imply fixed four-bag batches.

| Active elapsed observation | Mean range across three 128-frame windows |
|---|---:|
| Baked opening, including dispatch prefix | 1.053096–1.075434 ms |
| WholeReplay, including conditional flush | 2.746441–2.758218 ms |
| ReplayFlushInclusive, nested inside WholeReplay | 2.248903–2.252703 ms |
| WholeReplay minus its child | 0.496579–0.505515 ms |
| Retained opening and closing glue | 0.124836–0.130651 ms |
| DispatchRoute | 2.847265–2.850926 ms |
| Bounds | 2.259668–2.282224 ms |
| Object data | 2.214429–2.239308 ms |
| Finalization/tail | 2.000384–2.016023 ms |
| Thirteen selected bag intervals | 16.269245–16.288591 ms |
| Common Scene envelope | 19.366182–19.368673 ms |
| Scene minus selected bag intervals | 3.080082–3.098673 ms |

The child accounts for 81.8005% of pooled observed WholeReplay duration. This
locates shared-packet flush/submission as the dominant operation inside this
parent, while leaving its existing waits/snapshot/native work combined. Off
Scene means are 17.955527–17.960979 ms. Independent renderer work increases by
1.400969–1.402325 ms in Off/On/Off and 1.406834–1.408165 ms in reverse; outer
work spreads are 0.001356/0.001331 ms. The selected observer impact is substantial.
It includes enabled guards, sibling/child clocks, ownership writes and aggregate
commits, while common instrumentation remains in Off. Do not subtract this
cost uniformly, compare bucket deltas with the earlier ELF, or interpret the
measured flush span as an achievable saving or a pure CPU bill.

Native compilation/publication, full emulator replay and independent actual
header/source/parser checks pass. Initial WSL startup failed with
HCS_E_CONNECTION_TIMEOUT; stopping Docker Desktop restored Ubuntu access.
Ubuntu also responded after Docker Desktop was started again following capture.
The prepared TyraX Docker toolchain also compiled the sources; selected boots
use one exact recovered-native ELF. A Docker-created output directory blocked
native publication, so it was preserved separately before a fresh output retry.
All 328 baseline runtime assets are hash-verified after restoring missing assets.
These are recorded build/environment events, not an established Docker/WSL root
cause or a production renderer fix.

The next targeted experiment should drop the broad bag observer while splitting
the existing send path into pre-submit work, `Vif1Queue::submit` and post-submit
reuse. Snapshot and native conversion are synchronous children of submit.
Retain ordinary batching and actual waits; price the narrower observer in both
orders, then compare a concrete candidate with observation disabled. No shipped
performance improvement or VU/GS utilization claim follows from this result.

### Selected send and synchronous submit census

The next private fixture removes broad bag timestamps and observes only the
existing replay-triggered flush branch. Each selected flush uses 14 Count reads:
two for its envelope, four contiguous send stamps (PreSubmit, SubmitInclusive,
PostSubmitReuse), and two each for SnapshotInclusive, NativeSizing,
NativeCapacity and NativeEmission inside submit. Common Scene retains two reads
in every arm. Lexical singleton ownership rejects nested/orphan calls. No tag or
vertex loop reads clocks; batching, waits, fences and packet operations remain
unchanged. An independent diagnostic undo matches executable source against the
original control.

The same native ELF runs Off/On/Off and On/Off/On, retaining the ring, buffers,
fixed game dt and continuous scene. Reliable file export follows each sample
window. Snapshot includes preparation, copy and any existing drain/retry;
capacity includes pressure handling and existing waits. Post includes buffer
reuse and bookkeeping. SubmitRest and FlushResidual retain unlabelled work and
observer commits, so children are never added to their inclusive parents.

The [physical record](hardware-send-census-2026-10-03.json) accepts both complete
5400-loop orders and all 768 raw/metric pairs. Exact selected state, raw camera,
dt, accessible addresses and common buffers match across boots. Every active
PS2 frame has 86 flush/send/submit calls and successful snapshots/native records,
with no retry, fallback, non-null devkit hook or ownership error. These selected
packets contain 1,875 source qwords and 718 sized DMA records; snapshots copy
30,000 bytes and borrow 2,118,944 immutable bytes per frame.

| Selected observed interval, three active windows | Mean range |
| --- | --- |
| Replay-triggered flush, inclusive | 3.028882–3.046008 ms |
| PreSubmit | 0.237726–0.250161 ms |
| SubmitInclusive | 2.319855–2.326098 ms |
| PostSubmitReuse | 0.185619–0.189910 ms |
| SnapshotInclusive, child of submit | 1.165831–1.170302 ms |
| NativeSizing, child of submit | 0.167702–0.170118 ms |
| NativeCapacity, child of submit | 0.082599–0.083607 ms |
| NativeEmission, child of submit | 0.439863–0.451540 ms |
| Submit minus its four children | 0.454070–0.459110 ms |
| Flush minus its three send siblings | 0.280499–0.284130 ms |

Net observer activation adds 0.804536–0.804571 ms renderer work in Off/On/Off
and 0.784130–0.785166 ms in reverse. Outer work spreads are 0.000034/0.001036 ms.
This is still substantial disturbance; common apparatus remains in Off. Do not
subtract the price from each child, compare it arithmetically with an earlier
ELF, or label the larger snapshot span a pure copying bill. It already borrows
about 2.02 MiB of immutable data while copying about 29.30 KiB. The concrete next
candidate reuses a successful snapshot's DMA-record count to avoid native sizing's
second tag scan; its extra counting/return cost must be measured with observation
disabled before claiming any gain. This does not resolve the larger snapshot cost.

Native compilation, full emulator execution, source-operation undo, actual-header
ownership/read-budget controls and strict negative controls pass. The emulator
consistently has 85 selected flushes, also present in the previous replay artifact;
PS2 has 86. The initial hardcoded-86 emulator rejection is preserved, then an
explicit environment contract accepts all 384 emulator samples. Independent
checks also exposed and closed a missing record-count bound in the parser.
Neither tool correction changes the frozen source or ELF. Cross-device workload
equality and emulator timing claims remain false.

### What attribution can decide

A reliable total timer and a complete ownership ledger answer different
questions. A wide parent scope can own nearly all frame work while still
containing many unnamed operations. Detailed trace helps locate those paths,
but its several-millisecond disturbance can change when asynchronous work
finishes and where the EE waits. Its exclusive wall times are not a production
arithmetic bill.

Coarse Scene supplies a lower-volume envelope for `renderScene`: Update is
already closed. Snapshot copying and native sizing/emission called synchronously
by scene submission are inside this envelope; only work performed after
`renderScene` returns is outside it. Final chain closure, pending presentation
and DMA submission normally occur in EndFrame. Its span still includes
unlabelled waits and interruptions.
Only the named completion waits are separately classified; tiny measured waits
do not prove all consumers or the EE are idle elsewhere. Existing narrow
snapshot and math censuses rank hypotheses, but different ELF/boot observations
cannot be summed into one Scene total.

After establishing the envelope and its activation cost, measure a small set
of aggregate operations inside the dominant bag-preparation/dispatch paths.
Keep original batching and compare candidate work with the observer disabled.
A useful next experiment must identify an operation or validate a saving;
repeating calibration alone does neither.

[Seven physical PS2 controls and measured overhead](hardware-profiler-results.md)
record the first full-asset Motor District diagnosis.

## In the editor (1.92+)

![Native hardware timeline](img/hardware-timeline.png)

Open **Debugger > Hardware timeline** (F9). Set the first engine frame, capture
length (1–32) and optional register snapshots, then **Arm next boot**. This writes
`bin/hardware-trace.cfg`; it does not reset or interrupt the running console.
Build the game before arming. After the capture finishes, **Load hardware capture**
reads `bin/hardware-trace.csv` on demand. The previous file stays available until
the game replaces it; arming is not evidence of a new capture.

Select a frame and zoom horizontally (1–16x). Hover blue EE scopes, red waits or
orange register markers for duration, relative start and raw values. Scroll the
chart in either direction; expand or undock the Debugger for a wider view. The
inclusive table contains overlapping scopes, not additive hardware utilization.
The native viewer requires neither Python nor a browser, adds no polling and
rejects incomplete, overflowing or inconsistent captures. HTML/Perfetto export
still uses the same CSV and remains available with the command below.

Detailed engine lanes include `Package_create` (metadata and classification),
`Package_classify` (nested inside creation), `QBuffer_copy` (including pooled
allocation bookkeeping), and `Packet_build` (commands, excluding send/wait).
Generated debug games also expose `Live_debug`, with nested
`Live_debug_poll` and `Live_debug_flush` scopes when those synchronous host-file
operations actually run. These are the first place to look when an otherwise
fast physical-console frame periodically doubles.
Vehicle projects split `Vehicles_update`, `Vehicle_smoke_update` and
`Vehicle_skids_update`; `Vehicle_sleep` is nested once for every parked instance
that skipped its static physics work in that frame.
These extra hooks have measurable capture overhead; do not rank optimizations
using their short armed captures alone.

## Capture

Build a game with this engine and refreshed generated sources. Deploy all baked
resources, keep one ps2client host server alive, and use the usual ps2link marker.
Before the next boot:

```sh
python tools/hardware-trace.py arm /path/to/project --start 120 --frames 4
```

The engine reads `bin/hardware-trace.cfg` once after game initialization. Start
is a zero-based engine-loop index, including loading/splash loops, not a scene
frame number. Choose a warmed window after loading. Capture length is 1–32
frames. The default 8,192 event slots occupy approximately 480 KiB on EE;
capacity is configurable from 128 to 32,768. Boot capture allocates before
sampling and frees after export. A missing or invalid configuration disables capture. There
is no continuous host-file polling. The game keeps playing after capture.

The completed file is `bin/hardware-trace.csv`. Archive old files before a new
boot and verify fresh output; an earlier CSV is not evidence of a new capture.
Export only after the final END footer arrives:

```sh
python tools/hardware-trace.py export /path/to/project/bin/hardware-trace.csv -o /path/to/report
python tools/hardware-trace.py disarm /path/to/project
```

Export produces `report.html`, `report.json` (Perfetto/Chrome trace format), and
`report.summary.json`. Open HTML locally to select frames and hover over events.
The exporter rejects incomplete, overflowed and inconsistent captures. On
overflow reduce the requested frames. Disarm affects the next boot.

## Read the graph correctly

- Frame encloses the main EE loop, including pad, game and info. Game/Scene,
  static bags, dispatch and wait scopes nest: never add their inclusive totals.
- Scene phases and object IDs reuse the render-cost labels, but do not insert
  that tool's GS barriers. Do not request a serialized render-cost capture,
  screenshot or VU memory capture during hardware sampling.
- `Roads` is the per-frame cull/submission of road and junction bags that were
  generated once at scene load. `Procedural` excludes those reserved road
  chunks and covers only procedural volumes and prefab geometry. Generation is
  therefore not recurring, but submitting visible ready bags still is.
- VIF1_DMA_wait measures EE waiting for DMA consumption, not VU1 arithmetic.
  VIF1 snapshots show VPS and VEW at observed boundaries only. GIF_STATE shows
  active path and FIFO occupancy at those instants; it is not GS utilization.
- GS_FINISH_wait measures existing completion handshakes and can include queued
  upstream work. No new FINISH is inserted. Present includes buffer-flip waits.
- Packet_qwords is a submitted static-chain size, not total vertex/texture bytes
  referenced through DMA tags. No unmeasured transfer-byte total is inferred.
- COP0 Count timestamps use 294,912 ticks/ms. Scopes measure elapsed EE time,
  including preemption; they are not retired CPU instruction counts.

Record an unarmed control, an armed capture and a repeat. `--no-states` disables
register snapshots while retaining scopes for overhead diagnosis. Active scope
recording and register reads perturb timing; export itself occurs after the
window and must be excluded from FPS comparisons. Short traces locate work,
while longer warmed controls establish performance.

## Engine API

`debug/hardware_trace.hpp` exposes `Scope`, `record` and `state`. Labels must have
static lifetime; storage retains their pointers until export. The recorder is
for the main EE thread only. Scope destructors handle early returns. When
unarmed, hooks check the active flag without reading clocks or hardware state.
The engine owns configure/beginFrame/endFrame, so early game-loop returns still
close a complete frame. The editor uses boot capture configuration; calibration
code can reserve retained storage and arm future windows between phases.
This is not a hardware utilization percentage display.

Pair traces with isolated raster, shader, extra-pass and debug-I/O controls.
Changes to visible content are diagnostic substitutions, not proposed quality
settings. Verify actual GS captures and geometry counters before interpreting
their sensitivity as a bottleneck diagnosis.

For two reproducible engine probes, use an already-built fixed-workload project:

```sh
python tools/hardware-probe.py FIXTURE vendor/tyra NEW_DIRECTORY --kind one-pixel-scissor
python tools/hardware-probe.py FIXTURE vendor/tyra ANOTHER_DIRECTORY --kind unlit
```

Each command copies the game and engine, records resource hashes and patched
files in `probe.json`, and refuses an existing destination. Build its `game`
with native-build and its `tyra` as the engine source. The one-pixel probe masks
engine scissor writes, including render-target restores, while retaining the
original geometry, projection and VU programs. It suppresses raster area; it
does not remove primitive setup or all GS work. Verify its nearly empty GS
capture and matching geometry counts. Unlit removes lighting and normal data
from static bags, restoring the caller's pointer afterward; this changes
program class, data transfer and possibly package capacity, so it cannot
isolate VU arithmetic alone. Both are destructive to image quality by design
and must remain isolated diagnostics, never production engine patches.
