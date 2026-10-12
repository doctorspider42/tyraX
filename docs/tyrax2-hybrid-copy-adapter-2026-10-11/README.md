# Hybrid2 Copy DMA and FINISH Adapter Component

This A41 investigation implements main/ISR arbitration and actual EE register/cache
operations, then qualifies private Core/queue integration. Component evidence
alone does not establish full-game interrupt safety or a runtime speedup.

## Accepted Component V5

The SAME templated adapter code passes42 readiness/claim/fallback guards in
optimized and ASan+UBSan host builds. Real EE backend is explicitly instantiated
and cross-compiled with the SDK headers and `_EE`; a static assertion verifies
60-byte EE adapter ABI. Actual backend packet validator is extracted unchanged
and compiled against an explicitly mapped, non-replacing host test allocation:
644 tail/bounds cases pass in BOTH host modes. These are component controls,
NOT target drawing/source/readback or physical timing evidence.

The implementation publishes a cache-line-aligned immutable normal GIF packet
on the main thread, validates2..128qw/RAM bounds and the SDK's exact final two
FINISH quadwords. ISR polls the bound old-job queue proof, VIF STR, GIF STR and
GS FINISH. On a permitted claim, acknowledge ONLY CSR FINISH=2 before programming
GIF MADR/QWC, sync, then CHCR=0x101. Do not OR all CSR events. The copy's completion
requires BOTH GIF DMA idle and a NEW owned FINISH; old FINISH cannot satisfy it.
Main critical-section restoration preserves the previous interrupt-enabled state.
VBlank-end can consume completion before main; main then recognizes Done without
waiting for another already-consumed event. Early source release/duplicate copy
and ISR publication are rejected. No ISR wait/allocation/cache publication.

Compiled ISR entry sections contain no direct SyncDCache/DIntr/EIntr/heap
relocations. This is a DIRECT-reference inspection, not a full indirect call
closure or runtime WCET proof. The bound queue callback must also be inspected.

[V5 source/compiler/object/test inventory](v5/report.json) includes full source
snapshots and outputs. V1 passed adapter controls but lacked extracted packet
tests; V2 fails missing initializer_list in its host driver; V3 fixes it; V4 adds
the60-byte ABI assertion. V4 evidence is retained, not target-qualified. V5 adds
the necessary copy-window precondition following source review. No deployment or
price for these attempts. A first raw EE compile failed missing `_EE`, not renderer
code; normal SDK flag fixes it. Common protocol-mode runner regression also passes
144/161280 controls after the runner's adapter-mode extension (private V2 output).

## Important Window Gap Found Before Integration

A VBlank-start handler can run late. A software open-blank flag and completion
at the END handler do NOT by themselves prove that copying happened inside the
physical blank: both handlers can be delayed. The adapter now requires an explicit
`copyWindowReady` callback, independent of queue/FINISH readiness, for BOTH ISR
and fallback kicks. EE backend returns false if that proof is unbound. Host
controls cover denial/retry; they do not fabricate a physical proof.

The window proof must cover actual event age, remaining physical blank and a
qualified copy/dispatch budget. Next inspect available hardware timer/gate and
kernel interrupt semantics before binding it; do not infer live blank from old
VSINT, handler entry, or handler-order timestamps alone. Source review caught this
before target integration, avoiding a potentially torn-output candidate.

## Remaining Integration

### Actual Native Queue Ownership Predicate

Vif1Queue now exposes read-only `canCopyCompletedNativeFrame(sequence)`. It
requires nonzero job, native pipeline mode, matching nativeInFlight and exact
last-started identity, empty ring and VIF STR clear. If metadata still says
running, the old completion may be unserviced (the baseline also supports an
EE-advanced queue); do not advance it in ISR. Otherwise completed must match.
Call from ISR or an interrupt-masked main transition. No flush, close, wait,
queue advance, cache operation or allocation. This API has NO runtime caller yet.

Do NOT compare the old job against `submitted`: the next frame may already
reserve that logical ID without actual DMA submission. Its unsubmitted prefix
must remain intact. The new predicate intentionally does not read/alter that
reservation. It establishes queue identity/idleness only; Core must still own
the old framebuffer and exclusive GS FINISH, prohibit foreign/new submission,
and supply the independently qualified copy window and immutable packet.

