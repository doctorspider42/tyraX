# A18 three-path frame experiment

This experiment separates scene production, descriptor-to-packet preparation,
and execution of an owned command stream using the same frozen scene state.
It is a diagnostic harness, not a proposed production cache.

Current checkpoint (2026-10-10): clean V4 physical day and V5 physical night
both pass, locating a large net cost downstream of descriptor production.
V5 repairs diagnostic allocation and passes four emulator states / 80 GS images.
Later-view and parked physical prices remain pending. The older V3 day is
interim because its outer sampler was not suspended. Do not mark the full
experiment complete or present emulator times as PS2 prices.

## Corrected historical interpretation

The old V7 23.33 versus 10.73 ms result compares full repeated validation with
sealed validation of an ALREADY captured terminal replay. It does not compare
ordinary scene rendering against replay. The earlier conversational shorthand
was misleading. No A18 conclusion may use that difference as scene-prep cost.

## Required gates

1. Current-code ordinary frozen Scene rendering with roads and a live reflection
   capture, at states reached during the actual drive. Freeze simulation, not
   by deleting geometry, passes or lights. State scopes and exclusions must be
   identical in every arm. First qualify one daytime driving state, then extend
   to turn-heavy, parked and nighttime states.
2. Own a draw-event list at StaPip Core entry: descriptors, matrices, stream
   bytes, stamps, light state and view state. Preserve environment-target and
   batching events in order. Texture resources stay live during this terminal
   frozen experiment; do not claim the list survives a scene reload. No pointer
   to temporary descriptor/geometry storage may be dereferenced on replay.
3. Compare repeated ordinary controls, list replay, and ordinary return using
   actual main and reflection GS readbacks. Refuse an incomplete event model
   or differing rasters. Qualify lifetime and unsupported producers separately.
   Per-draw copied state restoration is currently a correctness mechanism, NOT
   a priced minimal dispatch path.
4. Only after list qualification, capture and qualify the matching complete
   command closure, including VIF/VU/GS prestate, program changes, target changes
   and mutable REF ownership. The old V7 capsule hardcodes a specific resident
   program layout; it cannot be transplanted as a generic current-frame replay.
5. Time ordinary/list/command routes with equal synchronization and completion,
   no repeated validation or file I/O inside windows, both orders and bracketing
   ordinary controls. Report inclusive non-pacing cost, not pure EE/VU/GS times
   or ordinary moving-game FPS. Only then replace the dominant preparation stage.

## In progress

`tools/tyrax2-bag-tape.py` clones source-bound A16 into a private fixture, leaves
its rejected target gate OFF, preserves the TEX1 correction and EE paint, and
adds the descriptor qualification prototype. It freezes day drive offset 1380,
renders Scene without HUD/postFX, and forces shared capture in every ordinary
arm. Four 24-frame stages: capture ordinary, ordinary control, list replay,
ordinary return. Main and env raw GS readbacks occur outside any timing (none
is collected yet). Both authored projects and production engine stay unchanged.

V1 preparation failed on Windows default text encoding before building; V2
uses explicit UTF-8 for patched sources. V2 captures 102 draws / 106 events /
1,833,792 stream bytes. Main and env list-replay rasters match the captured and
ordinary-control rasters exactly, but ordinary RETURN changes 192 RGB bytes in
the main image, within x=13..373, y=131..147. Env remains identical.

V3 adds an all-ordinary control marker and a fresh-run clone of the same bound
ELF (`289085e6fa767e15416bb37a4322afeaeda200ef0ce5ad223db247e67c6ef50d`).
All four control main/env images are identical within that boot. Replay on the
same ELF again matches ordinary exactly, then the return differs in the same
192 RGB bytes / bounds. Cross-boot pixel equality is NOT assumed: the driving
approach uses real dt and can settle at slightly different poses.

The `v3-control/` and `v3-replay/` archives preserve 504 / 503 hash-verified files:
473 bound sources, ELF, raw GS images, logs and reports. Assets are hash-listed
rather than duplicated; parent A16/A17 archives retain the workload resources.