Actual method body is extracted unchanged from the current queue source into
the test driver.16384 combinations cover zero/wrapped IDs, matching/stale native,
started/completed IDs, running-vs-unserviced metadata, ring wrap, busy DMA and
disabled native pipeline.60 admissible cases match the independent truth table;
all state remains unchanged. Optimized and ASan/UBSan pass. EE compilation tests
the same method with mock volatile metadata, NOT complete engine linking or live
MMIO. Target setup entry keeps pipeline state mutable, avoiding a vacuous
constant-false cross-compiled wrapper. V1 before this correction remains private.

V3 runner also rejects a compiled method lacking state loads or containing stores/
calls; this checks the mocked target method, not full indirect ISR closure.
[queue-v3 source/test/object inventory](queue-v3/report.json). Reproduce with
`python3 tools/verify-hybrid-copy-queue.py OUT --ee-cxx EE_GXX`. No full-game native
build, ISR call, console deployment, FPS improvement or default promotion here.
Next bind the actual Core/GS lifecycle and qualify the complete indirect ISR call
closure, full-frame source/raster and hardware completion witness before A/B.

### Original Copy Consumer Budget

`tools/tyrax2-copy-budget.py` pins A40's actual native-bound GS source and extracts
getRasterTarget/emitRasterRestore/emitHybridPresent bodies unchanged. A private
standalone class supplies fixed448x448 CT32 draw/CT16 display/Z32 layouts, matching
the permanent allocation ordering, and progressive non-redirected raster state.
Dispatch stubs run only during construction; actual timed sends use prepublished
normal GIF DMA and require DMA idle plus a NEW GS FINISH. Packet is79 original
quadwords plus2 SDK FINISH quadwords. Original14x32px columns, dither enable,
texture/target formats and raster restoration remain. This is NOT the game or
its full EE preparation, timer reservation and queue ownership.

V1/V2 emulator attempts are rejected before PS2: test-pattern/readback coverage
is wrong, with one dominant color/black edge in V1 and displaced rows/wrong
destination address units in V2. V3 uses explicit integer test-pattern SPRITEs
and existing tape-style64-byte-aligned row readback with wordAddress/64. No
original copy-body edits. Do not diagnose a production engine bug from these
failed diagnostic fixtures. Their private attempts remain preserved.

Accepted V3 ELF ed9d1ec8db00ea9e72f0fbd690fb98e54a3599033c6dfc3ada3ea4afefbe95fa
completes hidden PCSX2 then PS2:120 copies, no errors, timers restored, zero late
copies. BOTH targets independently verify all200704 source/destination pixels:
four quadrants50176pixels each, <=7channel error from16-bit quantization/dither.
Reader checks all14 column UV/XYZ pairs, CT32/CT16/TEX dimensions and FINISH,
source/ELF pins, complete sequence and six rejecting controls. This packet
validation is scoped to construction/copy, not the full game's state lifecycle.

Actual PS2 completion median0.179562ms, p950.179942ms, maximum0.180206ms;
minimum0.179477ms. All sampled hardware ages remain inside blank. Compared with
the separate physical1.428819..1.430556ms interval observation, the isolated
conversion has substantial room. Emulator reports0.000631ms, illustrating why
it is NOT physical pricing.120-copy maximum is empirical, not universal WCET;
the main-thread kick is not an IRQ dispatch qualification. Appending FINISH also
changes synchronization from stock Hybrid2, and must be priced in the game.

Next bind full-game timer/queue/framebuffer ownership, conservatively qualified
dispatch/copy guard and immutable normal packet; preserve current unsubmitted
prefix/context/bank/HUD and old-vs-copy FINISH ownership. Full raster and actual
in-blank completion qualification BEFORE same-ELF Hybrid2 A/B/reverse. Do not
claim either game60fps or avoidable EE deadline counts from this isolated trial.
Owned physical34720 and hidden256/245 stopped after complete exports; devices ON.
Evidence: [copy-budget-v3/physical-copy.json](copy-budget-v3/physical-copy.json).