V4 adds unpriced source-descriptor comparisons at ordinary control and return.
Control matches; return differs at 37 ordered draw positions (mask 27: counts,
positions, colours or coordinates). The automatic heavy-pass interleaver sees
a recording-generation gap while replay bypasses scene production and restarts
its probing. Thus ordinary return can change submission order.

V5 pins the interleave choice active immediately before freezing, identically
in every arm. All four main and env emulator rasters now match byte-for-byte;
both ordinary descriptor comparisons report zero mismatches. This intervention
removes the observed confound, not a demonstrated production FPS bottleneck.
The bound ELF is `8f84d37fb90a443229ef26957b82cc20ba74ae27e43a94fd12e70316a7759698`.
The same-ELF all-ordinary emulator control also passes. Physical PS2 completes
all four stages with 104 draws / 108 events / 1,916,304 owned stream bytes,
zero checked descriptor mismatches and byte-identical main/env images across
all stages. Physical and emulator poses are not identical (real-dt approach);
comparisons are strictly within each run. The physical host remains alive at
PID 5216, recorded in the V5 fixture's `physical/editor-deploy.json`.

`v5-replay/` preserves 529 hash-verified files including physical evidence;
`v5-control/` preserves 504. The first ordinary/list correctness gate passes
for this one frozen daytime Scene. This is not generic lifetime coverage,
moving/night qualification, or evidence of an FPS gain. No complete three-route
comparison or performance pricing has been obtained.

## Resume here

1. Keep the V5 evidence immutable. Production code and authored maps were not
   changed by A18. Keep the rejected target gate OFF and preserve EE paint and
   corrected TEX1 in the private baseline.
2. Build a fresh command-capture fixture from the bound V5 sources. Inventory
   complete frame segments, external submissions and VU program transitions;
   own every mutable REF and preserve target/texture ordering. The old replay
   helper's hardcoded MSCAL 1202 and resident program identity are NOT a generic
   interface. Fail unsupported state rather than removing those guards.
3. Qualify command replay against the corresponding ordinary/list main AND env
   images. Account explicitly for replay startup and terminal state. Restore
   normal rendering only after proving it safe; do not silently drop a pass.
4. Add equal-synchronization timing windows after correctness. Keep readbacks,
   logging, validation and capture outside samples; amortize or expose helper
   cost. Test both arm orders and ordinary bracketing controls. Per-draw copied
   RendererCore3D/light restoration in the list path is currently overhead,
   not a proposed production dispatch cost.
5. Extend to several frozen states reached during driving/turning and night,
   then use the dominant difference to choose production work. Do not claim
   pure EE arithmetic from inclusive time, or 60 FPS from a frozen experiment.

Nothing was shut down. Only owned hidden emulator instances were stopped.

## Command continuation (in progress)

`tyrax2-command-tape.py` creates fresh private fixtures from bound V5. Capture
V1 failed compilation on a missing debug include and was never run. V2 passes
capture: one flush / one submission, 2,188 root qwords, 1,849,312 owned bytes,
753 microprogram starts, no MPG and byte-identical resident program memory.
The first MSCAL is 1226, not old V7's 1202. Its prestate is BASE=22, OFST=461,
TOPS=22, TOP=483, DBF=0. The new helper uses two properly prepared draw-finish
handshakes for that DBF, then restores VU data and VIF state; never bare MSCAL.

V3 command qualification rejected the target before replay. V4 identifies the
actual reason: two buffers with HYBRID output (32-bit render / 16-bit present),
not an ordinary double-buffer target. Both attempts remain rejected. The new
private target selector preserves hybrid target zero and its normal presentation
copy instead of changing the scene's colour-depth setting.

`tyrax2-command-pricing.py` prepares O/O/L/O/C/O/C/O/L/O, 224 frames per stage,
128 samples at local 64..191 in each non-capture stage. It pins the target and
interleave order, retains completed rendering and presentation, excludes actual
pacing, and includes command startup in total cost (also reports it separately).
Full startup validation, ownership hashing, actual GS readback and file output
occur outside samples. Ordinary file logging, contact telemetry and periodic
allocator reports are disabled identically. Price V1 failed compilation on
missing standalone header dependencies and was never run; V2 is building.
Price V2 passes all ten ordinary/list/command/return main and env images in
the daytime AND nighttime emulator runs (night: 119 draws, 8 active lights).
Its physical capture is rejected before any command replay. The post-frame
snapshot is not guaranteed idle after a hybrid presentation copy. V3 adds a
common `align2D` completion fence after presentation, inside every measured
arm, and reports busy VU/VIF/GIF snapshot state explicitly. V2 has no accepted
physical prices; its emulator raster evidence remains separate from hardware
completion. V3 native binding covers 476 sources, ELF
`0839a8acec9610f9cbf3697d84d542b2b15f83e34ccdcbaf99053d38440e5f59`.
V3 emulator qualification is running; no physical timing result yet.

### V3 interim result and V4 cleanup

V3 daytime physical completes all 10 stages / 1,152 samples and exact main/env
rasters. Ordinary controls are 25.615..25.838 ms, list replay 25.208/25.231 ms,
command replay 9.565/9.532 ms including startup 0.095/0.072 ms. These are
serialized frozen-frame costs, NOT ordinary moving FPS or pure EE/VU/GS work.
`command-price-v3-drive/` contains 570 hash-verified files; read-only archive
reanalysis reproduces the result. This is INTERIM, not the clean final run:
the inherited outer Engine drive sampler still resumes after freeze and fills
its buffer outside the pricing clocks. V4 suspends both its begin/end hooks,
including counter branches inside rendering. Do not attribute its cost by
subtracting different-ELF prices. Current physical analyzer requires suspension
unless explicit `--allow-legacy-observer` requests historical comparison.

V3 nighttime physical stops after stage 8 with `freepad: DMA Busy`; there is no
final control or PRICEDONE, so no night price is accepted. The editor-equivalent
reset for V4 receives only the same freepad output, not a new boot. Its returned
`console-output-received` status is NOT boot confirmation. One reset was tried;
no repeated resets or machine shutdown. The user has been asked asynchronously
to power-cycle to ps2link. Current owned waiting client is PID 20552 with record
`F:/Projects/tyrax2-command-price-20261009-v4/physical/editor-deploy.json`.
Keep it alive until explicit recovery; use a fresh deployment directory for
the next attempt, preserving this failed launch evidence.

V4 native binding: 476 sources, ELF
`667b549ce061ed0fd9d725ee291276268bfef5546eb1d5943fba40e9421fd640`.
Its daytime emulator passes all ten rasters and has no frozen drive-sampler
activity. Same-ELF clones select `three-path.cfg`: night `3 1380`, later drive
`0 1500`, parked `0 1250`; default day drive is `0 1380`. Finish these emulator
qualifications while waiting for physical recovery, then price the four states.
Use command-price-analyze, with --physical only for complete hardware runs.

The interim daytime boundary agrees with the route intervention: ordinary
preparation is 16.126..16.344 ms inclusive, while work after that boundary is
about 9.49 ms; command replay including startup is 9.53..9.56 ms. This supports
investigating descriptor-to-command preparation rather than upstream scene
production. It does not isolate arithmetic from EE memory/cache costs or every
wait, nor qualify a reusable whole-frame production cache. List replay itself
copies saved per-draw renderer/light state, and command replay relocates owned
REF payloads. Report the net intervention and retain these implementation costs.
The frozen synchronization also removes ordinary N/N-1 overlap, so do not turn
the 25 ms total into a claim about live-game FPS.

### Clean V4 emulator coverage completed

All four same-ELF states pass 10 stages, 1,152 protocol samples and 448 command
replays each. Every main/env image agrees byte-for-byte within its state,
including ordinary returns; checked source descriptors remain equal. No legacy
drive-sampler activity occurs after freeze. No emulator timings are reported.

| State (phase/offset) | Draws | Events | Owned command bytes | VU starts | Lights |
| --- | ---: | ---: | ---: | ---: | ---: |
| Day drive 0/1380 | 102 | 106 | 1,849,312 | 753 | 0 |
| Later drive/view 0/1500 | 106 | 110 | 1,345,696 | 668 | 0 |
| Night drive 3/1380 | 120 | 124 | 1,941,888 | 798 | 8 |
| Parked 0/1250 | 133 | 137 | 2,337,344 | 786 | 0 |