Build: `python3 tools/tyrax2-copy-budget.py BOUND_A40 OUT --toolchain PS2DEV_ROOT`.
Check: `python3 tools/tyrax2-copy-budget-check.py OUT --platform physical` or
emulator; use `--output FILE` outside immutable archive during replay.

### Isolated Target Timer Preflight

`tools/tyrax2-window-preflight.cpp/.py` builds a standalone ELF with A40's signal:
noninterlaced480p, FRAME, flicker filter disabled. No IOP reset, pad/audio loading,
scene or copy. Reserve T0/T1 ONLY with MODE both0 and timer IRQ mask/pending clear.
Own bounded START/END handlers and main hardware-age polling collect intervals.
Restore COUNT/COMP/MODE and previous S/E IRQ enables; remove own handlers before
export. T2/T3 are observed, never reprogrammed. Isolated ownership is NOT game
ownership qualification.

ELF6489c9eed2cbb74627ec194f459700c3fcb21568175b2bfd6b225b0cb410653a
completes hidden PCSX2 and PS2:120 START/120 END, zero errors, restored state,
six rejecting reader controls each. Exclude startup records from steady checks.
Initial reader misindexes the hexadecimal mode column; repair changes parser
only, NOT target ELF/logs.

Physical T0/T1 MODE0, I_MASK0x1802/I_STAT0x0d with no timer0/1 ownership bits;
T2_MODE0x382/T3_MODE0x83 corroborate their exclusion. T0 resets at START, T1 END.
Physical active8785..8786ticks, blank823..824ticks =1.428819..1.430556ms;
median field16.683336ms,29 crossed-START reads rejected. Emulator blank841..842
ticks =1.460069..1.461806ms, median16.682677ms.576timer ticks/ms. Do NOT transfer
emulator budget to hardware. Ranges are observations, not universal bounds/WCET;
no conversion packet or scene FPS price exists.

Editor-equivalent reset/execee succeeds despite negative ICMP ping: ping alone
does not establish ps2link failure. Owned41540 stopped after full export; owned
hidden emulator40/Xvfb29 stopped after completion. Devices stay ON. Sources,
build pins/command, logs, reports and deployment metadata are in
[preflight-v1](preflight-v1/physical-window.json).

Next qualify original448x448 conversion+FINISH/dispatch budget on PS2, full-game
timer/queue/source owners and exact output BEFORE Hybrid2-only A/B. Build with
`python3 tools/tyrax2-window-preflight.py OUT --toolchain PS2DEV_ROOT`; check with
`python3 tools/tyrax2-window-preflight-check.py OUT --platform physical` (or
emulator). Archive replay must use `--output FILE` outside the archive.

### Dual-edge window predicate component

`tools/tyrax2-hybrid-copy-window.hpp` now implements the proposed fail-closed
window decision, without configuring or reserving any hardware timer. Samples
use start-age/end-age/start-age and modes before/after. Hardware-qualified
opposite-edge resets allow age order to identify blank independently of START/
END callback delay. A decreasing start age rejects an edge crossing; active
delta, field range, configuration and a nonzero matching qualification epoch
are mandatory. Admission also requires strict remaining-blank room for the
original-copy-plus-dispatch budget. Bounds are NOT learned from IRQ timestamps.

Optimized and ASan/UBSan host tests each exercise280800 synthetic physical-time/
read-span/budget positions, including delayed delivery across eight fields,
variable field/blank lengths and read sequences crossing edges.20000 admissions
all stay inside the independently modeled physical blank through the budget.
137 negative mode/epoch/range/budget/overflow controls pass. The same predicate
cross-compiles for EE;24/32-byte bounds/sample ABI is statically checked. This
is bounded-model soundness, NOT hardware sampling, empirical copy WCET, timer
reservation, actual polarity qualification or frame-time evidence.

Both physical counters must reset each field under verified polarity and stable
video timing; otherwise this reasoning does not apply. Read-span and post-check
dispatch latency must be bounded in qualification. T0/T1 are candidates only;
no hardware adapter binds this predicate and no console was restarted/deployed.
The existing V5 callback remains unbound/fail-closed. Next implement target
reservation/sampling and qualification before enabling actual copy submission.