Archives `command-price-v4-{drive,turn,night,parked}-emulator/` preserve the
native binding, raw GS/VU/command data, logs, reports and tool snapshots. Drive
contains 541 hash-verified files including the failed hardware launch; the
other three contain 531 each. Read-only reanalysis reproduces night, later-view
and parked reports. The analyzer rejects all 11 corruption controls. Python
compilation and git diff whitespace checks pass. Both authored project hashes
are unchanged. Owned emulator processes are stopped; PS2 host PID 20552 stays
alive. No computer or console shutdown was performed.

Remaining work is specifically hardware, not another implementation proposal:
after the requested power-cycle, deploy the bound V4 ELF into a FRESH physical
directory and complete the four configurations above with the editor launch
procedure. The existing V4 physical directory is failed evidence, never reuse
it. Analyze only completed PRICEDONE runs with --physical, then reconcile the
clean prices and decide the production change. Do not promote the private
whole-frame tape as a solution for moving-camera rendering.

The user's subsequent request explicitly defers the reset and permits only
code/emulator continuation for now. [A19](../tyrax2-list-join-2026-10-09/README.md)
records that offline audit and a narrow redundant-gather qualification. It does
not replace the clean V4 hardware gate above. Leave the console and waiting
host untouched until the user returns; no shutdown or repeated reset attempts.

### Clean V4 physical recovery, 2026-10-10

The user confirmed the console reset. Fresh bound same-ELF fixtures are
`F:/Projects/tyrax2-command-price-20261010-{day,night,later-view,parked}`.
The first uses `tyrax2-deploy-ps2.ps1 -ConsoleAlreadyReset`: only the network
reset/listener step is omitted after this explicit recovery. All ordinary
ownership checks, channel cleanup, marker, input pins, 10-second settle and
keep-live timeout remain. The helper records this exception as `resetMode`.
There were no live ps2client processes before launch. The initial 15-second
output timeout was unconfirmed, not failure; client PID 45020 was kept alive
and a fresh loadelf/asset startup followed without a resend or reset.
This supersedes the offline-only hold, not the prohibition on shutdown.

Day completed and passed the full physical reader: ten stages, 1,152 samples,
448 command replays, identical main/env GS images, zero checked descriptor
mismatches and unchanged 303 launch inputs. Stage means in ms: ordinary
25.744655..25.932887, list 25.197230 / 25.225687, command
9.563365 / 9.533562. Adjacent ordinary-control savings are 0.518968..0.721850
for the list and 16.181962..16.355714 for command replay. Command startup
0.093209 / 0.071259 ms is included, not subtracted. Ordinary preparation
16.251413..16.435107 ms leaves about 9.49 ms after its boundary.

The exact frozen pose is z=-58.902039, speed=13.984566: 103 draws, 107 events,
759 VU starts and a 1,875,184-byte closure. Do not subtract old V3 prices to
estimate observer tax: real-dt pre-freeze motion and code layout differ.
The clean within-ELF result confirms the downstream preparation gap, not
pure EE arithmetic or live FPS. [Day archive](command-price-v4-physical-day-20261010/archive-sha256.json)
contains 533 hash-verified files; read-only physical reanalysis passes.
After completed export, the normal editor protocol released owned PID 45020,
sent one reset and launched night as PID 39300 with a confirmed fresh boot.
Other state results remain pending at this checkpoint.

Night subsequently FAILED after stage 7: `std::bad_alloc` precedes the
`freepad: DMA Busy` messages. No PRICEDONE or complete timings exist; the
physical reader correctly rejects it. [Incomplete night evidence](command-price-v4-physical-night-incomplete-20261010/archive-sha256.json)
preserves 532 files. No further network reset was attempted; PID 39300 is kept
alive pending the user's recovery. Later-view and parked V4 runs are staged
but NOT launched. Correction to the older V3 account: its raw log likewise
contains bad_alloc before freepad; this precursor was previously overlooked.

V5 bounds a diagnostic-memory repair: size the already validated CNT/REF/END
root plus every copied REF payload before allocating; retain the 4 MiB upper
bound but allocate only the required 64-byte-rounded bytes. Never move a live
closure using realloc: embedded DMA addresses would become stale. Allocation
moves from arm() to the one capture flush, outside priced samples. The original
payload/program/state comparators remain. Off-clock PRICEHEAP rows describe
allocator arena/used/free blocks, NOT total available EE RAM. Memory pressure
is a hypothesis, not a proved cause of bad_alloc or the original FPS problem.

V5 native build binds 477 files with ELF
`2c44d651446d6f6c4fc455b7d8058a322604f18d1689f9f409b594096d005f77`.
The actual-arena host oracle passes 3,120 synthetic comparisons and malformed
chain controls under ASan/UBSan. The reader rejects 14 negative controls,
including bad_alloc, undersized closure allocation and missing heap stages.
The first day emulator run passes all 20 main/env readbacks. The user confirmed
another physical reset; the next night deployment must omit the network reset.

### V5 night recovery completed, 2026-10-10

After that user-confirmed reset, the helper released owned PID 39300 and
launched V5 night without any network reset. Fresh boot and full completion
are confirmed, not inferred from the deployment status. Owned host PID 17744
remains alive; console/computer stay on. No additional hardware launch followed.

All four V5 emulator states pass: 80 main/env GS images, exact within each
state including ordinary returns. Physical night passes ten stages, 1,152
samples, 448 command replays, zero descriptor mismatches, all 20 GS readbacks
equal and unchanged launch inputs. Frozen pose z=-56.974430, speed=14.770270,
yaw=0, 121 draws / 125 events, eight lights and 805 VU starts. Closure allocation
is 1,992,256 bytes, saving 2,202,048 bytes versus the old fixed reservation.
Every heap stage is present; no bad_alloc/freepad failure occurs in this run.
This supports diagnostic memory pressure as the recovery hypothesis, not an
exact diagnosis of the earlier allocator failure or the ordinary game's FPS.

Physical night stage means (ms):

| Stage | Route | Inclusive non-pacing work | Preparation boundary |
| --- | --- | ---: | ---: |
| 1 | Ordinary | 29.229898 | 18.549281 |
| 2 | List | 28.993552 | 18.317290 |
| 3 | Ordinary | 29.392438 | 18.714203 |
| 4 | Commands | 10.727233 | 0.095212 |
| 5 | Ordinary | 29.224782 | 18.546108 |
| 6 | Commands | 10.701206 | 0.074732 |
| 7 | Ordinary | 29.227377 | 18.547544 |
| 8 | List | 28.983525 | 18.304302 |
| 9 | Ordinary | 29.388257 | 18.707543 |

List saves 0.236346..0.404732 ms against adjacent ordinary controls; commands
save 18.497549..18.665206 ms, including their startup. The ordinary remainder
after preparation is about 10.68 ms. Command replay changes owned REF layout
and startup state as well as skipping preparation: this is a net contrast,
not pure EE arithmetic or proof that all 18.5 ms is recoverable during motion.
The serialized frozen scene excludes update/HUD/postFX and normal overlap;
10.7 ms is NOT a demonstrated live-game FPS result.

Archives: `command-price-v5-emulator-{day,later-view,parked}-20261010` each
contain 536 verified files; [night emulator + physical archive](command-price-v5-night-20261010/archive-sha256.json)
contains 572. Read-only archive reanalysis passes for all five results.
No production defaults, renderer sources or authored maps changed in this repair.

Next decision: retain the descriptor boundary and distinguish normal StaPip
packet construction from the ownership/snapshot/native-enqueue tail. Design a
single coarse, disjoint split with whole-window controls, or an intermediate
owned-chain intervention preserving the normal submission tail. Audit its
callers/lifetimes before instrumentation, keep both target images and ordinary
returns, and price observer cost. Do not substitute another generic cache or
infer memcpy-only cost from the current downstream gap. Later-view/parked
hardware coverage and moving-camera production qualification remain separate.