Reproduce in a fresh directory using
`python3 tools/verify-hybrid-copy-window.py OUT --ee-cxx EE_GXX` under WSL.
The copied source/compiler/test/object inventory is [window-v1/report.json](window-v1/report.json).

### Hardware-window source audit

The Sony EE User's Manual v6 chapter4 describes four16-bit counters, BUSCLK/
16/256 or H-BLNK clocks, V-BLNK gating, and gate-edge counter resets. This supports
measuring event age independently of handler entry; it does not specify a safe
copy duration or establish the polarity/blank width of our active video mode.
[Primary manual, mirrored](https://studylib.net/doc/25813272/emotion-engine-user-s-manual).

The actual A40 symbol ELF SHA256 is
`5b7ed810f3a2c7749ed2d04237c381ea76ab78de085d6aedcb50fd970fb1f52b`.
Its `_ps2sdk_init_timer_impl` at0x2fefb8 calls `InitTimer(2)` then
`StartTimerSystemTime`. `InitTimer` at0x2ff638 reads T2_MODE0x10001010,
installs INTC11 and invokes SetT2_COUNT/COMP/MODE. T2 is NOT available to steal.
Linked `InitAlarm` at0x1083c8 reads T3_MODE0x10001810 before kernel setup;
Tyra `Timer::prime/getTimeDelta` read T3_COUNT. T3 is also excluded.
Current upstream [SDK timer implementation](https://raw.githubusercontent.com/ps2dev/ps2sdk/master/ee/kernel/src/timer.c)
agrees about T2, but linked instructions, not upstream master, establish this
fixture's behavior. The SDK allocation API manages software counters over T2;
calling AllocTimerCounter does not reserve a separate physical T0/T1.

The local PCSX2 Counters.cpp SHA256 is
`0aada58be35681113a282b80a36e3cf87291e816fb5c9410bef559e088d412b9`.
Its rcntStartGate resets gate-mode1/3 and rcntEndGate resets2/3. This supplies
an emulator qualification route, NOT physical polarity or latency proof.
Source searches find no explicit T0/T1 owner in the current engine/private game;
kernel/module ownership and actual MODE/COUNT behavior remain unqualified.

Next target preflight must reserve a genuinely unused T0 or T1, preserve teardown
state, verify which edge resets it and measure the actual blank interval. A
BUSCLK/256 candidate gives576ticks/ms and a113.78ms wrap period: faster clock
settings can alias delayed handler age through16-bit wrap. Even this candidate
requires verified periodic edge resets and fail-closed startup/reconfiguration.
No MODE constant or guessed NTSC blank length is currently authorized for copying.

Only admit a copy after stable configuration/epoch and remaining blank cover the
qualified dispatch-plus-GS-copy budget with margin. Detect crossed edges during
sampling, late delivery, wrong mode and reconfiguration; both ISR and fallback
must use the same guard. Handler-order timestamps and VSINT remain insufficient.
Budget qualification needs the original conversion packet, empty/exclusively
owned consumer paths and full resolution, not a no-op packet. No new runtime,
console deployment, frame-time gain or physical timing evidence in this audit.

Recheck linked functions using the fixture's `vehicle-playground.elf.sym` with
`mips64r5900el-ps2-elf-objdump -d --disassemble=FUNCTION`, for
`_ps2sdk_init_timer_impl`, `InitTimer` and `InitAlarm`. The private fixture path
is `F:/Projects/tyrax2-paint-triple-20261011-v1/game/bin`.

Bind an actual old running/completed sequence plus empty queue/no newer submitted
prefix to the readiness callback; CHCR idle alone is insufficient. Preserve current
native prefix/context/bank/HUD owners, freeze original conversion/restore commands
and keep packet/draw source alive through completion. No arbitrary caller boolean
can substitute for the actual queue and framebuffer owners. Publish/arm only after
the actual old job starts; explicitly exclude zero sequences/unsupported layouts.

Then test the actual Core/queue adapter, native source binding, positive held/moving
activation and exact original source/main/env contracts. Qualify physical blank
budget and copy completion BEFORE Hybrid2-only same-ELF net A/B/reverse. No native
game build, emulator boot, real-PS2 trial, default or60/ceiling claim in this step.
Computer and console remain on; no console reset was sent.

Reproduce with a fresh output directory under WSL:

```sh
python3 /mnt/f/Projects/tyra-editor/tools/verify-hybrid-copy-protocol.py /mnt/f/Projects/tyrax2-hybrid-copy-adapter-replay --adapter --ee-cxx /home/spider/.cache/tyrax/native/toolchains/3594ba072b1f303047137e63/ee/bin/mips64r5900el-ps2-elf-g++
```

Each archived sha256.json covers original runner files except itself; this README
is outside the inventories. Proprietary game states/assets are not included.
## Full-game integration in progress

`tools/tyrax2-hybrid-copy-fixture.py` derives a private full-game fixture from
the bound A38 reverse source, never the rejected A39 hook. Both arms retain
Hybrid2, roads, live reflections and original geometry. The trial binds the
actual read-only old-native queue predicate, captures the original copy emitter
into immutable81qw RAM once, and arms only the pending Core job. Main retires
the copy and its exclusive FINISH before the next actual prefix or foreign GS
work can reuse the source. ISR does not call renderer waits/builders/cache APIs.

T0/T1 and GS IRQ reservation fail closed on configured/enabled owners; START,
END and one-shot FINISH handlers preserve prior ownership on final teardown.
The FINISH handler masks itself before a nonblocking completion poll, including
the FINISH-before-GIF-idle race. New adapter `copyFinishISR` has four additional
mock controls. The288-tick admission budget remains experimental; every full
game copy must separately prove physical inside-blank completion. Pixel equality,
full-game native linking, positive ISR/fallback activation, physical timing and
reverse order remain gates, not claims supplied by component tests.

Integration source review finds V3 queue predicate insufficient: beginRecordingFrame
resets nativeInFlight for the NEXT bank before the OLD presentation retires.
V4 adds separate nativePresentationSequence, published after the final prefix
flush, preserved across beginRecordingFrame and revoked after the old Core callback
BEFORE any new enqueue. The predicate never advances or flushes. Actual-source
65536 combinations vary transient nativeInFlight independently;240 admissions,
optimized/sanitized and EE load/no-store/call gates pass. Structural checks bind
publication/reset ordering to the actual source. This is queue evidence, not
proof of full Core/GS/IRQ safety or performance. Accepted V3 above is historical.

Adapter V6 completion entry passes46 guards and644 actual tail-validator cases
in optimized/sanitized builds plus EE compile/direct ISR relocation checks.
[V6 component inventory](v6/report.json) and [queue V4 inventory](queue-v4/report.json)
retain full sources, tests, logs and EE objects.
Full-game fixture V1 is generated but never built; V2 fails native compilation
because inherited private queueProof shadows the namespace callback. V3 fails
the generator's ambiguous source anchor. All rejected attempts remain private.
V4 resolves explicit callback qualification and source anchors; native build is
in progress, not yet a runnable/qualified candidate.

V4 native build now succeeds:496 source files match native mirrors and linked
ELFb37d8abcca32ed99556fc27d9942a240e91ee64e08b90a403593b32e66e6bcdb.
The required linked POD witness is HybridCopyTrial::rows; adapter also has a
C++ guard-variable symbol, so an unfiltered name-suffix count is unsuitable.
Hidden emulator shows installed0/words0/refused32 in held arms and zero
candidate activation. REJECTED before PS2 pricing. V5 records one reservation
failure on main, outside steady-state measurements, to identify the owner.
No ownership gate is relaxed because an emulator run refused installation.

V5 refusal records m0/m1=0, I_MASK1802/I_STATd, purported IMR551b6000.
This is not a timer owner: direct IMR read mirrors CSR. The linked SDK provides
GsGetIMR (syscall70) and GsPutIMR (71). V6 reads the kernel mask on MAIN, freezes
the exclusive owned word for direct ISR writes, and restores through main
GsPutIMR. No IMR read-modify-write and no SDK syscall in ISR. This matches the
[PCSX2 primary GS read/write implementation](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS.cpp),
not a claim that emulator timing establishes hardware safety. Retain all ownership
and physical completion gates.

The reader first rejects V4 at exactly six first-gas flip counts: common capture
retirement happens before begin(), so that one row has zero rather than one.
The revised reader requires exactly zero at offset1320 and exactly one elsewhere,
binds the explicit source pre-fence, then passes all36 main/env raster and baseline
travel/geometry gates before correctly rejecting V4's installed0 activation.
This boundary contract does not permit omitted steady-state flips/geometry.

V6 kernel mask7700 reveals SDK's VSINT-unmasked/GS-INTC-disabled baseline.
V7 admits ONLY known7700/7f00 with GS INTC disabled, masks VSINT before taking
the GS interrupt, and restores the saved kernel mask at teardown. Other timer/
GS ownership refusals remain. Startup warmup must acknowledge ONLY CSR8:
the linked SDK wait ORs the complete CSR and could erase this old native FINISH.
V7 actual full-game emulator completes six trips with81qw/fallback copies and
zero recorded late/drop/refusal, but ISR0: fast emulated consumers let main
withdraw before START. This does not qualify independent dispatch or price.

V8 HELD-only qualification keeps the next clear prefix unsubmitted for40ms
at frame1 in EVERY arm. Other held frames use ordinary asynchronous Core,
retiring after the loop. First candidate held set reports32 copies:1 ISR,
31 fallback,32 completions, zero late/outside/drop/refusal. This exercises the
actual stable old-presentation identity while the new bank is recording.
Physical moving still needs positive ISR activation; emulator moving does not
need artificial delay, and its throughput is never hardware evidence.

The actual linked V7 application IRQ audit reaches six routines plus exactly
two constructor/source-bound queue/window indirect callbacks, no kernel/cache/
heap/wait calls. `tools/tyrax2-hybrid-copy-irq-audit.py` binds full native sources
and symbol ELF before inspecting that closure. It is not arbitrary indirect
target resolution, BIOS routing, GS exclusivity, cache safety or WCET proof.

V9 adds aligned-row CT16 display capture to the existing main/env readback,
54 complete rasters rather than36. Draw-buffer equality alone would miss a
wrong/stale scanout copy. V8 frozen firmware lacks this new gate and is not
qualified for PS2 pricing. V9 native build/full emulator output/IRQ/negative
controls precede physical same-ELF testing. No default promotion.

V9 emulator qualification now passes:496 source/native files, shipped
ELF2f6a2e99eccd321f031cd2dd58bd484429496f27f6eed9eb9780805e86cdb9f7,
54 exact main/env/display readbacks,1262 owned copy completions, eight positive
HELD ISR claims, zero late/outside/refusal/drop,16 rejecting reader controls.
Linked application closure audit passes on this exact ELF too. The first reader
negative attempted to zero an already-zero moving ISR count; preserve that failed
log, repair to mutate actual HELD positives and exactly cross-match moving copy
event counts. No firmware/source/raster changes for the reader repair.

Completion admission additionally requires elapsed Count<288*512 ticks, preventing
dual-edge ages from aliasing a later field after delayed GS/END callbacks. This
checks the observed experimental budget, not a universal WCET. Physical moving
requires positive ISR dispatch and every completion/window/output gate before
net/60fps analysis. Stage helper omits hybrid-copy CSVs from physical inputs.

V9 clean PS2 attempt physical-v3 completes all six phases: full reader PASS,
54 exact main/env/display rasters and16 rejecting controls,971 moving copy
completions with day160/night221 ISR dispatches and zero error counters.
Copy median0.183302ms/max0.204512ms. OFF/ON/OFF presentation copy-kick Hz:
day47.201/57.559/48.225; night40.303/51.230/40.188. Both net gates PASS,
both stable60 gates FAIL (candidate21/77 intervals>25ms). Night p95 worsens
33.367->33.648ms; no default/no-regression claim. The old non-pacing scope
includes new fallback waits, so its~3ms increase is not packet-preparation cost.
Private fixture F:/Projects/tyrax2-hybrid-copy-game-20261011-v9 retains complete
sources/native bindings/ELFs/readbacks/IRQ report and physical-check-v3.json.
Reader replay is byte-identical, SHA256
008b87d566d0778d9ed2f1d348bcca6b10ad9a7a3798e3e1ea6624ef0fafeea2.
Full repo archive and reverse order remain pending. First attempt fails in phase5
with freepad DMA Busy; second completes but reader rejects stale exports copied
from that attempt. Preserve both; --stage-dir stages original game/bin into a
fresh attempt, --physical-dir selects it without relaxing provenance gates.

`tools/tyrax2-hybrid-copy-archive.py FIXTURE OUT --physical-dir physical-v3
--physical-report physical-check-v3.json --emulator-report emulator-check-retry-reader.json`
preserves both full54-raster reports, exact readers, all bound sources, original
ELFs, linked IRQ disassembly and ALL immutable physical launch inputs. Archive
readers can replay unchanged provenance gates; no asset omission waiver.

First-order game-v9 archive contains998 hash-verified files. Its exact copied
reader replays both reports successfully. Fixture --reverse changes ONLY the
mode order to ON/OFF/ON in both moving and held arms, keeping Hybrid2 and original
workload/output gates. Checker admits exactly the two predefined orders, binds
their actual source expressions, requires positive ISR on every candidate and
compares BOTH reverse candidates against the middle baseline. Negative controls
mutate the first actual candidate, never an inactive arm. Full native/emulator
qualification still precedes any reverse PS2 timing.

Reverse-v1 ELF1510ae7908063612c3f7ce8fbf42233ac1fbbb49b8e6ea04ef4380cf0973cf8a
has496-file native agreement and linked IRQ closure PASS. Full hidden emulator
passes all54 rasters/16 rejecting controls,2524 owned copy completions/14 held
ISR plus one moving ISR/zero errors. Native verification must finish before IRQ
audit (initial missing-binding invocation failed, later bound audit PASS).
Owned emulator stopped; clean303 inputs staged for same-ELF reverse PS2 series.

Reverse PS2 full gates PASS:54 rasters/16 controls/1943 moving copies, all four
candidates have independent ISR dispatch, no error counters. ON/OFF/ON copy-kick
Hz day57.559/47.656/57.450, night51.247/40.074/51.230. Both net gates PASS,
both stable60 FAIL. Candidate long intervals21/22 day and77/77 night; night
p95~33.647ms vs OFF33.375ms retains slight regression. Copy median0.183295ms,
max0.204044ms. game-reverse-v1 archive998 hashes verify; copied readers replay
BOTH reports byte-identically. Completed-run client stopped, devices left ON.

Next SOURCE question: START makes one readiness attempt; later native completion
inside the SAME blank currently has no retry entry. Inspect bounded IRQ ready
notification with unchanged physical window/job/prefix/FINISH gates, both orders
of VIF/GS completion and no waiting/poll storm in ISR. The source opportunity
is not physical causality evidence and is not A39's rejected main-ready hook.

Late-ready component adds nativeReadyISR, shared by START and future native
notifications. It cannot open a field, reclaim a withdrawn job, copy twice or
ignore remaining physical budget. Added32 readiness-mask cases and12 ordered
START/VIF/GS normal/expired-window cases; runner expects90 guards and inspects
four EE IRQ entries. Full-game notification binding is not implemented yet.

V7 component qualification passes90 controls in BOTH optimized and ASan/UBSan,
644 extracted actual packet-validator cases each, EE60-byte adapter ABI and
four direct IRQ relocation gates. v7 archive inventories all generated/source
evidence with verified hashes. Native-ready callback binding, exclusive DMAC
reservation, linked closure, full-game outputs and real PS2 A/B remain pending.
Actual queue ISR stays OFF: the new observer must not advance a queue or run
lazy-flush/cache work. A one-shot native GS FINISH handler must preserve its latch
for fallback on refusal; VIF/GS may arrive in either order. Do not reduce budget
or claim this source opportunity explains measured misses without target evidence.

A42 fixture --late-ready adds a private read-only VIF DMAC observer and one-shot
native GS FINISH retry. A41 copy remains ON in all arms; only notifications vary.
HELD-only START deferral opens the field without claiming, qualifying actual GS/
VIF ISR dispatch as kick kind3. It changes no moving workload/delay. Reader binds
actual mode/source, demands kind3/witness equality and positive held activation,
positive moving activation on PS2, exact54 rasters and unchanged window budget.
Final main teardown explicitly verifies restored DMAC/timers/GS IMR/INTC masks.
Component runner now91 guards/five IRQ entries. Full native/linked/raster/physical
qualification remains pending; source implementation is not a performance result.

A42 V1 native496-file/ELF1cdb92ca...86f147 and linked IRQ closure PASS. Four
IRQ roots contain six constructor-bound callback sites (two each in START,
native and GS FINISH); gate follows actual jalr loads0/4 rather than every data
use of t9. Both earlier rejecting audit logs remain private evidence. Hidden
emulator's first candidate held performs two actual kind3 IRQ copies, no errors;
full54-raster/18-control/restoration/native activation gate still pending.
Archive runner accepts A42 only with18 controls (A41 remains exactly16).

A42 V1 complete emulator qualification PASS:54 exact rasters,18 rejecting
controls and restored DMAC/timer/GS/INTC masks. Moving candidates receive535
VIF/535 GS notifications each but zero kind3 copies; HELD gets two kind3 copies
per candidate set. Emulator activation is not physical pricing. Checked owned
emulator/Xvfb stopped; clean303 inputs deployed with editor-equivalent reset,
client36644 successfully starts same ELF on PS2. Physical gates pending.
Source complete()->takeMain withdraws Armed before waiting for old readiness;
native notifications cannot claim after withdrawal. Investigate that policy only
after the frozen A42 result, preserving ownership and fresh-window fallback.

A42 V1 PS2 PASS for54 rasters/18 controls/2943 moving copies/restoration;
late-ready moving copies day2/night17, zero errors. Notifications OFF/ON/OFF:
day57.789437/57.789431/57.789380Hz (19 long intervals each),
night51.908696/52.346750/51.810958Hz (71/67/72 long intervals).
Day net FAIL, night net PASS but small and not reversed; both stable60 FAIL.
Copy median0.181956ms/max0.205644ms. No default promotion. Archive
native-ready-game-v1 contains998 hash-verified files; exact copied readers replay
both reports byte-identically. Owned completed client stopped, devices remain ON.
Next candidate: retain Armed ownership until old native readiness before main
withdrawal, with component race qualification and unchanged physical gates.

A43 adds optional takeMain retention while native queue/GS FINISH/GIF readiness
is incomplete. WaitNative leaves the immutable source and ISR ownership intact;
ready main still withdraws to the existing fresh-field fallback unless IRQ has
claimed. Added32 readiness-mask and8 IRQ-order/expired-window/policy controls.
V10 optimized/sanitized131 guards,644 actual packet cases and five EE entries
PASS. Initial V9 test FINISH-clear expectation was wrong for fallback (three,
including stray-latch clear); evidence preserved, protocol unchanged. Full-game
--retain-ready keeps copy and notifications ON all arms, varies only retention;
reader requires actual positive moving waits on PS2 and20 rejecting controls.
No physical result or default promotion yet.

A43 V1 physical completes all trips/restoration but reader REJECTS night control
phase3 outside=1: job696033 starts age6, completion observed age827/endAge3,
valid0 and420208 Count elapsed (~1.425ms). No FPS/net result admitted and no
window waiver. Late observation versus late hardware completion is unresolved.
Candidate moving WaitNative occurs only once in day519/night469 frames; not
evidence of a substantial moving benefit. Completed owned client stopped, devices
ON. retain-ready-game-v1-rejected contains998 verified hashes, exact physical
reader rejection replays and emulator report replays byte-identically.
Archive --rejected-physical reruns the strict checker, requires this rejection,
preserves all bound sources/ELFs/rasters/events/launch inputs and stores an
explicit valid=false physical-rejection.json, never a passing physical price.

