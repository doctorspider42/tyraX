# TyraX2 performance investigation register

This is the single decision register for the Vehicle Playground movement,
camera-turn, roads and vehicle-reflection performance investigation. Read and
update it before starting another experiment. Detailed reports and raw evidence
remain at the linked locations; this file records what they mean for the next
decision rather than replacing their provenance.

Last reconciled: 2026-10-12. Documentation is English by repository policy.

## User Checkpoint: Console Trial Finished; Paused

Repository checkpoint: the user requested committing and pushing all current
work to codex/tyrax2-foundation, then stopping. Source-only tyrax2/verify tools
are explicitly included by .gitignore; downloaded runtimes and compiled local
host tools remain excluded. Archive .gitattributes disables line-ending
normalization so sealed source/log/raster SHA-256 records survive cloning on
Windows and Linux. Preserved evidence and rejected trials are records,
not production-default promotion. No new performance experiment is authorized
by this publication request. The active performance goal remains paused.

The requested effective terrain neighbour-LOD V1 console attempt completed on
2026-10-12, with full emulator and physical correctness gates passing. Its
both-order gain is small (0.045194/0.034259ms), not stable60 qualification.
Finish evidence preservation and the user summary, mark the active goal paused
and stop. Do not open another lead or run another console attempt. Keep computer
and console powered on. This supersedes earlier automatic continuation consent;
the performance objective remains unfinished, not achieved or proven impossible.

## Actual Presentation Reconciliation / Input Representation (2026-10-12)

Fresh current-reader replay of native-segments V3 physical-v1 passes the full
source/ELF, six-trip image/pose/lifetime and identical-capture gates, now including
six actual presentation CSVs. Each has538 records for537 gas loops, slot1/ISR0.
Actual OFF Hz59.060/59.169/59.169 with8/7/7 misses; ON50.530/50.530/50.689
with100/100/98 misses. This confirms the previously rejected early-prefix policy;
entry-loop late counts268..269 must not be substituted for display misses.
The previous sealed physical archive remains unchanged; a separate extended
reader archive preserves this additional interpretation, not a new console run.
Extended actual-present archive
finishes with1296 payload hashes/305 inputs and exact fresh emulator AND PS2
reader replay. Correctness qualification is not promotion; net remains negative.

Next representation lead was checked against C21 and the external critique
before source inspection. Qualified event-audio ClipTC uses V4 position/ST/color
unpacks. WorldCellDirect::appendSdk still uses the same SDK emitter and budgets
4 + streams*(vertexCount+1) Qwords per partition. It removes intermediate builds,
not fourth-component transport. No V3_32/row-mask encoder exists in that bound
engine. This is source evidence of a possible format change, not evidence that
transport dominates EE time. Do not assume every W component is constant or
claim25% total-frame savings. Audit exact source values/global VIF state first;
only cold-owned compaction is admissible, never per-frame EE repacking. Preserve
decoded VU bytes, shader/kick/strip contracts and mutable uniform publication.
No new runtime candidate or timing conclusion follows from this read-only audit.

Cold representation component now exists in tools/tyrax2-world-input-compact.hpp.
It admits the current SDK-shaped two/three-stream ClipC/TC partitions only,
copies package heads/kicks unchanged and converts constant-W geometry streams
to masked V3_32 with scoped STROW/STMASK. Small or variable-W streams remain
original V4_32. All transmitted XYZ bits are unchanged; no quantization. Caller
must establish actual incoming ROW/MASK and STMOD=0; the component cannot infer
hardware state, queue lifetime or cache publication. It must never run per frame.
Host V1 compilation rejected a narrowing literal in the runner; retained as
failed evidence. Fresh host V2 optimized plus ASan/UBSan PASS1120 cases,
4770 compacted streams/eight malformed-body controls. Independent admitted-subset
interpreter checks all1024 VU Qwords at every kick, final ROW/MASK, source
immutability and non-growing output, including signed-zero/NaN W and two TOPS.
Three test partitions of72 textured vertices shrink2676 to2172 words (~18.83%),
NOT total-scene/frame saving. Frozen sources/binaries/log inventory:
F:/Projects/tyrax2-world-input-compact-host-20261012-v2/sha256.json.
Primary semantic cross-check: [PCSX2 VIF unpack implementation](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Vif_Unpack.cpp)
uses mask selection1 for ROW; plain V3 W is not a reliable constant. Host tests
use an independently written bounded decoder, NOT actual SDK or native VIF.
Next gate must feed actual SDK-produced bodies and establish incoming VIF state
before any runtime candidate. Native decoded-memory/image/lifetime and real PS2
same-ELF net remain mandatory; no public default or measured benefit yet.

Actual-SDK follow-up generator tools/tyrax2-world-input-sdk-fixture.py derives
only the source-bound qualified event-audio fixture. It extends the existing
cold SDK delivery gate to dump60 C/TC list/independent-strip pass bodies from
actual native SDK emission and real terrain input values, then explicitly
SleepThread after INPUTSDKDONE. No compact body is submitted to VIF; this is a
component, not a moving performance trial. Header/trailer VIF MODE/MASK/ROW
snapshots are observations, NOT drained-queue or future producer-state proof.
Extended host runner accepts that strict binary layout and compares original
and compact modeled VU memory per kick for two TOPS values. Fresh host V3
synthetic optimized/sanitized1120-case gates still pass; native SDK dump remains
unqualified until actual new ELF execution and source-bound offline replay.
Native SDK-dump V1 fails compilation: the exporter treated Quad::words
(std::array) as a pointer. No emulator/PS2 deployment occurred. Correct iterator
access in the generator and use fresh V2; do not silently relabel the failed build.
Fresh SDK-dump V2 generated and native build started after V1 exited. Offline
tools/verify-world-input-sdk.py now requires bound native sources/ELF/library,
actual launch ELF, completed cold SDK/log marker and dump length, then frozen
optimized/sanitized decode and eight malformed binary controls. No output is
qualified until it executes; both live/log.txt assertions must be rejected.
SDK-dump V2 native build completes; engine library copied from the exact cache
before any subsequent build. Native source/ELF binding precedes hidden emulator
execution. The dump verifier freezes all bound source files plus ELF/library and
runtime evidence so an archived verifier can reproduce the offline gate.
SDK-dump V2 binding PASS504 native files, ELF
4e0015c61018675ad9fa04e4c53388067ad52586e5ca88432a71b0a0ad18c512,
library1e50c0e4...7880055. Hidden emulator executes that ELF and reaches
INPUTSDKDONE records60/54856 words; existing SDK12/60/270/54 gate passes.
No assertion in either log. Owned emulator26/Xvfb15 identity checked/stopped
after completed dump, before freezing logs. Both devices stay powered.
Actual SDK component evidence
PASS531 payload hashes, frozen optimized+ASan/UBSan decode and eight corrupt
binary controls. Actual60 bodies shrink54480 to50280 words (~7.709%);75 streams
compact and615 remain original. All VU Qwords match at every modeled kick for
two TOPS values; observed starting/ending MODE/MASK/ROW are all0. This is NOT
compact GPU execution or physical state/queue proof. Native SDK list packages
have counts3/6/9/12/18, none profitable with scoped state restore; independent
strips have3/5/72, so only72-vertex streams compact. The mixed component set is
not the map's moving traffic distribution: do not claim7.709% scene saving.
Next need native VIF decoded-memory and scoped-state gate, then full unchanged
moving image/lifetime/capture and real PS2 net. No public default/FPS result.

Actual VIF input component now being prepared separately from moving rendering.
tools/tyrax2-world-input-native.hpp runs BEFORE world init/frame recording,
drains Vif1Queue and consumes FIFO before touching VU1 DATA at0x1100c000 (NOT
VU0's0x11004000). It replays the qualified60 SDK inputs with shader kicks
replaced by FLUSHE/NOP; compares every4096 VU data word per partition for
original versus compact transfers, with two incoming ROW/MASK states. Save/
restore data, ROW/MASK/CYCLE and unchanged MODE/TOPS are mandatory. Bounded
DMA/VIF idle wait avoids calling DMA completion FIFO completion. This does not
test shaders, moving images, bank leases or performance. Native build/actual
execution remain unqualified; do not promote from source alone.
Input-native V1 generation and native build complete; exact component helper
hash matched before build. Input SDK file retained219424 bytes after staging.
Engine library copied before any later build. Native source/ELF binding then
hidden emulator execution are next; no compact VIF execution claim yet.
Input-native V1 source/ELF binding PASS506 files,
ELF52e5628ab5c86745abaeff5b078828e7f6a7b46cd39e267f96db023916d4b0ec.
Actual hidden emulator VIF gate PASS540 full-data comparisons,150 compacted/
1230 fallback streams,two ROW/MASK states and restored data/state. Both logs
assertion-free. Owned28/Xvfb23 identity checked/stopped after VIFINPUTDONE;
fresh final-log reader passes. Physical same-ELF component still pending;
moving shader/image/lease/FPS gates remain unqualified.
SAME ELF real PS2 input-native V1 now reaches VIFINPUTDONE after successful
editor-network-reset/execee (owned5832). Full actual hardware component reader
PASS540 data comparisons/two ROW/MASK states/restoration,150 compacted and1230
fallback streams. No shader kick or world update executes. Physical ps2link
logging is console-only (debug.hpp explicitly flushes assertions); absent
bin/log.txt is expected there, while emulator file log remains mandatory.
Owned5832 identity checked/stopped after completed component. Fresh final-log
replay and separate combined archive follow; no moving FPS/default claim.
Combined input-native component archive
finishes842 payload hashes and exact fresh copied emulator AND physical reader
replay, including306 staged hardware inputs. Both completed clients are stopped;
devices remain powered. Next moving integration must invalidate encoding choice
on A/B switches, preserve old immutable owner/bank readers and retain the SDK
decoded-memory invariant rather than requiring encoded bytes identical. Rebuild
cost belongs inside same-ELF price, not outside samples. No per-frame repacking.

Cold world input-format V1 integration prepared from qualified event-audio
baseline, direct/paint/hybrid wheels/event audio common ON. Only owner geometry
encoding alternates0/1/0/1/0/1; one-time zero ROW/MASK initialization is common
and STMOD=0 required. Per-cell encoding flag plus terrain preparation epoch
reject stale mode before rendering. Existing owner publication/bank readers,
texture reservation, heads/kicks and max old-size budget remain. Cold compiler
transcodes geometry only; extra rebuild work is inside normal measured updates.
Full native SDK oracle is preserved using audit-only lossless canonical inverse,
not omitted/disabled when encoded bytes differ. Inverse parses VIF boundaries
and skips DIRECT GIF payloads rather than scanning payload values as commands.
Host V4/V5 optimized/sanitized1120 cases pass inverse equality; V5 adds material
DIRECT payloads containing fake ROW/MASK/opcode values. Actual60 SDK bodies also
pass the V4 inverse runner. Moving generator pins helpers before copy and rejects
edits during generation. No moving native/target/net result yet.
Extended price reader retains original image/pose/lifetime/direct/paint/wheel/
audio gates, requires cold-format activation OFF/ON, exact moving capture/env/
paint sequences and actual presentation records on PS2. Existing native-segment
report field shape retained; old archived reader reports must replay unchanged.
Price V1 generation rejects a whitespace-sensitive render guard anchor before
native build or deployment. Use the exact function boundary in fresh V2; keep
the incomplete V1 fixture, not a runtime failure/performance conclusion.
Fresh input-price V2 generation completes and native build starts, no target
result yet. Host V6 adds five inverse corrupt-scope controls, all pass alongside
1120 optimized/sanitized cases. Current extended price reader replays the
qualified event-audio emulator and native-segments physical reports byte-for-
byte unchanged against sealed archives. Input-format archiver includes both
new helpers/generator and all common audio/wheel evidence without altering old
archive payloads. Qualification still requires actual new ELF/full target gates.
Input-price V2 native build now PASS. Exact newly built engine library copied
before future builds; native source/ELF binding precedes hidden emulator. No
target correctness or performance conclusion follows from compilation.
Input-price V2 native506 binding PASS ELF
0c59779f05b7594a524acb6a77a47e4ec2483e3203baa0021c292e4a479b9baa.
Owned hidden emulator16/Xvfb15 identity verified, actual new ELF running. First
phase OFF/ON/OFF main/env/actual CT16 display pixel payloads exact (nine images),
moving OFF113 direct calls/20 full-body audits, ON113/20 also completes. ON cold
geometry totals378456->333096 words,864 compact/2700 fallback streams; this is
cold-built command storage, NOT per-frame transfer/time/FPS. Held ON25 cells/
186 streams positive, both OFF roles zero compaction. All six trips/full reader
still pending. Manifest inherited audio comparison wording corrected only in
descriptive metadata; original generator frozen in provenance and its birth
hash matches. Native source/ELF unchanged by that correction.
Input-price V2 FULL emulator gate now PASS54 exact images/3942 pose rows,
native full-body inverse audits and old-reader/lifetime/delivery/capture gates.
Every moving ON phase113 cold cells/864 compact streams,378456->333096 words;
OFF no compaction. Owned16/Xvfb15 checked/stopped after DRIVEDONE; final-log
reader passes. Separate emulator archive running. SAME ELF physical-v1 editor
network reset/execee launched owned10940 with console output. Full physical
activation/images/net/actual presentation gates remain pending; no FPS claim.
Input-price V2 FULL real PS2 correctness gates PASS54 exact images/3942 poses,
all inverse SDK audits/old-reader lifetime and identical live capture/env/paint
sequences. NET FAIL both orders: forwardSavedMs=-0.0879101965359812,
reverseSavedMs=-0.11890965309198265. Actual OFF58.95..59.17Hz/7..9 missed fields
versus ON58.21Hz/16 misses in every candidate trip (537 gas loops each).
Original versus compact cold rebuild boundary~0.58->1.41..1.42ms per replacement,
93 replacements each phase. Extra cold transcode/copy/allocation is visible in
source; elapsed boundary includes scheduling/publication, not exclusive EE cycles.
Reject this scoped post-SDK transcode implementation; no default promotion or
byte-size/padding tuning. Smaller commands did not produce net performance gain.
This is NOT a rejection of all compact-input/direct-producer formats, proof of
bus independence, or scene60fps ceiling. Do not treat recovering this candidate's
new overhead as a promised substantial gain. Native format/memory proof remains
useful for future work, but return to substantial existing EE preparation.
Emulator archive PASS921 hashes/305 inputs/exact fresh reader replay. Owned
physical10940 stopped after completed readbacks/full initial reader; final-log
reader and separate physical archive running. Devices remain ON, goal incomplete.
Input-price V2 physical archive
finishes1297 payload hashes/305 inputs and exact fresh archived emulator AND PS2
reader replay. Correctness-qualified archive contains a NET-REJECTED candidate,
not an accepted fix. No live owned build/emulator/archive/ps2client remains.
The native build cache currently contains this private input-price engine;
next baseline must rebuild/verify its own sources, not borrow that library.

## Existing EE Work: Effective Terrain Neighbour LOD (2026-10-12)

**V1 completed: correct, positive but minor; private, no default promotion.**
Fresh native source/mirror binding covers506 files, ELF
`069b0152d3d67a4a4b02814826135e6d39b6fbd2fbfbfdb51750fe2ae0c70b73`.
The same ELF completed emulator and one editor-equivalent network-reset PS2
attempt. Both gates pass54 exact images,3942 poses, identical moving capture/
env eligibility/draw/paint sequences, native SDK audits and owner lifetime gates.
Actual native full producer oracle passes every phase:20 starting resident
chunks,16 shadow-bearing chunks, all ownLOD1/2/4 (mask22),279612 bytes compared
exactly before priced recording. Five additional chunks become resident during
the drive. Thus local rebuilds93 consist of88 stale-neighbour budgets plus five
new residents, not93 stale-neighbour budgets. All six arms keep these budgets;
ON skips23 raw-only transitions and performs70 actual rebuilds, OFF93/skips0.
The other23 existing owners/streams/stamps stay untouched, not repacked/cached.

Physical both-order inclusive non-pacing net saved0.045193922/0.034259189ms.
Actual ON phases1/3/5 each59.387Hz with5 late presents per537 gas loops (538
presentation records). OFF phase0=59.169Hz/7; phases2/4=59.387Hz/5. Therefore
there is NO reproducible cadence improvement versus the settled controls, and
NO stable60. ON non-pacing means10.81..10.84ms versus OFF10.86..10.88ms;
p95 ON15.02..15.13ms versus OFF15.19..15.24ms. Local compiler elapsed totals
fall~54ms to~43ms per whole trip, including pre-price replacements/publication/
preemption, not exclusive EE cycles or all terrain producer work. Differences
from earlier ELFs cannot be added or used as another optimization gain.

This identifies genuine unnecessary EE work but rejects it as the substantial
remaining bottleneck. Do not tune this key further or turn24.7% fewer rebuilds
into24.7% faster frames. Daytime only, ordinary variable-dt/night and public
adoption remain unqualified; no preserved-scene incapacity proof exists.
Full physical report.
Sealed archive verifies1296 payload hashes/305 launch inputs, with exact fresh
copied emulator and PS2 reader replay. All owned build/test/client jobs stopped;
computer and console remain powered on.
The user pause checkpoint is now reached; no subsequent lead is started.

Preparation history: the requested console trial was prepared as private terrain-lod V1:
normal input encoding, accepted renderer/audio common in both arms, alternating
only effective-key no-op admission. A pre-price actual native producer oracle
rebuilds each resident chunk with equivalent raw neighbour keys and requires
exact base/shadow/optional stream bytes, bounds and material/package descriptors,
then restores the original key. Metadata skips consume the same budget slot.
At preparation time no target correctness, net gain or promotion was claimed.

Before this lead consulted A28 stable focus, terrain-normal fine tuning and
the cold compiler experiments. This is NOT another focus/normal/cache tweak:
updateTerrainChunks rebuilds a full chunk when any RAW neighbour LOD changes,
but actual edgeMix ignores neighbour detail<=own detail. Therefore some raw
metadata changes cannot affect edge interpolation or topology and trigger
otherwise redundant mesh plus complete-command regeneration. Own LOD changes
and coarser-neighbour changes remain real geometry changes. Scene boundaries
also ignore the absent neighbouring edge.
Read-only source audit closes prepareWorldOwners as an every-frame whole-scene
sweep suspect: its epoch/runtime/front guard returns before loops. Pool.collect
visits retired entries only, not every live owner. Existing register also
already rules out QBuffer::setMaxVertCount scratch allocation; don't repeat it.
New tools/tyrax2-terrain-lod-key.hpp expresses conservative effective-key equality.
Source-bound host runner extracts the ACTUAL edgeMix/hAtE/shadeAtE lambdas from
the qualified event-audio producer, tests all81 neighbour combinations against
each other for own1/2/4, nine boundary positions and full/partial extents.
Height/shade callbacks are synthetic stable values, not complete lighting.
No runtime omission or FPS claim before native whole-stream/side-effect proof.
Critical trial constraint: metadata-only no-op must consume the ORIGINAL build
budget slot and update all RAW neighbour fields. Simply continuing the scan
would advance real LOD changes earlier and alter the priced workload. Preserve
all material/ground-shadow passes and old immutable bank readers. Shading may
change independently of LOD; a guard/native oracle is mandatory before skipping
the full producer, particularly for lighting/probes/emission. Not implemented yet.
Source-bound effective-key host V1 now PASS optimized+ASan/UBSan:76243 admitted
pairs/100904 rejected pairs/36672883 actual-lambda point comparisons.74056
admitted pairs change raw neighbour metadata while exact edge descriptors,
interpolated height and shade remain identical under stable callbacks. Frozen
producer/helper/runner/binaries/logs/report:
F:/Projects/tyrax2-terrain-lod-key-host-20261012-v1/sha256.json.
These are synthetic combinatorial cases, NOT measured scene rebuild counts or
performance. Next audit actual shading dependencies and native full producer
equivalence before enabling budget-preserving metadata-only updates.
Actual shading audit narrows the safe trial scope: shadeOf reads authored
constexpr SCENE_LIGHT/AMBIENT/DIFFUSE/BRIGHTNESS, not live sun state.
g_scenePointLights is populated by collectScenePointLights at scene load;
the bound source contains no later mutation. Probe grids and emitter tables
are nullptr in all three fixture scenes; terrain tint variation is0 and
driving scenes0/1 have no layers/AO/emission companions. Ground shadow rows
are authored masks: membership depends on own LOD, positions on hAtE plus
0.02, not inactive-neighbour detail. Thus a conservative fixture guard can
reject probes/emission/layers/map-light routes and texture-load changes while
retaining base plus ground-shadow arrays. Still require ACTUAL native whole-
producer stream comparison (positions/colors/ST/shadow data and metadata),
not just synthetic callbacks, before a moving omission. No runtime fix yet.

## Partial Road Frontend / Queue Integration (2026-10-12)

Wheel-local V1 native build rejects wrong helper type name Tyra::StaPip; the
actual game member is Tyra::StaticPipeline. Preserve rejected V1/no target
deployment. Correct helper type and explicit clamp statements, then create
fresh V2. This compile failure is not consumer/performance evidence.
Fresh wheel V2 native503 binding PASS ELF6ff78953...c018e1ee, matching
libraryb9a1d075...b0c0e9da copied/hash checked. Hidden owned emulator now executes
this actual standard-matrix route; full wheel activation/exact rasters/moving
lifetimes and net remain unqualified. No PS2 deployment yet.
Owned wheel V2 emulator26/Xvfb15 is live with exact recorded args. First held
ON1 car/4 draws/1200 vertices, main image pixels equal both ordinary controls
(header stage IDs excluded as original raster gate requires). Phase3 moving
ON4498 cars/17992 draws/5397600 source vertices, no observed assertion. These
are activation/preliminary image facts, not full-series/physical price. Recheck
exact live process identity; wait same process until actual completion/error.
Wheel V2 FULL emulator PASS54 exact main/env/actual Hybrid2 images/3942 poses,
moving ON4498 car draws/17992 wheel draws/5397600 source vertices EACH phase;
held ON1 car/4 draws/1200 vertices, OFF0 new route. Input vertices are NOT
removed EE transforms: ordinary control already skips still rigs. No source
change was needed to pass numerical/light-bound image risks; no tolerance.
Completed exact owned26/15 stopped after DRIVEDONE/full gate. Emulator archive
running, physical same-ELF both-order net and real lifetime gates next.
Final wheel V2 emulator archive PASS908 hashes/305 inputs/exact archived-
reader replay. Fresh physical-v1 SAME ELF launched editor-network-reset with
console-output-received, owned ps2client5748 confirmed live/executable/args.
Keep this server until actual DRIVEDONE/full raw image/pose/wheel/net gate;
do not reset/restart on timeout. No live build/emulator/archive. Physical net
is pending; phase1/3/5 actual wheel activation and all gates still required.
Wheel V2 REAL PS2 FULL gate PASS54 exact images/3942 poses, moving/held four
local draws/car only ON, zero errors and unchanged common terrain/direct/paint/
Hybrid2/ordinary roads. Physical net+0.542574740/+0.494577658ms BOTH orders.
OFF57.07..57.17Hz with26/27 late intervals; ON57.58..57.68Hz with21/22 per537
gas rows. ON p95work15.74..16.04ms, mean11.39..11.44ms, still NOT60.
Candidate four draws also apply to PARKED cars; ordinary skips their vertex
bake, so extra heads can offset gains. This is not pure affine EE timing.
Completed exact owned5748 stopped after DRIVEDONE/full raw reads; devices
powered, physical archive running. Keep private: no night/variable-dt/default.
Next source cleanup is a distinct HYBRID producer: offload moving cars, keep
parked cars in existing still-rig/world-space batches. Both routes always draw
with current state, never omit still wheels; price head reduction with all
gates, not another cache knob. No hybrid implementation/net claim yet.
Final V2 physical archive PASS1284 hashes/305 inputs/exact emulator AND PS2
reader replays; no live owned job. Hybrid generator option --moving-only now
implemented: nonzero-speed cars use VU, zero-speed rigs remain in ordinary
current-state world batches (including any spin/suspension changes). Never
infer zero speed means no redraw. WHEELSTATIC witnesses require positive
ordinary parked batches during ON and fewer new car draws than all-car V2;
same full geometry/image/pose/net gates. Fresh V3 generation follows; no new
native/runtime/net proof yet. Frozen V2 source/evidence remains untouched.
Hybrid V3 native503 binding PASS ELF d828538b...d21cc384, matched copied
engine library b9a1d075...b0c0e9da (SAME as V2). Hidden owned emulator24/
Xvfb15 now runs this exact ELF. Actual mixed wheel activation/retained parked
batches/full images/lifetime/net pending; do not claim V3 faster or restart
on timeout. Recheck executable/args against V3 launch.json on next poll.
V3 actual moving phases1/3 show1815 VU car submissions/7260 wheel draws/
2178000 source vertices plus2683 retained ordinary parked batches, versus
V2 all-car4498/17992. Held ON still1 car/4 draws/1200. No observed runtime
error in this window; full pixel/pose/net gate still pending on same24/15.
Hybrid V3 FULL emulator gate PASS54 exact main/env/actual Hybrid2 images/
3942 poses, ON1815 new car submissions/7260 draws/2178000 vertices PLUS2683
ordinary retained batches per trip, held1/4/1200, OFF0 candidate. No numerical
tolerance or geometry omission. Completed exact owned24/Xvfb15 stopped after
DRIVEDONE/full mixed gate. Archive and physical-v1 input staging running;
same-ELF real PS2 correctness/net still required, no V3 gain/default/night yet.
Final V3 emulator archive PASS908 hashes/305 inputs/exact fresh archived-
reader replay. Physical-v1 SAME ELF launched editor-network-reset, console-
output-received, exact owned ps2client16216 live/executable/args rechecked.
Keep file server until actual completion/full reads; do not reset on timeout.
No live emulator/build/archive. Next poll this owned deployment, require full
mixed activation/retained parked/image/pose gates, then BOTH-order net/tails.
V3 REAL PS2 FULL gate PASS54 exact images/3942 poses, ON537 VU car submissions/
2148 draws/644400 vertices PLUS3961 retained ordinary batches each trip,
held1/4/1200, OFF0. Backend eligibility differs from emulator's exact speed
zero tests; both platform-specific geometry/pose gates pass. Net+0.511926580/
0.480730135ms BOTH orders. OFF56.97..57.07Hz/27..28 late intervals; ON57.68..
57.79Hz/20..21 per537 gas rows, p95work15.75..16.06ms. NOT60. No additional
gain over V2 is proved by comparing different ELFs; prefer hybrid for avoiding
gratuitous parked heads, not an invented extra delta. Stop wheel fine tuning.
Completed exact owned16216 stopped after full reads; physical archive PASS
1284 hashes/305 inputs/exact emulator+PS2 replays. No live owned job, devices ON.

Next distinct EE source lead, checked against register and sound.md: audio
worker priority0x5 calls Audio::work forever; work sleeps ONLY0.5ms then calls
song.work, which immediately returns if no PCM song is playing/loaded. Thus
idle audio worker can wake~2000/s even when vehicle sound is independent IOP
ADPCM. Existing song RPC starvation fix/pitch quantization already ship; do
not repeat those. Source confirms unnecessary idle scheduling, NOT elapsed
cost or cause of missed display deadlines. A bounded same-ELF trial may slow
ONLY empty PCM polling while preserving active streaming and every ADPCM voice,
common accepted wheel/terrain/paint/Hybrid2 workload. No per-bag clocks/cache
tuning; require actual empty-worker/unchanged active behavior witnesses and
full raster/pose/physical both-order net. Declare any song-start latency trade
before testing; no audio removal/default or 60-fps claim from source alone.
Private idle-audio prototype now changes ONLY empty PCM sleep from0.5 to16ms;
active PCM retains original sleep/work and ADPCM code untouched. Declare
possible song-start delay16ms BEFORE qualification; no public audio acceptance.
Local const timespec avoids new writes to Threading's shared mutable tv. Worker
mode/reset epochs drop cross-arm sleeping samples from witnesses. New reader
requires actual idle16/idleHalf branch activity, ZERO PCM, common accepted
hybrid wheels/direct+paint/Hybrid2/ordinary roads plus full pose/image/net.
Fresh fixture derives qualified wheel V3; no native/runtime/net result yet.
V1 generation complete. Before first build, mode publication order amended
to enabled=value THEN epoch++, so worker cannot observe a new epoch with OLD
mode. Generated header and tool helper match; no build inputs changed while
compiling. This is witness transition correctness, not a performance result.
V1 native504 binding PASS ELF193d6703...b1ab721, matched lib599c5d9d...
ee3f951d copied/hash checked. NOT deployed: wake counters would run inside
price and inflate frequent OFF polling. Preserve this unpriced V1; clean V2
disables worker observations in ALL MotionProbe sampled rows, drops old wakes
by epoch, and records PCM play START events (require global0) rather than
polling PCM for qualification in price. Active streaming/source semantics
remain; startup-latency caveat unchanged. No source edited during V1 build.
Clean V2 native504 binding PASS ELF80ff6894...4fda637a, matched librarya7e3fcb2...
cbb9b214 copied/hash checked. Actual hidden owned emulator26/Xvfb15 runs V2
with sample-gated witness increments and PCM play-start events. No target
correctness/net/PCM-audio qualification yet. Recheck executable/args against
V2 launch.json before polling/stopping; keep same job through observation
timeouts. V1 remains unpriced and undeployed, not a measured baseline.
Clean V2 FULL emulator gate PASS54 exact images/3942 poses, actual idle16
ON/idleHalf OFF moving+held, PCM work0/play starts0, common hybrid wheels
active in every arm with retained ordinary batches. Outside-price moving
wakes42849/41915/41916 OFF versus1330/1330/1329 ON; counts are NOT elapsed
EE cost and do not price emulator speed. All sampled rows have observer off.
Completed exact owned26/Xvfb15 stopped after DRIVEDONE/full gate. Archive and
physical-v1 input staging running; no physical result or active-music quality
claim. Declared song-start latency caveat remains, keep private.
Final clean audio V2 emulator archive PASS911 hashes/305 inputs/exact archived-
reader replay. Physical-v1 SAME ELF launched editor-network-reset, console-
output-received, exact owned ps2client3512 live/executable/args verified. Keep
server through actual completion/full raw reads; don't reset on timeout. No
live emulator/build/archive. Next physical full image/pose/idle worker/common
hybrid wheel gate and BOTH-order net/tails; no measured gain/active PCM claim.
Clean audio V2 REAL PS2 FULL gate PASS54 exact images/3942 poses, actual
idle16 ON/idleHalf OFF outside price, PCM0/play starts0 and common hybrid
wheels in all roles. Net+0.573406571/+0.491723422ms BOTH orders. OFF57.38..
57.48Hz with23/24 late intervals; ON59.061927Hz ALL three trips,8 late per537
gas rows, mean non-pacing10.94..10.96ms/p9515.45..15.49ms. NOT stable60.
This proves empty-worker scheduling affects this workload; not exclusive EE
cycles, general active music safety or scene-capacity ceiling. Completed exact
owned3512 stopped after all raw reads. Physical archive PASS1287 hashes/305
inputs/exact emulator AND PS2 reader replays. No live owned job/devices ON.

Existing CSV offline follow-up (no new target counters): phase1 eight late
entry offsets1325/1337/1362/1366/1370/1374/1413/1619. They follow previous
frame offsets1324/1336/1361/1365/1369/1373/1412/1618 with non-pacing16.71..
18.71ms, update1.04..1.94ms and render-minus-pacing15.07..17.66ms. Period
belongs to previous entry interval; never attribute it to same row's work.
These broad boundaries include downstream waits/preemption, not exclusive EE
attribution. Consult existing cold93 replacement/21-reader and A18/A44 history
before tracing those tails; don't add another per-bag clock carousel.
Before public audio adoption replace the16ms startup-latency trade with event-
driven empty-worker sleep/wake (PCM play and mode change), and qualify active
music/stop/resume/lifetime separately. That is correctness/architecture work,
not an assumed further FPS gain. No event implementation or tail cause yet.
Private event prototype now uses a single-token semaphore created BEFORE the
audio worker. Empty ON waits; PCM play publishes playing then notifies; mode
changes notify to exit candidate sleep. Early wake token is retained, duplicate
notifications coalesce. Active PCM original work/polling and ADPCM retained.
Host actual-header/mock-kernel gate PASS256 checks/64 rounds for early wake,
blocked wake, duplicates and mode exit; NOT actual EE kernel/PCM/audio proof.
New audio-event fixture/reader requires waiting=true AND real kernel waiter1
ON, legacy idleHalf OFF, zero PCM starts and common full render gates. No
16ms idle startup delay intended, but real music lifecycle/audio still must
qualify separately before public adoption. No native/runtime/net result yet.
Actual-header/mock-kernel256 checks also PASS ASan/UBSan. Original idle16 V2
full emulator verifier still passes after conditional audio-event reader change.
Fresh event V1 native504 binding PASS ELF97a2af00...ed01eb05b, matched library
d0ee6b67...7f64e9f4 copied/hash checked. Owned hidden26/Xvfb15 executes this
event ELF; full real-kernel waiter/image/pose/net gates still pending. No PCM
runtime/audio/lifecycle proof from host tests or empty fixture. No console job.
Event V1 FULL emulator gate PASS54 exact images/3942 poses, ALL moving/held ON
actual waiting1/kernel waiters1/idleHalf0, OFFwaiting0/waiters0/positive polling,
PCM starts0, common hybrid wheels/direct+paint/Hybrid2/ordinary roads. Actual
mode-change wakes complete both directions, not merely mock-kernel tests.
Completed exact owned26/Xvfb15 stopped after DRIVEDONE/full gate. Archive and
physical-v1 input staging running; no physical price or actual PCM quality
claim. Keep private, no assumed extra gain versus16ms or stable60 proof.
Final event V1 emulator archive PASS913 hashes/305 inputs/exact archived-reader
replay. Physical-v1 SAME ELF launched editor-network-reset, console-output-
received, exact owned ps2client33528 verified live/executable/args. Keep until
actual completion/full raw image/pose/kernel-waiter/common-render/net reads;
don't reset on timeout. No live build/emulator/archive. Physical net and actual
PCM/music lifecycle/audio acceptance remain pending; no default/60-fps claim.
Event V1 REAL PS2 FULL gate PASS54 exact images/3942 poses, actual kernel
waiter1/waiting1 ON, legacy polling OFF, both mode-change wakes, PCM starts0,
common accepted render workload. Net+0.571953298/+0.542820014ms BOTH orders.
OFF57.58..57.79Hz/20..22 late intervals; ON59.17..59.28Hz/6..7 per537 gas,
mean non-pacing10.89..10.92ms/p9515.25..15.40ms. NOT stable60. No additional
delta versus idle16 V2 is proved across different ELFs; event gate removes
periodic empty polling by construction but actual music lifecycle/quality is
still separate. Completed exact owned33528 stopped after full raw reads;
physical archive running, no device shutdown. Next actual PCM start/stop/
resume while worker is idle, plus returned kernel waiter; do not promote from
mock-kernel or PCM-inactive image tests. Remaining tails are not a scene ceiling.
Final event V1 physical archive PASS1289 hashes/305 inputs/exact fresh emulator
AND PS2 reader replays. No live owned job. PCM lifecycle next should use actual
AudioListener::onAudioTick feed progress, play/stop/resume and returned real
semaphore waiter. Original PCM WAV is game/res/sfx/engine_idle.wav; game/bin
and physical stage contain only sfx/engine_idle.adpcm. Stage a separate bound
WAV explicitly; never pass ADPCM into AudioSong. Feed callbacks prove actual
feed/work, not audible mix/quality; keep audio capture a separate verification.
PCM lifecycle component implemented privately: normal game init suspended,
explicit bound PCM WAV, real AudioListener feeds, THREE polling and THREE
event play/stop/resume rounds. Each needs>=3 actual feed callbacks, playing/
no event waiter during feed, then stopped with real waiter only in event mode.
Three-second bounded waits reject stalls; callback first-feed elapsed is a
lifecycle response bound, NOT renderer price/exclusive EE/audio quality.
Listener removed only after worker is back on actual idle semaphore. After
completion component main sleeps, no inherited price/world run. Fresh native
binding and explicit post-build WAV staging required; no runtime proof yet.
PCM lifecycle V1 native505 binding PASS ELF bf9361c6...6cc57e2f, matching
unchanged event engine library d0ee6b67...7f64e9f4. First binding attempt
with inline listener symbol rejected its additional guard-variable match;
unique AudioIdle::wake binds successfully, no source/build modification.
Explicit PCM WAV remains staged after native build. FULL emulator lifecycle
PASS six rounds/three actual feeds each, six starts, event rounds return to
real kernel waiter1 and resume successfully. This is feed/lifecycle evidence,
NOT audible quality, renderer performance or public audio acceptance.
SAME PCM lifecycle V1 ELF passes REAL PS2 six rounds/three feeds each/six
starts, real idle event waiter1 after every stop and successful following play.
Polling first-feed ticks251172..434754; event143501..145355. These are response
observations, not renderer price or general worst-case latency guarantees.
Legacy round1 logs an existing low-music-ring warning; audible quality remains
OPEN (no capture), so no public promotion from callback progress. Exact owned
ps2client32820 stopped after completion/readback; devices remain ON.
Component archive `tyrax2-world-stream-2026-10-11/pcm-life-v1-qualified`
contains520 hashed files, all505 bound sources, ELF/library/WAV and actual
emulator/physical logs, launch records and verifier. Both archived verifier
replays PASS. Emulator report resealed after replay against FINAL stopped log:
initial live report had an earlier log hash, not a failed lifecycle gate.
This component does NOT archive the unrelated306-input render workload or
qualify FPS/audio quality. Goal remains unmet; actual render price unchanged.

Existing event V1 CSV tail review (no new counters): ON phases1/3/5 have6/7/6
late gas intervals. Common late offsets1362/1366/1370/1374/1413 correspond to
previous WORK offsets1361/1365/1369/1373/1412, at identical poses. Work usually
16.714..17.990ms, update1.035..1.921ms; BUT phase5 offset1362 follows only
14.573ms non-pacing work (update1.028/render-minus-pacing13.546ms). Therefore
even this residual tail is not wholly explained by previous-frame non-pacing
work exceeding16.667ms. Inspect preserved entry/presentation semantics before
equating a late interval with EE capacity. Source has two terrain rebuilds per
frame and cadence-based reflection capture; neither is yet attributed to these
specific late rows. Do not claim four-frame pattern proves either source.

Offline entry/present reconciliation resolves the14.573ms apparent exception:
phase5 offset1360 costs18.858ms non-pacing/19.403ms whole; offset1361 then has
14.573ms work PLUS16.054ms pacing, and actual present interval33.368ms. The
entry interval30.636ms appears in offset1362, when presentation is already back
to16.683ms. Thus a sub-budget previous-row work value is NOT evidence of an
independent new pacing defect: the earlier overrun and deferred presentation
must be included. It is also not exclusive EE attribution/scene ceiling.
New offline `tools/tyrax2-entry-present-tails.py` binds sources/qualified ELF/
CSV hashes, requires one flip per contiguous gas loop,538 actual records for537
gas loops plus final drain, Hybrid2 slot1/non-ISR records and the source-matched
capture bracket. Actual presentation cadence confirms6/7/6 late intervals and
59.277738/59.168770/59.277739Hz in ON phases. No target code/counters changed.
Final analyzer requires the existing sealed physical archive: every consumed
source/ELF/report/deploy/CSV must match its sha256.json, not just a new hash
of possibly edited inputs. Replaying that immutable evidence reproduces the
live-fixture analysis exactly. No archived source/input was modified.
Complete tail contexts and input hashes.
Next EE source audit should examine terrain source construction during the
remaining93 LOD replacements, not repeat A39/A41 rejected presentation hooks.
`buildTerrainChunk` recomputes height gradients/normalization and directional
shading for emitted grid coordinates even though authored heights/light tables
are immutable. Dynamic probe/point/emissive contributions and edge interpolation
must remain current: immutable normal preprocessing is a possible distinct
producer representation, not a license to freeze full lighting or remove LOD.
No measured normal-preprocessing saving or candidate implementation yet.
Private terrain-normal V1 implemented after that audit: source-owned81x81
normal lattice (78732 bytes) computed with ORIGINAL native arithmetic once per
scene generation in BOTH roles. ON loads normal values; OFF executes original
gradient/sqrt/division. All later directional/probe/point/emissive shading and
edge interpolation remain unchanged, no new DMA references or frozen colors.
Generation/height-pointer/dimension/spacing changes rebuild the input lattice.
No per-vertex priced observers/clocks; one build-level activation witness.
Host actual-header key/index/storage checks PASS optimized warnings-as-errors
AND ASan/UBSan. Initial sanitizer invocation rejected PowerShell comma syntax;
quoted compiler flag succeeds. Host does not prove EE math/image equivalence.
Fresh V1 cloned qualified event baseline, common event audio/wheels/direct+paint/
Hybrid2/ordinary roads. Native build running; full raster/pose/lifetime/native
oracle and physical BOTH-order net remain unqualified. Modified full reader
still replays original event emulator gate PASS; no weakening original gates.
Terrain-normal V1 native505 binding PASS ELF374053cf...f848c104, matched
unchanged libraryd0ee6b67...7f64e9f4 copied/hash verified. Helper host conditional
synced into fixture BEFORE build; no build input changed during compilation.
Owned hidden emulator37/Xvfb26 executes exact ELF/recorded args. Actual native
lattice, full six-trip activation/images/poses/lifetimes remain pending; no
physical deployment or price yet. Revalidate same owned process on continuation,
never restart solely on observation timeout. No live build session/devices ON.
First emulator NORMALSOURCE validates all6561 native lattice samples/78732
bytes with exact equality; phase0 ordinary route begins successfully. This is
initial storage/native-arithmetic evidence, not full moving ON/raster/net proof.
Terrain normal phase0/1 emulator actual moving witnesses now show132 builds,
OFF0 prepared uses/ON132 uses,6561 native samples each generation. First
cross-phase endpoint main/env/display pixels are EXACT between reconstructed
OFF/ON worlds (not just mode flips over prebuilt meshes). Full reader now
requires all SIX phase endpoints exact for this variant in addition to54
original held rasters/3942 poses/old-reader/delivery gates. Full series pending;
no pixel tolerance added, no target sources edited. Archive includes variant
helper/generator/host check plus common event/wheel helpers. Stage physical
inputs independently, but do NOT deploy before full emulator qualification.
Terrain-normal V1 FULL emulator PASS54 exact held images/3942 poses AND all
six reconstructed endpoint main/env/display rasters. All moving modes have132
builds, OFF0 prepared uses/ON132,6561 native lattice audits; original93 local
replacements/21 old bank readers remain. No geometry/quality tolerance change.
Completed exact owned37/Xvfb26 stopped after DRIVEDONE/full gate, final log
requalified. Emulator archive PASS917 hashes/305 inputs/exact archived reader
replay. SAME ELF physical-v1 launched editor-network-reset, console-output-
received, exact owned ps2client28304 confirmed executable/args. Actual PS2 first
two phases finish valid with original native lattice/ON activation. Keep server
through full series/all raw reads; no reset on observation timeout. Full PS2
cross-phase image/lifetime/both-order price remains pending. Devices powered;
no live build/emulator/archive job, only this actual console deployment.
Current physical V1 reaches three valid phases, then last EE output is phase3
shadow-atlas loading and console repeatedly prints freepad DMA Busy. No
DRIVEDONE or six-phase physical qualification; no accepted normal-input gain.
No allocation/assertion root cause is shown in this output. Preserve partial
evidence/client; user-requested old-lab cleanup does not reset this console.
Partial physical-v1 frozen in terrain-normal-v1-physical-incomplete:346 payload
hashes, phases0/1/2 valid only, no DRIVEDONE/full price qualification. Owned
client28304 stopped after confirming the terminal IOP failure. Fresh physical-v2
editor-network reset produced only freepad DMA Busy, no EE startup markers;
stdout receipt is not game boot evidence. After a human-confirmed physical
reset, physical-v3 uses SAME ELF374053cf...f848c104 and305 fresh staged inputs,
ConsoleAlreadyReset (no network reset), exact owned client42040. Actual EE
startup, terrain and vehicle initialization and phase0 warmup are observed;
full six-trip qualification remains pending. Devices stay powered on.
Physical-v3 now FULL PS2 PASS: six657-row trips,54 held rasters/3942 poses,
six reconstructed main/env/display endpoints exact; native6561 sample audits,
OFF0/ON132 uses,93 replacements/21 old readers unchanged. Both-order net saves
0.050958764/0.042518362ms; ON59.387Hz and5 late intervals/537 gas rows in all
three ON phases. OFF59.169/59.387/59.169Hz with7/5/7 late. This is a minor
positive net, not stable60/capacity proof or public default qualification. Close
further normal fine tuning. Exact owned client42040 stopped after DRIVEDONE
and all full readbacks; stopped-log reader requalified. Devices remain powered.
Preliminary next code audit: ensureProgramSet already normally NOOP when
billboardsResident; existing source removes two VIF drains and15.5KB MPG per
billboard. Do not rediscover that shipped fix. Need actual oversized fallback
caller/pipeline switching evidence before treating microprogram swaps as lead.
Final physical archive terrain-normal-v1-physical-qualified PASS1289 payload
hashes/305 inputs/exact fresh emulator AND physical archived reader replays.
Actual PS2 log explicitly confirms12 programs, billboards resident. This closes
the proposed per-billboard fallback-swap explanation for this configuration,
not every possible pipeline transition. No new program-swap candidate deployed.
Next offline-only attribution extends the existing sealed entry/present-tail
reader to qualified terrain-normals (requires six native sources and exact
cross-phase rebuilt endpoints). Export existing update/render-minus-pacing and
route poses around real presentation misses. No new target timers/counters or
performance intervention; do not infer exclusive EE cycles from elapsed time.
Source owner reset retires through bank leases; raw submissions wait their own
sequence, so reset alone is not proof of a full-frame stall. No lifecycle change.
Sealed terrain-normal-tail-v1 offline replay now PASS all source/ELF/input pins.
Actual ON presentation59.3871Hz,5 late intervals each, same offsets1361/1365/
1369/1373/1412 in phases1/3/5. Four first misses follow actual probeCapture=1;
1412 follows capture=0. Preceding ON work is15.57..16.05ms for most such
captures, except phase5 offset1360=18.91ms; update~1.02..1.62ms. Miss loop
render-minus-pacing~15.45..16.57ms, whole-minus-pacing~16.80..17.65ms.
Do NOT equate the initiating preceding work with the later entry/presentation,
or conclude all misses are reflections/LOD/EE arithmetic. Elapsed scopes still
include old consumer waits/preemption. Original event six-phase actual Hz/late
results replay unchanged after reader extension. A39 ready hook already failed
net; this correlation does not justify rescuing it with poll tuning. Next
consumer-handoff audit must bind actual current Hybrid2 source/ownership and
separate ready old work from current preparation before choosing an intervention.
New source-level candidate to investigate (NOT implemented/qualified): segmented
native recording with one semantic early publication after env pass. Existing
flushRecording completes the old frame only when the new prefix is ready; the
usual single-prefix path therefore delays both old presentation and GPU start
until most new EE preparation is done. This is not A39 per-Core ready polling,
H01 frozen replay or C05 fused snapshot. Must preserve exact VIF/GIF words,
all geometry/views/targets, final FINISH once, unchanged two draw buffers.
Naively calling flushRecording is UNSAFE: writer still points at the submitted
storage; reset overwrites live DMA. Existing recordBarrier also flushes/waits/
resets whenever nativeInFlight is set. A valid prototype needs non-overlapping
monotonic writer windows in the same leased native bank and no automatic wait
for a previous window when appending an ordering barrier to a new window.
Retain bankSequences final ordered sequence for ALL earlier windows, immutable
borrow leases, prior-frame presentation callback exactly once, strict capacity/
END reservation and ordinary synchronous overflow fallback. No weakening fence
or blanket removal of waits. First qualify actual writer storage/sequence
contracts on host, then native+full emulator exact rasters/lifetime, then
same-ELF both-order PS2 net/tails. No deployment or savings claim yet.
NativeSegments storage prototype now implemented in tools/tyrax2-native-segments.hpp
against ACTUAL FrameVifWriter, no extra allocation/copy: monotonic disjoint
windows, seal with clean END, advance only after seal; whole bank reuse still
requires caller's completed queue fence. This helper does NOT prove queue leases
or protect against an arbitrary external reset of the exposed actual writer.
Host v1 compilation missing cstring corrected; preserved v2 optimized+ASan/UBSan
PASS3371 cases. Preserved v3 also appends actual packed AD GS FINISH only in
final window and confirms single write/canonical VIF stream equality. Only
introduced END-tag clean NOPs normalized, payload/commands not dropped. Every
later operation checks earlier sealed bytes; saturation/alignment/null/zero/
duplicate seal/advance/rewind and canaries covered. Frozen headers/helpers/logs/
binaries pinned in F:/Projects/tyrax2-native-segments-host-20261012-v3/sha256.json.
No runtime integration/native/target/FPS proof yet. Next integrate privately
from qualified event baseline (do not stack minor normals). Replace submit,
recordBarrier and recordGif overflow reset paths consistently: do not wait/reset
merely because a previous disjoint window is in flight, retain safe drain/full-
bank reset for exhaustion. Existing completeBeforeSubmission protection also
does NOT restore GS logical recording context after old-job presentation;
early callback must save/restore that context before continuing EE capture.
Preserve final bank sequence covering earlier ordered prefixes, prior-frame
callback exactly once, no mid-prefix stray FINISH. Actual queue/GS contracts
remain mandatory before target performance testing, not inferred from host.
Native-segment private integration now implemented in fixture generator: derives
qualified event baseline with event audio/wheels/direct+paint COMMON ON, one
prefix OFF versus semantic early env prefix ON, two buffers unchanged. All five
writer reset sites route through a fence-asserted reset helper; submit/barrier
avoid waiting only for a live DISJOINT window. Exhaustion retains flush/wait/
full native-bank reset, without resetting snapshot arena or removing REF leases.
Final ordered sequence still covers prior queued prefixes. Early callback saves/
restores GS logical recording context. Per-gas activation and held-only prior
window byte audit (not new clocks) added to full reader, original gates retained.
V1 generated before held audit completed; do not build/deploy it. V2 generates
from frozen generator/helper input hashes; source changes during generation now
fail explicitly. Actual native build/queue/GS/emulator/net remain UNQUALIFIED.
V2 native506 mirror/source/ELF binding PASS b8d3f049...a242edd6, pinned rebuilt
library1783e85b...a294c8a3; no build input changed during compilation. Modified
full reader replays prior terrain-normal emulator PASS. Exact owned hidden
emulator34/Xvfb23 launched same ELF. OFF phase0 completes537 gas frames with
zero early publications, held OFF audit windows1/protected0. First held ON
stops emitting before publication witness; read-only PINE phase0/offset1857,
enabled1/earlyPublishes0/nativePending0, nativeInFlight=running=completed171452.
No ASSERT/DRIVEDONE, no physical deployment or price. Process/socket confirmed
live; preserve same process for wait-site diagnosis, not timeout restart.
Screenshot handoff-stop.png and PINE status handoff-status.json in V2/emulator.
Owned isolated Xvfb F8 action attempted during diagnosis; status must establish
actual pause state before interpreting a SaveState. Snapshot observers can
drain VU/GS; CPU PC must be bracketed against paused status, not infer GS cause
from post-snapshot registers. Devices remain powered, no live build/job session.
V2 terminal cause now verified in game/bin/log.txt: TYRA_ASSERT "Native audit
publication missing" at mode header:35, quiet SleepThread (kernel idle CPU PC
0x81fc0 in paused exact-text SaveState141). This was not a DMA timeout or proved
GS FINISH wait. Emulator assertion output goes to host log.txt, not necessarily
emulator stdout: inspect BOTH before diagnosing a stalled sequence. PINE paused
status1 bracket/exact2.9.93 layout/text decoder PASS; initial F8 did not pause,
configured Space did. Scene's held reflection reuse can legitimately omit all
captures, so ON32 held renders need not publish an env prefix. V3 explicitly
invalidates shared env basis when NativeSegments::audit is true (last held
iteration BOTH modes only). Existing FastJoin checkpoint unchanged, no other
interleave/geometry switch, no priced capture change or weakened windows gate.
Stop exact owned failed34/23 after terminal proof; no PS2 attempt for V2.
Current user goal budget update: stop below20% REMAINING Codex usage, superseding
the historical60% remaining threshold. Objective/scope unchanged, devices ON.
V2 assertion confirmed before owned34/23 termination; paused SaveState141 remains
diagnostic evidence only. V3 fresh generation running with common held capture
and birth-pinned helpers. Archive writer now includes native-segment generator/
storage/mode/host checks plus common event/wheel helpers, without editing prior
sealed archives. No target qualification or physical performance claim yet.
Full reader now explicitly rejects quiet "Assertion failed!" in emulator host
log.txt before completion checks; archive already preserves/hashes that file.
No output schema/gate relaxation or change to any sealed historical report.
V3 native506 binding PASS ELF73f7c6e0...96883340, matched same V2 library1783e85b
...a294c8a3 copied/hash checked. Exact owned hidden emulator352/Xvfb341 started
with recorded args/ELF; common held capture/protection/full gates pending. Stage
305 physical inputs independently, do NOT deploy before full emulator gate.
No live build session; retain same owned process through full series or actual
terminal assertion. Prior V2 stopped only after host log proved quiet assert.
V3 actual moving ON first phase gasEarly185/537, OFF0; held ON windows2,
protected1/equal1 and OFF windows1. Native byte audit now activates correctly,
not silent no-op. First five phases have185 captures/599 paintVu each. Full
reader additionally requires EXACT per-gas capture/env-eligible/env-drawn/paintVu
sequence across all six phases and every ON capture's early publication, not
merely positive mode counters. No new clocks/target source edit for that gate.
Full final phase/image/pose/lifetime/net still pending on same352/341 process.
V3 FULL emulator PASS all six657-row trips/3942 poses/54 exact held rasters,
actual ON185 gas early publications/537 frames, OFF0; held ON windows2 with
protected1/equal1. Exact per-gas capture/env/paint workload sequence preserved,
original delivery/local93 replacements/old-reader gates retained. Owned352/341
stopped after DRIVEDONE/all readbacks, final stopped log requalified PASS.
Same ELF physical-v1 actual EE start via editor-network-reset, owned Windows
ps2client6388 executable/args revalidated. Console reaches phase0 warmup, no
freepad failure observed. Keep client through full six phases/readbacks, no
timeout restart; emulator archiver running separately, no physical net yet.
V3 emulator archive PASS920 hashes/305 inputs/exact fresh archived reader replay.
Physical-v1 FULL gate PASS six657-row trips/3942 poses/54 exact held images,
window audit,185 ON early publications, same capture/env/paint sequence and
old-reader/local rebuild gates. Net FAIL: forward-1.053506294/reverse-1.048074070ms
(non-pacing elapsed includes waits, not pure EE arithmetic). OFF~10.94..10.97ms,
ON~11.99..12.01ms; ON entry~50.53..50.69Hz versus OFF~59.06..59.17Hz.
ACTUAL presentation records538 each confirm ON100/100/98 late versus OFF8/7/7,
~50.53/50.53/50.69Hz. Entry interval late counts268/269/266 are NOT display
miss counts: semantic early publication shifts intra-loop timing. Full reader
now separately validates actual native-segment present records (slot1/ISR0,
one per gas loop plus final drain, bounded wrap-safe intervals) before FPS claim.
Reject this early-env-prefix policy, no default, no poll/split-point tuning.
Earlier CPU producer stop/wait worsens overlap; this is no capacity ceiling or
proof all possible segmented scheduling fails. Exact owned client6388 stopped
after DRIVEDONE/full reads; stopped-log full gate repeats PASS. Devices ON.
Full physical archive underway; leave active goal, stable60 remains unachieved.

2026-10-12 user-requested historical lab cleanup removes286741 SHA-confirmed
archived copies plus525 regenerable directories,5.245GiB logical payload.
6394 retained archive hashes verify; unique evidence/states remain. Historical
lab origin paths may no longer exist: use retained docs archives or cleanup CSV
mapping, never rewrite sealed provenance. Current October12 fixtures untouched.
See [cleanup record](tyrax2-lab-cleanup-2026-10-12.md).

Wheel VU prototype now implemented privately in tools: immutable local padded
source meshes, same rig matrices/suspension/spin/steer/visibility and FOUR
standard StaPip draws per admitted car. EE no longer multiplies every source
vertex in ON. Common cold source preparation BOTH arms and drain before scene
source release. Derives accepted direct+paint baseline (ordinary roads BOTH),
not cross-ELF road price; all existing still-rig/trig/strip control fixes remain.
New same-ELF wheels-vu reader requires moving/held activation and four draws
per admitted car plus inherited full pose/image gates. Standard VU MVP instead
of EE-world-then-VP changes arithmetic order; per-wheel light selection differs
from old definition-batch bounds. These risks are declared BEFORE execution;
exact raster gate remains, no retrospective tolerance or night/default claim.
Native/GPU/net not yet qualified. Source-local arrays remain owned until a
drain at existing wheel batch scene reset; no transient source DMA pointers.

Next V9 pressure gate: at existing offset1100 outside priced rows, after actual
partial flush, require a positive ROAD immutableSpanReady=false before
replacement. Reject one deliberately stale content key, restore it, replace
the producer attachment through original prepareRoadPackages, then require
old reader still held, old bytes unchanged and fresh distinct storage with
exact equal bytes/readiness. Never dereference retired metadata; only the
positively bank-held old geometry survives replacement. ROADLIFE required in
ON phases1/3/5 before each LOGWINDOWBEGIN, not substituted by terrain readers.
Native/runtime/images/net for V9 remain unqualified until actually executed.
V9 native506 binding PASS ELFbbb1273d...76ebb4bd, matched library1a44dbfe...
4f744695 copied/hash checked. Conditional verifier still accepts stopped V8
full evidence. Owned hidden emulator34/Xvfb23 now runs V9; actual lifetime
witnesses/full raster/net still pending. Revalidate executable/args before
polling/stopping; do not restart on observation timeout or old recorded PIDs.
V9 first actual ROADLIFE phase1 PASS at emulator78.02s BEFORE its price begin
82.29s: positive road old bank reader, replacement/distinct exact fresh bytes,
old bytes retained and stale content rejection. This is representative positive
replacement evidence, not full-series/runtime/physical qualification yet.
Owned34/23 continues; full phases3/5 and exact raster/pose gate pending.
V9 subsequently FULL emulator PASS54 exact images/3942 poses, positive
ROADLIFE1/3/5 outside each price window; whole/partial/wire gates remain PASS.
Old road bank-held bytes survive attachment replacement, fresh distinct bytes
match exactly and stale content is rejected. This is representative actual
road replacement, not exhaustive OOM/night/production lifetime qualification.
Completed owned34/Xvfb23 stopped after actual DRIVEDONE/full lifetime reader,
exact executable/args checked. Archive creation running; no physical price yet.
Final V9 archive `road-prepared-v9-emulator-qualified` PASS918 hashes/305
inputs/exact archived-reader replay; stopped-log replay also PASS. No live
emulator/archive. Fresh physical-v1 input staging now running for SAME ELF;
no live ps2client at staging start. Use editor-equivalent reset/listener/start,
not ConsoleAlreadyReset without fresh human confirmation. Physical result
and 60 fps remain unproven; preserve full correctness/lifetime/net gates.
Physical-v1 now launched SAME ELF by editor-equivalent network reset/listener/
execee procedure. `editor-deploy.json` reports console-output-received; exact
owned ps2client11296 executable/args rechecked live, console actively loads
vehicle assets. Keep that client until completion/full raw reads; do not send
another reset or treat timeout as failure. Physical gate/net pending, no FPS
claim. No emulator/build/archive session remains. Next poll this specific
owned deployment, qualify full images/poses/ROADLIFE and both-order net/tails,
then archive same-ELF physical evidence before any new experiment.
V9 REAL PS2 FULL gate PASS3942 exact poses/54 exact main/env/Hybrid2 images,
ROADLIFE1/3/5 positive outside price, early whole/partial/wire gates and zero
failed/unready. NET gain is SMALL both orders: forward0.131657906ms,
reverse0.125368361ms. OFF55.98..56.17Hz, ON56.47..56.67Hz; candidate p95
non-pacing16.79..16.86ms,31/32/33 late intervals per537 gas rows. NOT60.
This validates the private combined partial frontend/early whole replay under
this workload, not a large EE bottleneck or public/default/night qualification.
Both roles allocate/prepare road owners; cross-ELF baseline changes are not net
evidence. Completed owned11296 stopped after exact identity/completion/full
raw gate; devices powered. Physical-qualified archive running now. Do not
continue tweaking this road frontend hoping for missing milliseconds.
Final `road-prepared-v9-physical-qualified` archive PASS1294 hashes/305
inputs/exact fresh emulator AND physical reader replays. No live owned job.

Next source lead (checked against the external-critique row and
`wheel-rebake-skip.md`): moving wheel vertices STILL get world-space affine
transforms on EE. Existing fix already skips still rigs, hoists trig/bases and
uses strips; do not reintroduce those as missing. Current code builds a
maximum-size per-wheel block across normal/fast models and transforms padding
too. Distinct architectural candidate is immutable local wheel geometry plus
per-wheel live matrix on VU, retaining suspension/steer/spin/LOD/palette and
visibility. Must price increased per-wheel heads/draws; historical1.970ms is
Motor District BEFORE existing fix, NOT current Vehicle Playground savings.
No new clock counters: native/moving geometry/raster/lifetime qualification,
then same-ELF both-order net/tails. This lead is source-confirmed, not a proved
dominant bottleneck. Keep qualified road experiment private and avoid changing
its frozen evidence while building the distinct wheel fixture.

Resumed follow-up: remove duplicate whole-inside frontend by admitting the
EXISTING whole baked replay at the early road entry, with readiness checked
before texture acquisition/head emission. Preserve original residency/repeat
submission batching, live material and O(1) replay; never replace whole bags
with per-package REF jobs. Missing whole replay falls back before emission.
ROADWHOLE witnesses separate successful early replay from fallback and account
for every inside admission. This integrates the A35-known mechanism with V6
partial routing; it does not claim A35 alone saves significant time. The new
manifest-qualified reader requires positive moving whole replay; older V6
evidence remains readable unchanged. Native/runtime/lifetime/net validation
for this follow-up is not yet established; do not promote or claim 60 fps.

V7 generation completed before its new manifest flag was finalized and was
not built/deployed. Fresh V8 generation/native build PASS506 bound inputs,
ELF36fd4fdc...8cd8047a, libraryf044ed97...273bdb4 copied/hash checked.
Original V6 full emulator verifier still PASS after conditional reader update.
Owned hidden emulator368/Xvfb357 runs V8; identity must be rechecked before
polling/stopping (PIDs are not ownership proof). New full raster/route result,
specific road old-bank-reader pressure and physical net/tails remain pending.
V8 preliminary actual moving phase1: inside36345, whole replay36307,
whole fallback38; held ON4/4 early whole replay, zero fallback. First actual
ROADWIRE count72 still passes source writer/transport equality. No observed
invalid stream in this window. This proves activation, not full images or net
gain; series remains live and must be polled by exact owned identity.
V8 FULL emulator gate PASS54 exact main/env/actual Hybrid2 images and3942
poses, all three moving ON arms36307 early whole replays/38 fallback each;
held ON4/4, OFF0. Positive partial/wire gates unchanged; producer failed0,
unready0. Serial DRIVEDONE valid=1 and full reader verified before stopping
exact owned368/Xvfb357 (actual executable/args checked). No live target job.
Archive now being produced; road-specific old-bank pressure and physical
both-order net are NEXT, not waived by this correctness result.
Final V8 archive `road-prepared-v8-emulator-qualified` PASS918 hashes/305
inputs/exact fresh archived-reader replay, with stopped-log replay also PASS.
No live owned build/archive/emulator job remains. Goal is still unmet:
next implement positive road-old-reader replacement pressure outside price,
then qualify preserved image/route gates before physical both-order pricing.

After the cold-plan and actual SDK split gates below, implement a PRIVATE
producer attachment for stripped road chunks. Early Core entry handles
genuinely PARTIAL bags with original coarse8/merged bounds and C/TC route,
mask, source ranges/parity. Whole-inside/whole-guard bags KEEP ordinary O(1)
whole replay; unsupported/list/empty-selection bags stay ordinary. This avoids
regressing the already-cheap whole route while removing the generic frontend
for partial road work, not changing admitted geometry or forcing all-clip.

`tyrax2-road-prepared.hpp` owns plan, SDK metadata and bounded WorldOwner arena.
Generator/snippets wire source preparation, live texture/material/light/fog
head, original qbuffer slot acquisition and ORIGINAL half/full/end flushes.
New per-buffer writer emits three live header Qwords, immutable geometry REF,
actual selected SDK kick. Pending packet records owner submissions at ACTUAL
flush, not each job. Owner metadata is cleared on slot acquire AND after
emission; never walk old qbuffer slots for owner pointers (can dangle after
scene rebuild). No frozen TEX0, no per-job flush, no borrowed DMA sources.

Common direct+paint/Hybrid2 both arms, roads OFF/ON/OFF then ON/OFF/ON. Both
roles cold-prepare the new road owners; skip unused A31 road producer ONLY when
ready, ordinary fallback otherwise. This is common cold/memory preparation,
not untouched production startup pricing. Snapshot key includes original
program/primitive/depth/source stamps/model/buffer limits; primitive changes
must fall back before a head. Mutable selection scratch is NEVER DMA-referenced.
Audit at offset1100 (outside priced rows) compares actual original packager
routes/masks/ranges. Positive partial emission and ZERO unready/failed mandatory;
full same-ELF image/pose/native gates precede physical price. Queue-bank lifetime
pressure remains a separate required gate; inherited terrain old readers are
NOT positive road-old-reader proof.

V1 generator stops before build on wrong placement of WorldMoving::begin:
that hook is in game/terrain_game.cpp, not engine.cpp. Incomplete fixture
preserved; corrected fresh V2 generation running. No native/GPU/PS2 gain yet.
V2 generation completes; native compile stops on SDK helper's host/engine
include selection in Core TU (only renderer TU formerly defined
WORLD_CELL_ENGINE). No target run. Scope that macro around SDK inclusion in
the new runtime header, restoring caller state; add explicit cstdio include.
Preserve failed V2 build; fresh V3 generation running. Not a performance result.
V3 generation completes. Native-width deduction is explicit in the plan/budget
min calls (PS2 uint32_t differs from host unsigned). Fresh host V6 original-
packager gate PASS same764/15280/253861/4584 optimized AND ASan/UBSan. Reserve
EXACT suffix Qwords before compilation to avoid vector-capacity amplification
of the bounded pool. This last reserve update applied to V3 before build and
matches current renderer snippet; no build inputs edited while compiling.
V3 native build running; no target/runtime/physical price yet.
V3 native506-source/ELF agreement PASS,92774429...615c1443; matching engine
library139d453b...76729e28 copied/hash checked, current core/renderer snippets
match generated source. Hidden owned29/Xvfb18 now runs this ELF. This is the
first actual GPU-use trial of the new route, not the earlier SDK-only gate.
Full delivery/image/route/lifetime gates pending; no emulator timing claim.
V3 first OFF trip has47 compiled stripped-road owners/failed0/unready0 and
positive original-package audit. First held ON arm stops on ACTUAL SDK
`packet2_chain_close_tag` alignment assertion BEFORE qualifying new GPU output.
TTE open_cnt advances only8 bytes; emitter omitted two tag VIF slots before
copying payload. packet2_add_data counts Qwords (not bytes), so its count1 was
correct. Explicit two NOPs fill each CNT tag's VIF half. No topology/shader
change. Owned29/18 identity checked/stopped after actual assertion; V3 raw log/
ELF/binding/library preserved. Corrected fresh V4 generation running. No PS2
deploy or timing result; do not blame the later freepad DMA Busy as root cause.
V4 native506 PASS5dae4cfe...3e97fee2, matched libc9c21e09...d9097d02.
Its first OFF trip still47 owners/failed0/unready0; held ON no longer hits CNT
alignment, but produces ACTUAL Unknown VifCmd warnings (float-looking payload
words treated as commands). NOT qualified or a performance result. Owned
29/18 exact identity checked/stopped on this explicit invalid-stream evidence,
not merely a timeout. Preserve V4 log/ELF/library. Replace u128 typed copies of
std::array Quad heads/tails with memcpy/advance (avoids type-punning); that is
a suspected cause, NOT proved yet. Add independent actual-writer/transport
oracle before GPU submission in the next candidate, rather than price it.
Fresh V5 generation completes; native build running. First actual emitted
fragment now additionally reconstructs current ordinary SDK writer and checks
HEAD+owned suffix+kick equality, then flattens the actual new CNT/REF transport
against that independent stream with ONLY the three explicit wrapper NOP
Qwords. Runs once in phase0 held arm before any ON price window; reader pins
this placement/positive ROADWIRE. This does not yet prove the suspected
type-punning cause, queue snapshot correctness or road lifetime safety.
V5 native506 PASS8a3245bb...e1b8dca6; matching library9085a40f...1452b43f
copied/hash verified. Owned hidden29/18 runs this exact ELF. Actual ROADWIRE,
candidate GPU/raster and road-old-reader gates remain pending; no hardware
deployment or performance interpretation of these transport failures.
V5 oracle terminates on strict flatten tag-flags/alignment rejection in first
held ON attempt; no ROADWIRE. This does NOT prove u128 aliasing or isolate
which flatten call failed. Its newly allocated ordinary reference was not
reset(true), unlike accepted SDK checker; fix that diagnostic omission. SDK
tag bitfield writes also leave reserved fields untouched on reused memory:
emit fully initialized known-size CNT words (QWC3/header, QWC1/kick, zero VIF
half), using original emitQword and memcpy/advance, like accepted object-head
path. Strict actual-vs-reference oracle remains. Owned V5 29/18 checked/stopped
after explicit decoder exception; preserve rejected V5. Fresh V6 generation
running; root V4 invalid-command cause still unproved, no hardware deployment.
V6 native506 PASS0be9b4d1...3f3193cee; matching library45d0658c...7a2cd85
copied/hash verified. Actual first ROADWIRE count72 PASS current ordinary SDK
bytes AND emitted CNT/REF stream. First held ON emits3 fragments/2 partial bags;
first moving ON has47 compiled owners/failed0/unready0,2668 partial bags/2782
emissions and positive live route audit. No unknown VIF commands or exception
in this preliminary window. Owned29/18 continues full image/pose series.
This accepts the combined transport correction provisionally, NOT isolated
u128 causality, full regression, lifetime pressure or physical gain.

Source/activation also exposes V6's intentional first-stage cost:36345 whole-
inside/whole-guard early checks RETURN to generic Core in first ON trip, so
they repeat part of the frontend although ordinary replay remains O(1).
Next integration should reuse original WHOLE baked replay through the same
early prepared frontend (A35-known mechanism), not turn those into many REF
jobs or price the duplicated frontend as the final design. This combination
with new partial jobs is distinct from claiming A35 was never tried. Consult
its tiny/negative net first; prove existing inside replay/readiness before any
head, retain batching/material/flush behavior. No whole-route change yet.
V6 full emulator gate PASS3942 exact pose rows/54 exact main/env/actual
Hybrid2 image files, actual prepared PARTIAL emission moving/held only ON,
all47 stripped-road owners per trip/failed0/unready0 and original live package
audit positive. Current actual writer/transport oracle PASS outside price.
Existing terrain direct+paint/common cleanup and live reflection workload stay.
Owned V6 29/18 checked/stopped after full serial DRIVEDONE and valid reader;
stopped-log reader PASS. No physical deployment/price or new road-old-reader
pressure proof. Archive/replay running. Next remove duplicated inside frontend
using original ready whole replay and add specific road bank-reader pressure,
then REAL PS2 both-order net/tails. Do not promote this intermediate V6 as60.
Final V6 emulator archive PASS918 hashes/305 launch inputs/exact fresh reader
replay. Helpers/snippets/current generator and actual bound source/ELF/library/
images/launch inputs retained. No running owned build/emulator/archive remains;
devices powered, goal ACTIVE. Earlier rejected fixtures remain unchanged.

## Cold Road Package Plan Component (2026-10-12)

Follow-up actual SDK split (private V1, UNQUALIFIED): new
`tyrax2-road-package-sdk.hpp` compiles original C/TC cull-strip/native-clip
writers into two actual standard heads, an owned immutable geometry suffix,
and actual MSCAL/MSCNT tails. Live mask rewrites ONLY the original packed field,
retaining mask0->all-six fallback. No geometry queueing or new shader. The
checker compares split reconstruction against the actual SDK writer for BOTH
state modes/all64 masks, C/TC/counts3,6,66,72/source offsets0,3/native parity0,1,
then independently decodes unpack destinations/data/primitive/header words.
Expected48 bodies/4128 byte comparisons/192 bad-binding controls/48 source-
mutation ownership witnesses. Native compilation/execution pending; not yet
actual output proof, a scene caller, leases or a measured fix. Source fixture
derives accepted paint-cell V1; check runs once in cold startup preparation.
Native source build running; preserve parent engine/geometry/ordinary roads.
V1 native compile fails on the checker's const Vec4 pointer assignments to
mutable bag/texture source fields. No target was launched. Preserve V1/build
log; corrected checker uses its actually mutable owned raw arrays in fresh V2.
This is harness source recovery, not renderer corruption or performance.
V2 native504-file binding PASS, ELFa7d881fc...c9beb5cd, matched library
80a4f67f...abb082f1 copied after build. Initial witness requested incomplete
function name; exact namespace `RoadPackageSdkCheck::done` links and binds.
Owned hidden34/Xvfb23 executes fresh ELF. Actual compiled EE SDK check PASS
48 bodies/4128 original-writer comparisons/192 bad-binding controls/48 owned
witnesses/all64 masks, independent source/unpack/header decoding. Reader8
malformed controls PASS. This is compiled EE execution under emulator, NOT
physical timing or new candidate GPU execution. Keep report preliminary while
log grows; full inherited six-trip image regression is running (new body is
NOT used for drawing). Parent price manifest copied solely for that regression;
do not price this SDK startup checker or call it a renderer optimization.
V2 full inherited emulator regression PASS3942 exact pose rows/54 exact main,
env and actual Hybrid2 image files. Existing paint/cell roles and ordinary roads
remain unchanged: these images do NOT execute the new prepared SDK geometry.
Owned34/23 identity checked/stopped after actual serial DRIVEDONE and successful
full reader. Initial stop guard required DRIVEDONE in game/log.txt as well;
that logger does not contain this serial completion. Guard refused safely;
confirmed live exact processes, used authoritative serial completion plus full
qualified report, then stopped. Both stopped-log qualifiers re-run PASS.
SDK archive/replays running; no live native build or target process remains.
Final SDK V2 archive completes901 hashes/305 launch inputs; exact fresh SDK
qualifier AND inherited world-price/image reader replays. Failed native V1
log/header/manifest preserved alongside successful source/ELF/library/raw
logs/config/images. No archive job remains; devices powered, goal ACTIVE.

Next queue integration must preserve ORIGINAL half/full-buffer flush cadence,
not flush each prepared job. Existing WorldOwner span registration/retirement
and old-bank readers are the starting lifetime mechanism; record submissions
at actual flushes (raw sequence and native bank reader meanings differ).
The geometry suffix has no TEX0/CLUT: keep original live per-bag material head
initially, rather than freezing texture addresses without a texture lease.
Cold compiler owns bytes, but neither fact proves publication or asynchronous
reader safety. This remains a substantially new partial producer route, not
the rejected all-clip road cell or a semantic-key cache inside generic Core.

New work follows the partial prepared-package contract below, after reviewing
C11/C18/A31/A32/A34/A35 and the rejected whole-road cells. Private
`tools/tyrax2-road-package-plan.hpp` computes camera-independent package jobs
at producer preparation: ORIGINAL merged one-third bounds, original coarse8
groups when run==capacity, whole cull ranges and A30 native clip ranges. Hot
selection keeps the actual outside/cull/guard/clip choices and eight-plane
guard test; dispatch tracks program continuation across admitted jobs, not
across rejected ones. This is not another whole-bounds A32 owner or an all-clip
cell. It has NO runtime caller, owned SDK body, texture lease or DMA lease yet.

`verify-road-package-plan.py OUT --engine FIXTURE/tyra/engine` checks source
hashes against the accepted paint-cell native binding, compiles UNCHANGED
actual packager create/checkFrustum and bbox merge/classification sources.
Host ABI declarations, tracing/debug and VU0 minmax leaf are explicit shims;
all probes are disabled. Original core/native-strip/packet-emitter sources are
also frozen as evidence for the independently expressed dispatch recipe, NOT
compiled or represented as actual SDK output. Test views translate AND rotate;
capacity/run/tails exercise full coarse groups and misaligned one-third merges.
Compare each route/mask/input range plus admitted program-start sequence.

V1 failed warnings-as-errors on signed comparison; V2 PASS axis views, V3 adds
rotation, V4 adds shortcut counterexamples. V5 freezes current route sources
and runner; optimized AND ASan/UBSan qualification PASS with the same counts
and shortcut counterexamples as V4. Previous completed V4 optimized AND
ASan/UBSan PASS764 plans/15280 views/253861 command recipes/4584 malformed
shape controls. All four routes execute.45961 near/far-only cases would be
misclassified if only six bits decided guard;221326 continuations differ from
always-start;137024 jobs differ from unconditional clipping. These are finite
HOST counterexamples, not measured VU stalls or real-scene frequencies.

Next implement actual SDK immutable geometry variants and tiny live header/
mask/kick emission, preserving existing bag/flush cadence and material state.
Retain owner generations and old queue-bank readers on rebuild; cannot queue
this plan's borrowed producer pointers. Actual native SDK equivalence, moving
image/lifetime gates and same-ELF real PS2 net/tails remain OPEN. No FPS gain,
production/default change, completion or60fps ceiling claim from this component.

Final V5 archive includes original bound route/emitter sources and current
runner; no live component build remains. Devices untouched/powered; goal ACTIVE.

## Partial Prepared-Package Contract Audit (2026-10-12)

Consulted C11/C18 retained partial/refinement, A31 owned geometry, A32 bounds-
only failure, A34 head regression, A35 early ticket and rejected all-clip road
cells BEFORE selecting a new prepared-package lead. Source is accepted paint-
cell V1, not sampler-modified or an old producer template.

Actual strip partial route first uses ordinary package bounds and guard-band
tests: outside rejects; inside/guard culls by pointer; genuinely partial uses
A30 native strips up to72 inputs with original run seams/parity. No strip
expansion on this active qualified A30 branch. Thus "EE expands every road
strip" is false. Ordinary packager is grow-only, not new[] per submit; two
renderSubpkgs vectors are reused. Do not propose these removed allocations.
Capacity is pinned to the producer run, capped by actual CULL program capacity.
Classification groups eight packages conservatively, then tests partial groups
against exact merged one-third bounds. Tighter freshly computed boxes would
change routing; a new owner must preserve these original boxes/index intervals.

A35 cold ticket owns only whole bounds/whole cull body. Genuinely partial bags
fall back to generic Core; it does not implement prepared per-package execution.
Rejected road cells sent every accepted chunk through full six-plane clipping.
Proposed genuinely different boundary: producer-owned package jobs retaining
original outside/cull/guard/clip decisions and ordered actual C/TC SDK geometry,
with tiny mutable view/clip-mask/program-kick state and owned bytes/bank readers.
No whole-chunk all-clip, near-only shortcut, inline-template semantic-key cache
or frozen texture address without bounded core/CLUT leases. Exact baseline
MSCAL/MSCNT, primitive/strip parity, material/wrap/TFX and flush/lifetime policy
must be specified before implementation; don't casually add a flush per job.

Required next qualification: actual baseline/candidate package decisions and
input topology/ordered source equality in camera motion, independently decoded
SDK streams, stale/epoch/rebuild/bank negatives, same-ELF full main/env/Hybrid2
images, THEN physical net/tails on common direct+paint. Not implemented or
priced yet. Source audit closes false strip-expansion/allocation claims and
records this contract rather than silently repeating A35 or A44 road V1.

## Raw PC Refresh On Accepted 57Hz Baseline (2026-10-12)

Question: which remaining EE instruction paths dominate sampled non-wait
residency after direct-cold terrain and VU paint? Consulted A37 moving/calibration
and current paint-cell V1 first. This refresh has genuinely changed renderer/
producer work, not a renamed old snapshot experiment or a new per-bag clock.
Private fixture reuses byte-identical calibrated A37 adapter/store with bounded
768KiB storage, same SDK hashes. Both roles retain direct+paint/ordinary roads/
Hybrid2; moving OFF/997/OFF and1733/OFF/1733 quantify observer overhead and period
agreement. Held OFF/ON/OFF32 frames in each phase preserve exact image checks.
Sampler stops before any file export/readback or new scene reload.

Gates: new native binding, current-workload54 exact main-env-Hybrid2 images/
3942 poses and common paint/cell activation, all12 sample exports, positive ON/
zero OFF, no overflow/late callback, calibrated SDK identity and raw pc argument
(NOT callback CP0 EPC). Actual source/debug/deployed text equality, no alias
normalization, retain unknown/kernel bins and inline attribution. Emulator only
correctness, PS2 distribution/observer cost; histogram is NOT exclusive cycles,
saved milliseconds or a scene60 ceiling. Implementation pending generation/
native/targets; do not price a fix from the old A37 distribution.
Source inspection of the native warning also identifies an inherited held-
checkpoint harness issue: TYRA_ASSERT expands to a bare if, so
`if(candidate) TYRA_ASSERT(...); else {...}` binds else to the assertion, not
candidate. With all held arms cell-active it runs unused A31 preparation after
successful preparation, outside priced rows and BEFORE held sampler start.
Keep this fixture unchanged for matched observer comparison; record the actual
extra setup rather than claim it is absent. Fix with explicit braces in a
separate qualified harness change, never silently rewrite frozen evidence.
PC-refresh V1 native502-source/ELF agreement PASS,3a1876eb...20404af2;
matching libraryea7a6193...a1c27bff copied/hash-verified. Sampler headers were
already inherited but inert in parent; binding explicitly includes both, not a
missing dependency. Calibrated adapter/store byte-identical, SDK hashes exact.
Hidden26/Xvfb15 executes fresh ELF, full callback/image gate pending. No physical
distribution or observer-cost claim yet.
V1 full emulator gate PASS54 exact images/3942 poses/common direct+paint,
all12 PC exports21762 rows, OFF0/ON positive, no overflow/late callback,
exact calibrated SDK/adapter/store and9 malformed controls. Owned26/Xvfb15
checked/stopped after completion. Stage305 physical inputs complete; same-ELF
editor-equivalent deploy/archive running. Emulator histograms are not physical
performance evidence, and observer overhead/remaining code still unpriced.
Physical PC V1 receives output through exact editor reset/execee, owned12564/
ELF3a1876eb...20404af2. Same six-trip sampler/control run active, no manual
reset, no partial histogram claim. Emulator source/SDK/PC archive replay running.
PC-refresh V1 REAL PS2 full gate PASS54 exact images/3942 poses/common
direct+paint/Hybrid2,12 PC exports22599 rows/9 malformed controls, no overflow/
late callback, calibrated SDK/byte-identical adapter/store. Exact debug/deployed
.text equality and raw symbolization PASS. Moving distributions997/1733 agree:
VBlank graph_wait_vsync30.89/30.61..30.79%, copyQwords4.29/3.89..4.02%,
Core render4.09/4.00..4.32%, sendObjectData3.68/3.89..4.21%, bbox2.79/2.61..
2.81%, native-copy2.16/2.57..2.72%, immutable lookup2.25/1.97..2.05%. Work
remains distributed; no newly dominant single arithmetic/allocator function.
These are PC residency, NOT exclusive cycles, caller time or recoverable ms.
Observer adds~0.12ms in both controlled brackets; OFF returns~57.07..57.27Hz,
no stable60/ceiling proof. Old generic copy source cannot identify its producer;
do not repeat C05/H05 or optimize tiny memcpy assembly solely from this profile.
Owned12564 checked/stopped after completion/full raw/quality data read; devices
powered. Emulator archive926 hashes/305 inputs/exact price+PC replays verifies.
Physical raw/SDK/calibration/symbolizer archive freezing; next audit must inspect
actual residual Core/head/ownership call path, not add another per-bag timer.
Full physical PC archive completes1319 hashes/305 inputs; exact fresh price
and PC qualifier replays on BOTH platforms plus byte-identical physical
symbolizer replay. Adapter/calibration SDK and raw rows retained. Observer
added work0.115196ms/0.116004ms in controlled brackets; no sampler-as-fix claim.
No live owned build/emulator/archive/ps2client; devices stay powered, goal ACTIVE.

Residual source audit already rules out two tempting repeats: Core has exact
model/view-value transform reuse, and bbox cacher has bounded COUNT variants
explicitly preventing base/reflected-prefix thrash. Do not propose either as
missing. Whole/package bounds are still requested before whole-bag routing;
queue already uses counted owned snapshot and directly references owned inline
bytes. A35 tickets handle whole inside/guard and fall back on genuinely partial
bags, unlike a full producer-owned per-package frontend. Consult A32/A34/A35,
C11/C18 and rejected all-clip road cells before designing that boundary.
Raw PCs are function-qualified, but many addr2line locations are `??:?`; do not
claim precise source-line attribution or declare an allocator absent from
inlined/optimized work. No new prepared-package implementation/timing claim yet.

## Paint On Direct Cells / Hybrid2 (2026-10-12)

Question: does existing A13/A40 paint offload retain correctness and yield net
gain on accepted direct-cold terrain cells with actual Hybrid2 BOTH roles?
Consulted A08/A11 accepted-basis/readiness/fallback and A29 native paint images,
A13 integrated physical gain and A40's triple-only first order. This is a new
combination/second order, NOT a new paint discovery or sum of historical gains.

Source confirms built-in paint capability does NOT install raw overrides;
plain terrain has no normals-as-ST/paint request, so its Clip C/TC program and
whole-cell admission remain unchanged. Existing generator retains accepted
basis across mode switches, normal source/count/stamp invalidation and correct
EE materialization from the accepted basis on fallback. Private fixture changes
only paint request policy, leaving shader/writer arithmetic untouched. Both
roles force accepted directCold=true/skipA31/terrain cells, roads ordinary.
Actual2-buffer witnesses required; never import A40's Triple setting.

Moving activation uses existing paintVu CSV counts (sampled even with generic
observer off); no hot scoped timer/observer. Final held-frame head witness
requires supported paint, actual extra3QW per paint head and whole head-word
delta versus matched EE arms. Gates: native source binding, all6 matched day
trips/3942 poses/54 exact main-env-Hybrid2 images, positive paint only ON,
common terrain113 direct calls/other0,93 replacements/old readers and120 cold
body oracles outside price, zero fallback; then REAL PS2 both-order net/tails.
UNQUALIFIED implementation/generation pending. No normal-production/night/60
or default promotion from prior A40 or source compatibility alone.
Paint-cells V1 native502-source/ELF agreement PASS,7acc8394...877876f3,
matching library320186c8...ec3a249f copied/hash-verified. Actual SDK writer
normalization removes ONLY held-witness code and matches parent exactly;
all99 program files unchanged. Hidden26/Xvfb15 executes fresh ELF. Full moving
paint activation/held head delta/54-raster gate pending; no PS2 price yet.
V1 full emulator PASS3942 exact poses/54 main-env-actual Hybrid2 images,
positive existing moving paintVu only ON, held actual1 paint head16->19QW and
3QW paint payload per ON arm, EE0 paint words. Both roles retain113 direct
compiler calls/other0/20 cold body oracles,93 replacements/21 old readers,
zero fallback. Owned26/Xvfb15 checked/stopped after explicit DRIVEDONE. Stage305
physical inputs complete; editor-equivalent deploy and archive running. No
physical performance/normal variable-dt/night/default claim yet.
Physical-v1 editor network reset/execee receives output, owned23172, exact
7acc8394...877876f3; no manual reset required. Six-trip price running. Emulator
archive899 hashes/305 inputs/exact fresh PASS replay verifies; no hardware
timing inferred from emulator or incomplete first bracket.
Paint-cells V1 REAL PS2 full gate PASS:3942 exact poses/54 images, actual2
buffers every role,599 existing moving paintVu samples in each ON/0 OFF, actual
held head16->19QW/3QW per paint head, unchanged common direct terrain113 calls/
20 body oracles/93 replacements/21 old readers, zero fallback/live reflections.
Both-order NET PASS: forward0.887163ms/reverse0.871525ms. Candidate57.071..
57.172Hz versus controls54.741..55.210; p95 work16.365..16.736ms versus17.601..
17.673ms. Late intervals27/27/26 per537 gas rows: still NOT stable60. No pure EE
cycle attribution, historical-delta sum, normal variable-dt/night or public
default claim. Positive private combination; keep direct+paint as next private
baseline, ordinary roads/Hybrid2. Owned23172 stopped ONLY after completion/full
report read; devices powered. Full physical archive completes1275 hashes/305
inputs, exact fresh emulator AND PS2 reader replays. No running owned build,
emulator, archive or ps2client remains. No shutdown; goal ACTIVE/unmet.

Next evidence refresh to consider before another fix: current accepted direct+
paint/Hybrid2 workload is no longer A37's ~49Hz baseline. Reuse established
whole-EE interrupted-PC sampling (997/1733us, matched sampler-off controls),
not new per-bag clocks, to locate remaining non-wait instruction residency.
Consulted A37: old PC distributions are not current causal costs, sampling IRQ
mask/preemption bias and PC residency versus exclusive-cycle distinction remain.
Require matched moving workload/current rasters and quantify observer overhead;
do not use a histogram as a60fps ceiling or multiply percentages into "saved ms".
Fresh profiling is not implemented yet. Mean work~11.9ms alone does not establish
stable60: current p95/tails and pipeline/presentation dependencies still matter.

## Direct Cold Cell Construction (2026-10-12)

Question: can LOD replacement compile straight into final-owned VIF storage
instead of materializing copied Plan streams, SDK pass command vectors and a
second full body? Consulted accepted A44/skip-A31 and rejected complete road
cells first. This keeps roads ordinary, accepted terrain geometry/clipping/
texture leases and all original LOD replacement work. Not a generic queue/cache
rewrite, repeated A31 preparation removal or another clipping bypass.

Private `tyrax2-world-cell-direct.hpp` adds transient range partitioning with
the exact independent strip overlaps/parity and list capacities; direct SDK
adapter uses the ACTUAL existing program writer/MSCAL/MSCNT. CNT/REF append
copies each source payload straight into owned final storage, with total REF
budget and rollback on malformed input. Mutable producer arrays are borrowed
only synchronously during compilation, never queued to DMA. Original Plan
compiler and flatten code remain unchanged as independent byte/source oracles.

Component gate compares ranges/source bytes against owning Plan across all
capacities3..72/list/strip/textured/untextured, malformed streams and DMA tags,
budget rollback and owned output after source mutation. Native integration,
actual SDK output equality and same-ELF physical net/tail gates remain required;
no measured saving or60fps/default claim from host components.

Direct-cold component V1 PASS optimized warnings-as-errors AND ASan/UBSan:
840 exact range/source cases,12 malformed source controls against both paths,
14 malformed fragment/budget controls with prefix rollback, final-owned bytes
survive source mutation. Frozen8-file inventory at
`tyrax2-world-stream-2026-10-11/direct-cold-component-v1` includes independent
unchanged Plan/flatten oracles. This does NOT compile the SDK adapter or execute
VU/GS. Next integration derives accepted skip-A31 (NOT road-cell) and keeps
skipA31=true/terrain cells active BOTH roles, ordinary roads. Require actual
cold complete-body equality outside priced rows, identical native SDK writers,
positive93 replacements/old bank readers,54 exact images/3942 poses and physical
both-order net/tails. Do not charge discarded A31 work to the old control again.

Direct-cold V1 integration now implemented privately. Preserved materialized
function is renamed only; candidate retains identical binding/material-head
writers, plans byte length from transient ranges, reserves exact final size and
appends actual SDK fragments there. Complete opposite-compiler body equality
runs only at offset1100 (outside price), with temporary owner retired before
return; priced replacements invoke only the selected compiler. Both modes keep
terrain cells/skip-A31 active and roads ordinary. Endpoint compiler roles toggle
inside same-pose cell-active arms, with actual compiler-call witnesses. New
generator/reader/native/emulator/PS2 gates pending; no speed/default claim.
Initial direct V1 generation stopped on an include-anchor assertion before
native build: SDK is included transitively via world_cell_sdk_check, not a
standalone include in the renderer TU. Incomplete V1 preserved; corrected
generator creates fresh V2. This is generator recovery, not a PS2 failure or
performance result. No running native cache build was interrupted/restarted.
Direct V2 first native build fails on std::min deduction: PS2 uint32_t aliases
unsigned long, while host aliases unsigned int. Explicit uint32_t min type fixes
the adapter without changing range values. Failed build log preserved; helper
and fixture reconciled before a new build/binding, host component V2 rerun.
No target has run this candidate yet; not a runtime failure or performance claim.
Second V2 native build completes engine but game rejects early Cell include's
missing texwrap_t. Make the private Cell header self-contained with SDK
draw_sampling.h; reconcile generator/fixture, preserve second failed log before
retry. This is an include-order fix, not a change to encoded VIF or rendering.
V2 native501-source/ELF agreement PASS, cee24e8e...2ca5daf; matching library
e0e3f769...73d38c9 copied/hash-verified. Both compiler methods linked; old
materialized function remains text-identical after name normalization, Plan/
flatten/SDK and99 program files unchanged. Hidden26/Xvfb15 runs exact new ELF.
First phase full-body oracle passes and9 endpoint rasters exact; full six-trip
correctness gate pending, no hardware price/default claim.
V2 full emulator gate PASS:3942 exact poses/54 endpoint rasters,120 actual
complete-body byte oracles outside price, selected compiler113 calls/other0
per phase,93 replacements/21 old readers, zero fallback. Endpoint roles actually
invoke25 selected compiler calls each arm. Owned26/Xvfb15 checked/stopped after
explicit DRIVEDONE. Physical-v1 stage305 inputs complete; editor-equivalent
network deploy and emulator archive running. No PS2 net/default claim yet.
Physical V2 deploy actually receives output through editor-equivalent network
reset/execee, owned14172, exact cee24e8e...2ca5daf. Six-trip hardware run active;
no manual reset or performance inference from emulator localTicks.
Direct V2 REAL PS2 completes full correctness/activation:3942 exact poses/54
images/120 cold body oracles, selected113 calls/other0,93 replacements/21 old
readers, zero fallback; roads/live reflections/Auto/Hybrid2 retained. Both-order
NET PASS: forward0.464956ms/reverse0.399476ms saved non-pacing elapsed work.
Candidate55.022..55.210Hz, controls52.423..53.557; p95 work17.627..17.722ms
versus19.182..19.697. Late intervals48/48/46 per537 gas rows: still NOT60.
Local compiler boundary drops substantially (~2.81..2.85 to0.64..0.65ms per
replacement across93 calls), but includes pre-price work/preemption/publication
and is not exclusive EE cycles or total terrain baking. Do not sum old ELF
deltas or claim a scene60 ceiling. Positive private correction; no public
default/normal variable-dt/night qualification yet. Emulator archive898 hashes/
305 inputs/exact PASS replay verifies. Physical archive completes1274 hashes/
305 inputs/exact fresh emulator AND PS2 reader replay, including boundary scope
and two preserved failed build logs. Owned14172 checked/stopped after completion
and full evidence read; no running build/emulator/archive/ps2client remains.
No shutdown; goal stays ACTIVE. Use this direct-cold candidate as the next
private baseline, not the rejected road-cell route or old discarded A31 work.

Next combination to review (not yet implemented): existing A13/A40 VU paint on
this accepted direct-cell workload with actual Hybrid2 BOTH roles. Consulted
A13/A08/A11/A29 and A40 register entries: paint is NOT a new discovery, A40's
positive triple-only combination does not prove current double-buffer net or
compatibility with whole-cell program/texture epochs. First source-check paint
basis/representation lifecycle and plain terrain-cell admission, then current
same-ELF both-order correctness/net/tails. No adding historical gains or silently
switching to triple buffering; daytime60 and normal variable-dt remain unproven.

## External Proposal Follow-up: Terrain Pass Presence (2026-10-12)

### Next Experiment: Complete Road Cells

Question: does extending A44's owner-compiled frontend bypass to roads yield a
substantial net gain with unchanged roads/live reflections/Auto heavy ordering?
Consulted A02b/A25 and P01/P07 road work first. Unlike clipping omission,
road grouping, package memoization or retained tails, this bypasses generic Core
for an entire producer-owned road chunk. Existing road caller coarse checks and
submitHeavy/dripHeavy positions remain unchanged. Full six-plane SDK output and
independent strip seams remain; no new VU program or destructive clip shortcut.

Private `tools/tyrax2-world-road-cell-fixture.py` derives ONLY qualified skip-A31
emulator/physical evidence. Both roles retain accepted terrain cells and skip
discarded A31 terrain work. Candidate alone reserves road texture core/CLUT
slots and cold-compiles owned road bodies; ordinary road role does neither.
Stable heap Cell attachment survives ProcChunk vector moves; producer rewiring
and scene release revoke pointers/owners, old bank leases remain. Interleaved
objects overwrite VU uniforms, so every road restores live view state before
its cell; sharing one upload across the whole interleaved pass would be unsafe.

Gates: new bound native ELF, six same-ELF trips both orders,3942 matching pose
rows/54 exact main-env-actual Hybrid2 images, positive candidate road compile/
delivery and zero fallback, ordinary road zero candidate work, common terrain
cleanup unchanged. Physical net/tails decide gain; emulator only correctness.
Road V1 native500-source/ELF agreement PASS, ELF9187642e...ffaf119; matching
engine library0efa464e...17e8f02 copied/verified before any further build. Hidden
emulator34/Xvfb23 executes actual new ELF; first endpoint phase0 main/env/actual
Hybrid2 pixels are exact across all3 arms. Full six-trip gate still pending;
no physical price/default claim. Historical skip-A31 PS2 report replays
byte-identically with the extended reader.
Road V1 full emulator PASS3942 exact pose rows/54 exact endpoint images,
candidate66 road compilations/18109 road submissions each ON phase, zero
fallback; OFF0 candidate work. Same-pose endpoint cell counts3/10/3 or10/3/10
prove road activation there too. Owned34/Xvfb23 identity-checked/stopped only
after DRIVEDONE valid=1. Physical-v1 stage305 inputs complete; editor-equivalent
network reset/execee actually receives console output, owned40872, same ELF.
No manual reset or hardware timing result yet; six-trip physical run active.
Road V1 subsequently completes REAL PS2 full correctness/activation gate:
3942 exact pose rows/54 exact endpoint images,66 compilations/18109 road
submissions per ON phase, OFF0 candidate work, zero fallback, unchanged common
terrain skip-A31. NET FAIL in BOTH orders: forward saving-2.937708ms/reverse
-2.988562ms. Ordinary roads53.379..53.557Hz versus candidate42.520Hz; p95 work
19.181..19.361ms versus27.680..27.957ms, late intervals64/65/66 versus220 each
candidate per537 gas rows. Reject this complete-all-six-plane road route; keep
ordinary roads, no promotion/reverse rescue by changing geometry or quality.
The additional generic Core bypass does NOT establish a net saving. Candidate
also submits whole admitted chunks through full clipping and restores/flushes
shared VU state per interleaved road; those changed costs prevent exclusive EE
attribution. Do not call this a scene60fps ceiling, proof EE is fast, or failure
of all owner-built road architectures. No extra traffic timing was measured.
Emulator archive893 hashes/305 inputs/exact PASS replay verifies. Owned40872
stopped ONLY after completion and full PS2 report read; devices stay powered.
Physical source-bound net-rejected archive completes1269 hashes/305 inputs,
exact fresh emulator AND PS2 reader replays. `testValid=true` describes runtime
correctness, NOT successful performance: the saved net deltas are negative.
No live owned build/emulator/archive/ps2client remains; no device shutdown.
Next return to accepted
terrain local-rebuild/cold compiler path (93 replacements, high work tails), or
design VU packet admission explicitly before reopening roads; do not retry this
same all-chunk delivery with cache knobs. Do not infer time
from A36's road vector traffic fraction or promote defaults before all gates.

Question: can a single terrain position upload/transform serve base/layer/AO/
emission passes in the currently qualified day-driving workload? Consulted A02b
and A25 first: their shared-address audit already rejected blind sharing across
different subsets. New evidence is the actual A44 skip-A31 fixture configuration,
not a new claim that A02b proved every possible future multipass route absent.

Read-only `tools/tyrax2-terrain-pass-presence.py` verifies all500 bound sources,
the actual ELF ab1a91ea...622dc69b and qualified physical report/log hashes.
Generated SCENE_AO_MAP_OCCS and LITS are all0; terrain layers are0/0/1. Therefore
the driving scenes0/1 cannot create terrain AO/emission/layer companions. The
existing PS2 logs report20 cells/36 passes and25 cells/43 passes, consistent
with base plus ground shadows. Ground shadows explicitly bind gsVerts, a
shaded subset lifted0.02, not the base positions. No VU fusion was implemented
or priced: reject identical-position terrain multipass as the next fix for
THIS workload. This corrects the conversational suggestion that such passes
were the strongest next lead. Future layer maps and vehicle passes remain open.

Evidence: source/log audit.
Reproduce with `python3 tools/tyrax2-terrain-pass-presence.py
F:/Projects/tyrax2-world-cell-skip-a31-game-20261012-v1 OUT.json` on Windows,
or use `/mnt/f/Projects/...` paths under WSL. No new target run, timing saving,
public default or60fps ceiling follows. Next retain the previously identified
cold compiler copy boundary, or qualify road whole-cell admission separately;
do not add passes just to make a fusion experiment activate.

A44 ordinary-only four-view control now completes all3 phases with ZERO new
cell submissions. Native495-source/ELF6bc6b486...071233 PASS; image gate fails
the same fourth-direction transition. All36 byte-transition signatures match
candidate fixture,99/108 entire captures match;9 complete frames differ, so do
not claim universal candidate innocence. Control rejected archive937 hashes/
303 inputs/3 exact failing replays PASS as integrity/testValid=false. Emulator
30/Xvfb19 checked/stopped. View V2 adds32 ordinary settling frames per direction,
keeps exact acceptance and all workload; native495/ELFcc90a8a2...fafbb17 PASS,
hidden emulator completed all three phases and was identity-checked/stopped.
All three strict view gates FAIL despite additional settling: direction3 main
candidate differs from BOTH ordinary controls (658/650/692 bytes), while before
and return match; display differs286/284/282 bytes. Phase0 has254 changed pixels
at x216..443/y124..161. Extra warmup is not a fix; no moving/physical price.
SDK addStandardBufferDataToPacket explicitly sets prim.mapping from actual bag
texture, so a missing mapping assignment in the complete-cell caller is NOT
the diagnosed cause. Matched ordinary-only/32-settle control V2 is now being
built to distinguish candidate execution from periodic ordinary state; original
ordinary-only control had zero extra settling and does not settle this contrast.
Rejected V2 archive937 hashes/303 inputs/3 exact rejecting replays PASS as
integrity only, testValid=false. Matched control V2 native495 binding PASS,
ELF263ca23a...ac4c277; hidden emulator470/Xvfb459 started. No physical price.
Control comparison now rejects unmatched settling schedules, compares all3
arm pairs and derives reproduction from actual equality (old unconditional true
removed). Historical frozen readers remain unchanged; no relaxed raster gate.
Matched32-settle control V2 completes all3 view checkpoints with ZERO new cell
submissions. All108 complete captured main/env/display images match candidate
V2 exactly, including every failed within-run arm transition. This identifies
ordinary-route/test-state nonstationarity as sufficient for the observed V2
differences, not their source cause or universal candidate safety. The strict
held gate remains rejected; no moving/physical price. Full drive DRIVEDONE valid=1;
owned emulator470/Xvfb459 identity-checked/stopped, hardware unchanged.
Final stopped-log comparison view-control-v2-comparison.json confirms108/108
full rasters AND108/108 arm-pair transition signatures exact. Comparator negative
test rejects unmatched0/32 warmup; historical V1 rerun confirms99/108 full and
108/108 transitions. Next isolate ordinary source/state at affected foliage
(positions/ST/colours/MVP/material) before changing renderer or tolerances.
Matched control V2 rejected archive view-control-v2-rejected verifies937 hashes,
303 separate inputs and3 byte-identical failing reader replays. Archive integrity
PASS, runtime held testValid=false; no default, physical price or60fps claim.
A44 next same-lead diagnostic: bounded exact source snapshots at BOTH solo
object renderObjectBag and static-batch producer submission (models can bypass
solo path). Capture only final frame of each direction3 arm, preserve raw
positions/ST/colours/model/VP/material/fog/light inputs and stamps separately;
defer file write until completed frame.2MiB cap must fail closed, never truncate.
Source-only data excludes actual VIF and GS texture bytes; no timing/default claim.
Private source-game V2 native build completes. Exact TLV reader host decoding
and7 malformed controls PASS; runtime requires both solo/batch coverage and
108 exact images versus uninstrumented matched control. Stage excludes source
diagnostic bins. Native binding/GPU result pending; not an EE speedup.
Source V2 native496-file binding PASS, ELF7de49f45...50f12d2. Owned hidden
emulator24/Xvfb15 started; audit result pending. Original renderer/clipping,
roads, reflections and ordinary-only schedule remain unchanged.
First source phase captures31 records/404296 payload bytes per arm with exact
ALL fields (including stamps); its36 images equal uninstrumented control.
All9 captures subsequently produced within2MiB cap. Full observer audit pending.
Archive supports source-audit-baseline to replay raw inputs against frozen
matched control without waiving original view-image rejection.
Full source audit captures31 records/404296 payload bytes in EACH of9 stages;
all fields, including stamps, are identical across each phase's three arms.
BUT observer image qualification FAILS:105/108 exact; stages33/34/35 main each
differ2 bytes from baseline (display/env exact). Phases0/1 each36/36 exact,
phase2 33/36. This is not a waived two-byte tolerance or full observer acceptance.
Reader preserves rejected report before asserting; archive replays its exact
failure. Next matched SAME-ELF observer OFF/ON isolates capture vs build effect.
Full source drive completes DRIVEDONE valid=1; owned emulator24/Xvfb15 was
identity-checked/stopped. source-v2-rejected archive953 hashes/303 inputs/3 exact
rejecting view replays plus exact rejecting source replay PASS as integrity;
testValid=false/sourceAuditValid=false. Hardware stays powered; no physical
deployment, accepted renderer fix or60fps conclusion this step.
A44 source observer follow-through: runtime source-control.txt selects OFF/ON
without changing ELF. Both modes reserve the same2MiB at identical checkpoints;
OFF records zero, ON captures. Separate boots preserve the full ordinary schedule.
Switch fixture derives from rejected source V2, ON reuses completed OFF binary
and all bound sources. Reader requires same ELF, positive ON/zero OFF witnesses,
control-file hashes and exact108 images. Build-effect vs observer-effect only;
not a performance intervention or relaxed image criterion.
Source-switch OFF V3 native496-file binding PASS, ELF50d27d68...add92dfe;
owned hidden emulator26/Xvfb15 started. Extracted actual begin function passes
8 host I/O-shim cases/4 no-reread controls optimized+ASan/UBSan. Initial host
harness compile failed on POSIX index symbol collision; renamed readOffset.
This host gate is not target switch activation or image equivalence; runtime pending.
OFF V3 target switch REJECTS generated CRLF input (30 0d 0a), producing
valid=0 and no config witness. Identity-checked emulator stopped for this proved
invalid input, not an observation timeout. Generator now writes exactly1 byte
via write_bytes (platform-stable); recovery copies SAME built ELF/bound sources
with corrected OFF control, preserving original failed V3 log/input/manifest.
Corrected OFF V4 is a SAME-ELF reuse (50d27d68...add92dfe), no native rebuild;
owned hidden emulator135/Xvfb124 running. Host switch runner now includes CRLF
rejection:9 cases/4 no-reread controls optimized+ASan/UBSan PASS. Target pending.
Corrected OFF V4 actual config witness enabled=0/reserve2097152 and all9 zero-
record/valid stage witnesses PASS; all3 view checkpoints complete. Full drive
still running; ON clone will reuse exact ELF only after completion. No image
observer-equivalence conclusion before ON.
OFF V4 full DRIVEDONE valid=1; owned135/Xvfb124 identity-checked/stopped.
ON V4 clone binds same496 files/ELF50d27d68...add92dfe, byte1 control; owned
hidden emulator244/Xvfb232 started. No native rebuild. OFF strict held views
remain rejected; OFF/ON observer-equivalence result pending. GS source audit
confirms clearScreen already reasserts REPEAT after Hybrid copy, so missing
post-copy CLAMP reset is not an established new fix.
OFF V4 rejected archive945 hashes/304 inputs/3 exact rejecting view replays
PASS as integrity only. ON first phase config enabled1,31 records/404296 bytes
each; same-ELF OFF/ON first36 images only33 exact (main9/10/11 each6 bytes differ).
No observer acceptance. Cross-build ON V4 vs source V2 raw sources show different
model matrices for owners5/6/7/132/133 (e.g. owner5 translation differs~0.027 units),
while viewProjection matches. These initial scene-pose differences invalidate
assuming cross-boot source identity; OFF lacks raw sources, so they do not yet
diagnose THIS OFF/ON raster mismatch. Current within-phase captured inputs stay
identical; full ON audit pending, no tolerance/default/physical price.
ON V4 full drive completes; owned244/Xvfb232 identity-checked/stopped. Final
same-ELF observer gate FAILS96/108 exact ([33,33,30]); all9 raw captured arms
remain semantically equal. This does not establish capture causality without
matched scene state. Source audit finds updateFrameClock uses CP0 real dt for
the general world; automated vehicle INPUT is frame-scripted, not a fixed
physics step (stepVehicles receives g_frameScale/50 from that same real dt).
Next private V5 fixes GLOBAL simulation dt/scale to1/60 from first loop in BOTH
OFF/ON, retaining original CP0 clock update and real performance timers. This
controls scene pose, not renderer speed or a production physics change.
ON V4 rejected archive959 hashes/304 inputs/3 view replays plus exact source
failure replay PASS as integrity; testValid=false/sourceAuditValid=false.
Fixed-step OFF V5 native496-file binding PASS, ELF71cf0f87...a0f41804; owned
hidden emulator26/Xvfb15 running. This is the NEW V5 process identity, not old
V3/V2 pid26. Original clock call/Count sampling stays in source, but compiler
may eliminate overwritten dt arithmetic: no equal-clock-work or production FPS
claim. Real performance timers untouched; full same-ELF OFF/ON still pending.
Fixed-step OFF V5 actual fixed1/disabled-capture witnesses and all3 view
checkpoints complete; full inherited drive still running. Current monitor checks
exact live pid/cmdline and will stop owned process only after DRIVEDONE, clone
ON from SAME built ELF and start it hidden. A36 actual native-prefix capture/
FrameChainArena/Vif1ChainCheck already exists; use that verified parser/owned
closure boundary for further command audit, not a new ad hoc DMA interpretation.
OFF V5 full DRIVEDONE valid=1; owned26/Xvfb15 identity-checked/stopped. ON V5
clone preserves SAME496 source hashes/ELF71cf0f87...a0f41804, fixed-step flag and
one-byte1 control; owned hidden emulator116/Xvfb105 started. Original stationary
and OFF/ON observer image gates remain unchanged; actual results pending.
Fixed-step source V5 full OFF/ON observer gate PASS: SAME ELF496 sources,
all108 main/env/display images exact, all9 captures35 records each, EVERY
captured field (including stamps) equal within each phase. Owned ON116/Xvfb105
checked/stopped after DRIVEDONE. Original within-run stationary views still
FAIL on ordinary path; source observer PASS does NOT waive them or price A44.
Next native-prefix audit reuses actual A36 CommandTape/FrameChainArena/
Vif1ChainCheck, bounded2MiB persistent reserve,9 independently owned dumps.
Actual ONE prefix/submission and stable resident program must be qualified;
direct unrecorded VIF transfer rejected. No new parser, replay or hot clocks.
Source V5 observer-qualified archive958 hashes/304 inputs/3 exact rejecting
ordinary-view replays PLUS exact PASS source replay verifies. testValid=false
(stationary gate), sourceAuditValid=true (observer gate); do not conflate them.
Actual-command-source V1 native build completes, source binding/GPU pending.
Actual-command-source V1 native498-file binding PASS, ELF5823d2ae...4a4068c7;
actual new libtyra.a copied from matching native mirror and hash-checked.
Owned hidden emulator pending start result. New reader imports existing A36
flatten and compiles actual Vif1ChainCheck::Stream; nine owned closures must
validate, reject corrupt REF/truncation, preserve source bytes and108 exact
parent rasters. Delivered stream differences are reported, not waived or priced.
Command V1 first6 stages:548 VU calls/ONE prefix/submission,~1.697MB owned
closure and unchanged resident program. Middle root1138->1121 qwords (17 fewer
DMA tags), before==return flattened VIF bytes. Actual parsed command/payload
comparison distinguishes plain NOP padding from semantic changes; do not strip
zero payload values or infer EE cost from tags. Full observer gate pending.
Command V1 full observer gate PASS:9 owned closures/548 calls each/one prefix,
all source bytes and108 parent rasters exact. Before==return VIF bytes, middle
differs; first semantic header changes vertex count72->36 (not just zero NOPs).
Actual source shows INTERLEAVE_PASSES=1, auto ilAccount probes4 pairs/holds100
and switches draw order during held rendering. A18 ALREADY required pinning
Auto interleave for exact descriptor images; newer A44 held harness missed this
conditioning. Do not label it a new renderer corruption or reopen H09 pricing.
Next pin ONLY A44 held order (0 or2), restore at checkpoint exit, leave moving
Auto/probe/alpha gates unchanged. Avoid FastJoin::checkpoint alias: it also
invalidates shared env basis and changes capture work. No production fix yet.
Archive --command-audit-baseline freezes all9 actual closures/VU program/data/
VIF state dumps and requires exact fresh replay with actual bound parser against
the frozen source-qualified V5 baseline. Stationary rejection remains separate.
Held-order V1 native499-file binding PASS, ELFdb657653...c990819a; held mode2
only, moving Auto unchanged. Owned hidden emulator35/Xvfb24 running. No new
renderer default or net price; wait actual order witnesses/within-run raster
and command equivalence before promoting this harness correction.
Command-source V1 qualified archive1029 hashes/304 inputs/3 rejecting stationary
view replays PLUS exact PASS actual-command observer replay verifies. Code/
source/image preservation PASS, not a renderer FPS/default claim.
Held-order V1 first phase now PASS at unchanged stationary gate:36 exact
main/env/display images across ALL4 directions. Actual order witnesses mode2/
active1 all3 captured frames; root1121 qwords each/548 calls/program unchanged.
This supports A18-known Auto-order conditioning cause; other phases/full drive
and full corrected A44 cell route remain unqualified. No tolerance introduced.
Held-order phase0 actual parsed VIF streams are also EXACT equal across all3
arms (1688760 bytes,6168 non-NOP command records); not merely image equality.
Owned emulator35/Xvfb24 still running remaining phases/drive. Next finish this
conditioned baseline, then retest complete terrain-cell activation with SAME
simulation/order controls before moving same-ELF PS2 price. No new cache lead.
Next conditioned-cell fixture requires ALL3 ordinary view reports PASS with
9 actual order2 witnesses and completed drive, then changes ONLY held arm1 to
complete terrain-cell activation. Same fixed dt, held order, source/command
observers, shader/geometry/materials/roads/reflections retained. Positive cell
submission/no fallback and unchanged full main/env/display gates remain required.
Pinned ordinary control completes full drive; owned35/Xvfb24 checked/stopped.
ALL3 stationary view gates PASS,108 exact main/env/display rasters. New conditioned
cell V1 generated from bound successful control, native build running. Moving
candidate OFF, simulation/order preserved. Archive retains order/conditioned
manifests and raw command/source dumps when present; only declared reader
replays establish those respective gates, not inventory presence alone.

Conditioned-cell V1 native build completes and binds499 sources, ELF
b536f914...2180157. Matching native libtyra.a copied back and archive hash
verified. Owned hidden emulator34/Xvfb23 starts with that exact ELF; actual
four-view cell/image qualification remains pending. Pinned ordinary control
token comparisons now confirm exact1688760-byte VIF streams/6168 non-NOP
records across all arms in ALL3 phases. These token comparisons are partial
stream analysis, not an independent whole command-observer qualification.
No moving candidate activation or physical performance result yet.

Conditioned-cell V1 now PASS ALL3 unchanged strict four-direction gates:
108 exact main/env/actual Hybrid2 CT16 display captures across control/cell/
control arms; actual3/4/13/13 visible-cell admissions per phase, zero fallback,
25 compiled cells/43 passes/19 reserved textures/39104 words. Full inherited
six-trip drive completes DRIVEDONE valid=1; owned34/Xvfb23 identity-checked
and stopped. New cell route activates ONLY in daytime held checkpoints,
not moving/night trips. Final stopped-log reader reruns PASS all3 phases.
Ordinary held-order archive verifies1028 hashes/304 inputs/3 exact successful
reader replays; raw command/source inventory does not requalify observers.
Conditioned successful archive is being frozen. No physical price/default.
Next same lead: activate cells throughout daytime driving, prepare outside
recording after terrain completion, preserve roads/live reflections/Auto moving
order, and qualify per-frame delivery, rebuild/retirement/texture ownership.
Keep cold compile/reservation costs separately visible; identical common
reservation in same-ELF dispatch A/B isolates EE routing, but does not alone
price the whole residency-policy change. Night currently rejects dynamic
lights/spot, so do not claim this backend fixes nighttime preparation.

Inherited actual native-command witnesses at direction3 are positive for all9
stages: one prefix/submission, stable resident program/no MPG, within2MiB cap.
Controls have1121 root qwords/548 calls/1696928 closure bytes; complete-cell
arms846 qwords/625 calls/1722112 bytes. Smaller root is NOT smaller total
stream or measured EE saving: view-independent whole-cell delivery trades
additional VU work for less EE interpretation. Raw captures are retained;
previous command-observer baseline reader is not blindly applicable because
the new geometry dispatch intentionally differs. Real PS2 net price required.

Moving-boundary source audit: actual renderScene updates the resident terrain
ring after beginFrame (game_physics.gen.cpp:1755); buildTerrainChunk resets the
cell owner and marks completeTerrainDirty. prepareCompleteTerrain releases and
re-reserves the whole texture set through synchronizeFrame/align3D/align2D, so
calling it directly at that dirty render point is NOT a safe moving integration.
Plan explicit pre-record preparation/rebuild boundaries with unchanged streaming
budget/view selection, or a bounded deferred replacement/fallback contract whose
activation and cost are reported. Do not hide fallback as successful cell delivery.

Successful conditioned-cell archive now verifies1026 hashes/304 separate inputs
and3 byte-identical successful reader replays: conditioned-cell-v1-qualified.
This freezes the positive held daytime GPU/display gate only; raw command/source
inventory is retained without claiming additional independent observer replay.
No processes needed for this qualification remain running; PS2/computer stay ON.

A44 moving correctness fixture now generated PRIVATELY from conditioned V1:
same-ELF world-moving-control.txt exact-byte OFF/ON, global fixed step, forced
moving order2 in BOTH modes for image conditioning. Original in-recording
streaming remains; dirty whole-set compilation/reservation runs next loop
outside recording, common in BOTH arms. Per-phase activation/fallback/dirty
and cold prepare ticks exposed; sampled current-frame main/env/display
readbacks during gas. This deliberately does NOT claim production Auto order
or FPS. Native OFF build running; ON must reuse completed OFF ELF/sources.
New verify-world-cell-moving.py requires same bound ELF/sources, identical full
day driving pose rows, matching capture schedule, positive delivered cells/
zero fallback and exact main/env/display pixels. A rejected delivery/image
report is saved, not waived. Python syntax checks PASS only; runtime pending.

Moving OFF V1 first native build rejected missing WorldHeldOrder declaration
in generated world_moving.hpp; failed log preserved as build-missing-include.log.
Generator and generated header now explicitly include world_held_order.hpp.
First build confirmed terminal before starting repair build; no overlapping
native-cache jobs. Corrected native build pending. This is compile repair,
not moving GPU correctness or a renderer optimization result.

Corrected moving OFF V1 native build completes;500-source/mirror/ELF binding
PASS, ELF02d5c150...7083a26. Matching libtyra.a copied and hash verified.
No native jobs or emulator launched for this moving fixture remain running.
Next launch OFF hidden, complete/stabilize log, then clone ON --reuse-built
without a rebuild. Actual moving observer/readback qualification is still
unproven; do not send this diagnostic to PS2 as a performance build.

Moving OFF V1 owned hidden emulator26/Xvfb15 starts exact ELF02d5c150...7083a26.
Actual config disabled/fixedStep1/order2/deferred1; first pre-record compiler
prepares20 cells/36 passes/17 textures/34880 words (heap401408), unlike final
held25/43/19 set. First gas readback stage1408 completed with zero cell calls,
as required for OFF. Full drive and same-ELF ON still pending. Moving reader
extracted raster function passes positive and6 malformed/blank controls on host;
component evidence only, not full reader or target qualification.

Motion launch staging now excludes moving-*.bin diagnostic readbacks too;
the exact-byte world-moving-control.txt is retained as an authored launch
input. No physical deployment or performance claim follows from this change.

Moving OFF V1 runtime REJECTED at first display readback: target assertion
`A44 moving requires Hybrid2 display` in game/bin/log.txt; main/env captures
were complete but display absent. Source audit finds inherited A38 Engine::realLoop
switches3/2/3 buffer modes during each day/night trip; held checkpoints forced
2 only at completion. Stop is based on explicit assertion, not timeout; owned
26/Xvfb15 checked/stopped and waiter interrupted. Do not qualify OFF/ON from V1.
Generator V2 recovery preserves V1 and pins setDiagnosticTriple(false) at EVERY
phase boundary, asserts actual2 and emits A44BUFFER; reader requires all6
2-buffer witnesses. No waiver of display gate, no Hybrid3 solution/EE claim.
Recovered OFF V2 clone running, native rebuild required because engine changed.
Ignore prior log/drive/presentation outputs in future clones. Raw first failure
remains in V1. Native build/actual V2 activation pending.

Moving OFF V2 corrected native500-source agreement PASS, ELFefcd3cf3...71fdb63;
matching newly built libtyra.a copied and hash verified. Owned hidden34/Xvfb23
starts exact V2 ELF, not prior same-number conditioned processes. Removed stale
copied V1 log before start; whole drive/readbacks pending. Motion staging actual
preflight verifies305 inputs including world-moving-control and NO generated
moving/cell/source/command/tape readbacks. No PS2 deployment or performance claim.

Moving OFF V2 completes all3 daytime summaries: EACH757 tracked frames/537 gas,
50 whole-set preparations/49 dirty frames/4 moving capture stages, zero cell
requests as expected. Actual2-buffer phase witnesses positive. This reveals
the PRIVATE integration's whole-set rebuild amplification, NOT a production
Core bottleneck or PS2 timing claim. Old deferred full-set preparer remains
the current baseline; ON must establish actual per-frame delivery/fallback.
Useful next boundary if rejected: rebuild ONLY producer-invalidated cell bodies
with existing owner/retired-bank leases; preserve texture reservations when the
material set/address epoch is unchanged. Cold SDK compiler uses owned local
packets and copied primitive/local buffers, unlike actual submission; inspect
in-record publication/queue leases before binding producer replacement. New
texture-set changes still require an actual pre-record drain/reservation.
No such incremental binding implemented or priced yet. Full OFF drive pending.

OFF V2 full six-trip drive completes DRIVEDONE valid=1; owned34/Xvfb23 checked
and stopped. Actual2-buffer witnesses in all6 phases. Final stopped-log held
view readers ALL3 PASS/108 exact main/env/display images, despite moving common
preparation.12 moving stages/36 rasters produced; observer equivalence still
requires ON. ON V2 --reuse-built clone running, no native rebuild. Archive now
retains moving manifests/control/raw rasters/drive poses/log and reader when
present; mere inventory does NOT replay or qualify the moving observer gate.

ON V2 clone complete; exact same ELFefcd3cf3...71fdb63 and one-byte ON control
verified before launch. Owned hidden emulator41/Xvfb30 starts; no rebuild.
OFF held-qualified archive being frozen with12 moving stages retained as raw
evidence. Actual ON movement/delivery/images still pending; hardware unchanged.

ON V2 phase0 actual6561 requests/6536 cell submissions/25 fallback,50 prepares/
49 dirty frames; first12 current moving main/env/display rasters EXACT OFF/ON.
This is preliminary sampled image equivalence, not full zero-fallback delivery.
Strict moving gate must reject fallback; do not turn partial coverage into a
whole-cell or physical-speed claim. Finish ON to preserve complete evidence.
OFF archive attempt stopped at explicit stale303/304 input-count assertion:
new moving control is correctly the305th input. Incomplete archive retained;
helper now derives303+sourceSwitch+movingSwitch and keeps exact count checks.
Retry must use fresh destination; no valid archive/replay claimed for failed one.

Full ON V2 completes DRIVEDONE valid=1; owned41/Xvfb30 checked/stopped. Strict
same-ELF moving reader confirms500 identical sources/1971 exact day pose rows,
12 matched capture stages and ALL36 moving main/env/display rasters EXACT.
BUT delivery gate REJECTS: each phase25 fallback (6561/6560/6561 requests,
6536/6535/6536 submitted). Saved world-cell-moving-emulator.json valid=false,
deliveryValid=false; no tolerance or partial-coverage promotion. Each phase
50 whole-set preparations/49 dirty frames remains. ALL3 final held readers
PASS/108 exact images independently. OFF successful held archive verifies1070
hashes/305 inputs/3 exact successful replays; moving raw inventory retained.
ON archive --moving-audit-off additionally verifies frozen OFF hashes and
requires fresh byte-identical replay of the explicit moving rejection; pending.

Next concrete producer fix contract: keep common bounded19-texture reservation
including existing groundShadowPins (borrow texCache entry, do not acquire per
frame). For unchanged texture/program/fog/depth epochs, cold-compile ONLY the
changed chunk after producer rebuild, using owned local SDK packets and bank
retirement/readers. Do not release texture slots or drain GS for ordinary LOD
replacement; registration itself only adds immutable metadata, while actual
queue publication remains required. Separate exceptional new texture-set
reservation at a pre-record boundary. Qualify positive49 rebuilds with old
pending readers, zero fallback, preserved same-frame env/main streaming order,
and repeat exact OFF/ON rasters before clean production-order PS2 net A/B.
This contract is source-audited, not yet implemented or priced. Goal remains unmet.

ON moving rejection archive now verifies1072 hashes/305 inputs/3 exact passing
held reader replays PLUS exact fresh moving rejection replay against frozen OFF.
Archive integrity=true/held testValid=true/movingAuditValid=false, explicitly
separate. No owned test/build/archive sessions remain running. Devices remain
ON, allowance78% remaining; no PS2 performance/default/60fps claim this run.

A44 incremental local producer prototype now implemented PRIVATELY. Initial
reservation includes already-retained groundShadowPins via borrowed texCache
entries, no per-frame acquisition. Cold local SDK compilation runs after ONLY
the original changed chunk producer; texture/program/fog/depth epochs guard it.
No whole-set GS drain/re-reservation for ordinary LOD, original in-record
streaming order/budget preserved. Old bodies retire through existing queue-bank
readers; actual immutableSpanReady=false records held old-reader replacements.
Whole-set epoch changes remain pre-record only/fail closed if unsupported.
New generator derives from completed V2 exact-raster/failed-delivery evidence;
OFF native build running. Same-ELF ON must reuse completed OFF. Reader now
requires one cold pre-record preparation, positive local rebuilds/held old
readers on ON, zero deferred dirty/fallback and exact pose/raster gates.
Python syntax checks only so far; no native/GPU/physical price or default claim.

Incremental OFF V1 native build completes;500-source/mirror agreement PASS,
ELF103f97b8...40e7506a and actual linked compileCompleteTerrainChunk witness.
Matching libtyra.a hash verified; owned26/Xvfb15 starts. Bound monitor waits
for explicit DRIVEDONE or target assertion, checks identities before stopping,
then qualifies held gates, clones ON from SAME completed ELF and runs original
moving reader. No timeout restart. Runtime/local replacement gate pending.

Incremental OFF V1 first3 day summaries each757 tracked/537 gas frames, ONE
whole-set preparation,93 local changed-chunk compilations, zero deferred dirty.
OFF old-reader witness0 as expected (cells not submitted in moving control).
This confirms new local producer execution, not real PS2 speed. Full OFF drive
and exact same-ELF ON/positive held-reader/zero-fallback/image gates pending.

Incremental OFF V1 full DRIVEDONE valid=1; owned26/Xvfb15 checked/stopped.
ALL3 final stopped-log held gates PASS/108 exact main/env/display images.
Same-ELF ON clone now running without native rebuild; OFF held archive freezing.
No ON moving/old-reader gate or physical performance result yet.

Incremental ON V1 same ELF103f97b8...40e7506a starts owned116/Xvfb105. First2
day phases actual6561/6560 requests ALL delivered, zero fallback/dirty; one
whole-set preparation and93 local replacements each. Actual held-old-reader
witness21 per phase, unlike OFF0. This is positive local lifecycle activation,
not a PS2 net price or full moving image gate. OFF held archive verifies1073
hashes/305 inputs/3 exact successful reader replays, raw moving data retained.

Incremental ON third day phase also6561/6561, zero fallback/dirty,1 whole
preparation/93 local replacements/21 held-old-reader replacements. First24
moving images are exactly OFF/ON; whole drive/final reader still pending.
Before physical NET pricing, do NOT use common cold cell compilation as the
ordinary baseline: OFF currently compiles unused cell bodies too and retires
old A31 bodies. Qualify a further SAME-ELF production-role correctness pair:
OFF retains ordinary A31 producer/residency with NO cell preparation; ON pays
its reservation/local compilation/retirement costs. Preserve workload/images,
then remove readbacks/held captures, retain normal Auto moving order and measure
both physical control/candidate/control and reverse. Dispatch-only common-slot
pricing can isolate a boundary but is NOT the final production NET price.

Incremental V1 SAME-ELF OFF/ON moving gate now PASS:500 bound sources/1971
exact day pose rows/12 stages/ALL36 exact current main/env/display rasters.
ALL6561/6560/6561 requested cells admitted, zero fallback/dirty in all3 phases.
Each phase one whole-set preparation/93 local producer replacements/21 actual
held-old-reader replacements ON (OFF0). Full6 trips DRIVEDONE valid=1 BOTH,
owned ON116/Xvfb105 checked/stopped; ALL3 final held gates PASS/108 exact images
on BOTH modes. This qualifies observed local replacement and image preservation,
not allocation-failure/all-view/night/physical performance/default. Positive
ON archive --moving-audit-off freezing with exact fresh moving reader replay.

Production-role correctness generator now added PRIVATELY: derives ONLY from
completed incremental PASS and positive held-reader evidence. New OFF skips
cell preparation/reservation entirely during tracked driving and retains its
ordinary A31 producer; ON pays candidate preparation itself. Same ELF, fixed
step/order2 and original readback schedule. Reader requires exact role config,
OFF0 cold/local preparations, ON1/positive local/old-reader plus existing pose/
zero-fallback/image gates. OFF clone running; native/GPU pending. This avoids
inflating net gain by charging unused candidate work to control. Not a PS2
performance build; clean normal Auto order/readback removal still follows.

Incremental positive ON archive completes:1075 hashes/305 inputs/3 exact held
reader replays PLUS exact fresh PASS moving replay against frozen OFF. This
freezes the representative local producer/old-reader/pixel qualification only.
Production-role OFF clone completed, native build running. No emulator active
or physical deployment; devices stay powered. Goal still not60 or a ceiling.

Production-role OFF native build completes and binds500 sources/linked local
compiler, ELF1db72761...0fc34cbb; matching libtyra.a verified. No native jobs,
emulator or archive work remains running. Next hidden OFF/ON same-ELF role
qualification (ordinary OFF0 versus candidate ON1/93/positive old readers),
then clean Auto-order hardware net pricing. Usage77% remaining, devices ON.
No default promotion, physical saving or60fps/ceiling claim.

Production-role OFF V1 owned26/Xvfb15 started exact ELF1db72761...0fc34cbb.
Actual day summaries show0 cell cold/local preparations and0 moving admissions,
as required; dirty flag is unused cell state on ordinary OFF, not a deferred
candidate rebuild. Full monitor awaits completion/held gates, clones exact ON
ELF and runs strict production-role reader. Hardware preflight: no live
ps2client processes, one ping192.168.100.150 returns false. No reset/deploy
attempt yet; ping alone does not establish terminal hardware failure.

Production-role OFF V1 full drive DRIVEDONE valid=1; owned26/Xvfb15 checked/
stopped, ALL3 final held gates PASS/108 exact images. Each day tracked OFF
preparation/local compilation0, normal A31 producer retained. Same-ELF ON
clone in progress; OFF archive freezing. Windows Ethernet is UP192.168.100.41/24,
matching console subnet; no adapter/IP change or reset attempted. Availability
still unconfirmed after failed single ping; this does not stop emulator work.

ON production-role starts owned113/Xvfb102 with SAME ELF1db72761...0fc34cbb.
Initial watcher exits on transient absent startup emulator.log after confirming
live executable; this is NOT target failure or permission to restart. Resume
same process with missing-log guard until actual completion/assertion. No second
emulator launched; source/ELF/control unchanged. Whole ON role gate pending.

Production-role V1 full SAME-ELF gate PASS:500 sources/1971 exact day poses,
ALL36 current main/env/display rasters exact, zero fallback. OFF has0 candidate
preparations/local replacements; ON1/93/21 held old readers each phase. ALL3
held gates independently PASS/108 exact images BOTH modes. ON113/Xvfb102
checked/stopped after DRIVEDONE; resumed watcher never restarted it. OFF archive
1075 hashes/305 inputs/3 exact held replays verifies; ON positive replay archive
freezing. This is fidelity/representative lifetime, NOT physical net price.

Clean net-price generator now private, gated on production-role PASS. Six
day trips modes0/1/0 then1/0/1, ordinary OFF intact, normal Auto moving order,
fixed simulation step/Hybrid2. No in-price GS readbacks or source/command capture
activation; endpoint32 held-order2 renders/readbacks only AFTER finishPending,
outside recorded rows. Local compilation during driving remains charged to ON;
cold reservation at1100 reported separately before samples1200/gas1320. No
night/ordinary variable-dt FPS claim. New price clone running, native/target
gates pending. Stage excludes world-price-*.bin generated endpoint captures.

Production-role positive archive verifies1077 hashes/305 inputs/3 exact held
replays PLUS exact PASS production-role moving replay against frozen OFF.
Clean price V1 native500-source/link witness PASS, ELF23622c7a...2e1f9370,
matching library verified. Owned hidden26/Xvfb15 starts exact price ELF; target
six-day modes/Auto/no-in-price-capture and endpoint image gates pending. No
hardware reset/deploy yet; failed ping availability remains unresolved.

Price V1 completed six daytime trips/Auto order, owned26/Xvfb15 checked/stopped,
but REJECTED by new price reader: unconditioned cross-phase endpoints differ,
and entire camera pose rows do not match across phases. Vehicle x/y/z/speed/
yaw endpoints match, camera shake differs; source g_vehShakeClock persists
across scene reload. This is not a proved cell rendering bug. Old ClipTrial
observer also remains active every128 frames, including reference packet
construction/memcmp; do not price that ordinary-only diagnostic as saved work.
No physical price from V1. Saved rejection, original generator/checkpoint
snapshots in V1/provenance before refinement. V2 generator preserves shake
amplitude/math but resets its phase at gasStart, disables ClipTrial observer,
and compares THREE endpoint roles at the SAME pose per phase after price rows.
Transitions use real pre-record drain/release/reservation and restore ordinary
A31 owners on OFF; no retrospectively relaxed raster gate. Native V2 running.
Reader initially mistook probeCapture for diagnostic capture; actual caller
is LIVE reflection capture. Corrected to require positive live capture per
phase and captureFrozen0, not disable reflections. Corrected V1 reject saved.

Price V2 native build completes;500-source/link witness PASS, ELF33e15dfe...
1b399db6, matching NEW libraryc145162d...81fe0ef copied and hash verified.
No owned build/emulator/archive sessions remain running. Next hidden V2
six-day qualification requires exact shake-conditioned poses, same-phase3-arm
main/env/Hybrid2 images, actual Auto/no hot observer/readback, positive LIVE
reflections/zero fallback. Only then editor-equivalent physical net/reverse
deploy. Console network availability unconfirmed; no reset attempt yet.
Usage77% remains. No hardware price/default/60fps or capacity-ceiling proof.

Clean Price V2 emulator gate now PASS: six day modes0/1/0 then1/0/1, exact
ALL3942 pose rows across phase schedules,54 same-phase endpoint main/env/
actual Hybrid2 images EXACT across3 arms each; actual Auto during driving,
ClipTrial observer0/no hot readbacks/source/command capture, positive LIVE
reflection captures and zero candidate fallback. Ordinary0 preparation versus
candidate1 cold/93 local replacements remains. Full DRIVEDONE valid=1;
owned26/Xvfb15 identity-checked/stopped, exact price ELF33e15dfe...1b399db6.
No emulator timing emitted as performance evidence. New price archive freezes
sources/library/inputs/drive/endpoints and requires exact fresh reader replay.
Archive and fresh physical-v1 staging running; editor-equivalent deploy next.

Physical-v1 stage305 inputs completed; editor-equivalent reset/listen/-t10/
10-second delay/hidden persistent execee actually receives console output,
despite failed ICMP/Unreachable neighbour earlier. Exact deployed ELF33e15dfe...
1b399db6; owned ps2client5288, no prior live clients. Hardware is running the
six-trip price test, no repeated reset. ICMP failure alone is NOT a ps2link
availability proof. Preserve server until actual completion/error evidence;
whole physical price and endpoint gates pending. No hardware FPS claim yet.

Price V2 REAL PS2 full gate PASS:6 matched day trips/3942 exact pose rows,
54 exact same-pose endpoint main/env/Hybrid2 captures, live reflections/roads,
Auto moving, clip observer0, zero candidate fallback, actual21 old readers per
ON phase. Both A/B orders NET positive: forward1.170333ms/reverse1.173160ms
non-pacing elapsed saving. Controls47.335..47.756Hz; candidates52.423..53.115Hz.
Candidate p95 non-pacing19.494..20.219ms and68/69/76 late nominal60 intervals
per537 gas rows: NOT stable60. Non-pacing is not exclusive EE; no scene ceiling.
Ordinary baseline0 cell prep, candidate1 cold/93 local replacements; startup
cold reported separately outside samples. Full DRIVEDONE valid=1, owned server
5288 identity-checked/stopped AFTER all evidence read, console/computer not
powered down. price-v2-qualified emulator archive891 hashes/305 inputs/exact
PASS replay and price-v1-rejected857/exact rejection replay verify. Full physical
archive now freezing with exact fresh PS2 reader replay, not just summary pins.

Next same-lead source audit finds concrete duplicate producer work: changed
chunk STILL calls prepareWorldGeometry on base/layers/AO/shadow/emission before
compileCompleteTerrainChunk immediately resets those A31 owners and cold-
compiles its whole cell. Skip these discarded A31 preparations ONLY when
completeTerrainSlots is active/qualified; ordinary OFF must retain them.
Keep previous bank retirement, geometry/pass/LOD/camera and local compiler.
Price against accepted whole-cell candidate in same ELF, not just baseline;
no assumed gain or default promotion before correctness/net/reverse qualification.

Full physical archive completes:1267 hashes/305 launch inputs, exact fresh
emulator PASS replay AND exact fresh REAL PS2 PASS/net-reader replay. Physical
data retained under price-v2-physical-qualified/physical. No owned build,
emulator, archive or ps2client processes remain. No shutdown requested/sent;
console remains in completed game's idle loop. Usage76% remaining, goal ACTIVE:
private net gain established, stable60/default integration and night still open.

Next duplicate-work test now implemented PRIVATELY: all6 price roles use the
accepted complete-cell route. Mode0 keeps old A31 producer work, mode1 skips
it ONLY with active completeTerrainSlots; inactive-slot startup/ordinary paths
still prepare A31. Original geometry/LOD/bank retirement/local compiler,
Auto/shake/observer0/endpoints retained. Actual A31 calls/successes counted
only in eligible tracked producer work, not unready startup; require old
positive/new0 before pricing. Same ELF old/new, not cross-ELF subtraction.
New skip-a31 clone running, native/GPU/physical gates pending. No predicted
saving or default change; next larger roads/night lead waits for this result.

Skip-A31 V1 native build completes. Generated fixture counter eligibility was
reconciled with latest generator before build: only ready-slot tracked producer
work counted, not initial unready startup. Legacy Price V2 REAL PS2 report
replays unchanged with the extended reader. New native binding/GPU pending;
no removed geometry or ordinary-path preparation, no performance claim.

Skip-A31 V1 native500-source/link agreement PASS, ELF ab1a91ea...622dc69b;
matching libraryc145162d...81fe0ef verified. Owned hidden26/Xvfb15 starts exact
new ELF (not prior reused pid identities). Monitor waits explicit completion/
assertion with checked stop, then runs variant-aware price reader. Old/new
eligible A31 success/zero, six matched poses,54 endpoint pixels, Auto/live
reflections/zero-fallback gates pending; no hardware net price yet.

Skip-A31 V1 emulator full gate PASS:3942 exact phase poses/54 exact same-pose
endpoints, both moving roles cell-active/zero fallback/live reflections/Auto.
Each old mode has372 eligible A31 calls/93 successful bodies; each new0/0.
Unready startup not counted or skipped. Local93 replacements/old readers
preserved. Owned26/Xvfb15 checked/stopped after completion. Fresh physical-v1
stage305 inputs complete; editor-equivalent deploy and emulator archive now
running. No performance saving inferred from eliminated calls.

Skip-A31 physical starts through exact editor procedure, owned ps2client35140/
ELFab1a91ea...622dc69b; no manual reset required. First3 phases complete with
zero fallback, old372 calls/93 successful A31 bodies versus new0/0. Full reverse,
same-pose pixels/poses and net report still pending. Emulator qualified archive
892 hashes/305 inputs/exact PASS replay verifies. Do not infer FPS from counts.

Skip-A31 REAL PS2 full gate PASS:6 matched poses/3942 rows/54 exact endpoint
rasters, live reflections/Auto/zero fallback, old372 calls/93 successful bodies
versus new0/0 each phase. Same-ELF old/new cell producer NET forward0.209699ms,
reverse0.250058ms. New53.557..53.826Hz, p95 non-pacing18.865..18.962ms and
61/62/64 late intervals per537 gas rows; NOT stable60. Small real cleanup on
accepted private cell route, not the remaining major bottleneck. Do not add
its delta to another ELF's1.17ms as a measured combined result. Full archive
1268 hashes/305 inputs/exact emulator AND PS2 reader replay verifies; owned
server35140 stopped after completion/read, no power-down or live jobs.

Next larger source boundary to audit: cold cell compiler at LOD replacement
copies producer streams into WorldCellPlan parts, SDK pass commands, then
complete body before publication. Preserve actual SDK output/all-six-plane
clipping/owned bank leases; consider direct final-owned construction instead
of repeated intermediate materialization. Do not borrow mutable producer
arrays. Source RULES OUT one false lead: setMaxVertCount only assigns a field;
fillByPointer does not allocate four arrays per part. No new compiler fix,
timing attribution or scene ceiling claimed. Goal ACTIVE, usage76% remaining.

A44 four-direction/display qualification now private in view-game-v1. Native
495-source binding PASS, ELF2e626e91...2025cb; three phases complete but image
qualification REJECTED: fourth direction changes254 pixels in phase0 (main658
bytes/display286), and candidate exactly equals ordinary RETURN. Other three
directions and env remain exact; actual admissions3/4/13/13 with no fallback.
This does NOT establish warm drift or candidate innocence: candidate may alter
persistent state. Next ordinary-only cloned gate isolates that hypothesis;
do not loosen raster acceptance or price V1. Additional
gate rotates only camera look around the unchanged end-of-drive position, keeps
producer/pass/geometry, and captures main/env/actual Hybrid2 display for all
four directions and control/candidate/control arms. Reader requires varying
positive visible-cell coverage, zero fallback, exact images and ordinary return.
No moving candidate/physical price yet; prior same-pose terrain evidence remains
preserved. Rejected view archive937 hashes/303 inputs/3 exact failing replays PASS
as archival integrity, testValid=false. Owned emulator35/Xvfb24 identities checked
and terminated. Ordinary-only control result and V2 follow-through are above.
Continue this source/view/display boundary, not a new cache lead.

A44 actual TERRAIN producer/submission fixture now private. V2 native495-source
binding PASS (ELF56492496...ba71c7), hidden emulator GPU gate PASS in three
daytime repetitions at the SAME end-of-drive pose:25 cells/43 passes/19 textures,
39104 words; actual three visible cells submit with zero fallback each repetition.
All18 main/env images are exact control/candidate/control matches. This is actual
new GPU execution, not just compiler execution; NOT broader camera/display,
moving lifetime/pressure, physical pricing or60fps. Complete-cell geometry
is decoded against every actual ordered source part before publication; final
candidate frame must submit every requested visible cell without fallback.
Full resident terrain texture set reserves at most75 percent of current heap,
leaving25 percent dynamic allowance. Old A31 terrain owners are retired rather
than retaining duplicate full bodies. Existing held gates remain unchanged and
new cell images are separate. Moving candidate stays OFF. Reader eight negative
witness controls pass; archive terrain-gpu-v2-repro844 hashes/303 separate inputs/
three exact reader replays PASS. Owned emulator26/Xvfb15 identities checked and
terminated; computer/PS2 unchanged. Next diverse view/main-env-DISPLAY gates,
then actual moving same-ELF physical price. See A44 terrain integration.

A44 complete-cell backend now integrated PRIVATELY into a copied qualified
texture-V2 engine; V2 full native build and495-file mirror/source/ELF binding
PASS (ELF6b7a1ab3...935d289). All three actual backend methods are linked.
No terrain caller/GPU/FPS result yet.
Compiler owns ordered material heads plus geometry, view entry supplies VP and
clip/fog state, cell submission bypasses Core/per-package classification. First
build failed on Attachment alias misuse and SDK integer template mismatch; V2
corrects these. Optional first CLAMP skip retains mandatory FLUSHE and actual
interior-span owner readers. Preserve pass TFX/light contracts and GS texture
fences. Next actual terrain producer/view restoration/full-set residency and
GPU images, not another generic cache. See A44 complete-cell backend section.

A44 real texture reservation V2 cold gates PASS: native494/ELF3235af3a...474b29,
hidden emulator actual2112-word slot, heap401408/budget200704/dynamic100352,
six rejected requests, both victim tiers, address rebind and GS-fenced epoch
release. SDK12/60/270/54 still PASS; slot reader eight negative witnesses PASS.
This is NOT full-world VRAM feasibility, sustained pressure or a GPU/FPS fix.
Source audit: FLASHLIGHT_USED=0; base dynLightPick=false/spotLit=true, layer
spotLit=true with default dynamic pick. Preserve those distinct light contracts;
one globally disabled-light lane is not an established equivalent path. Material
TEX0 uses pass TFX; compiled CLAMP must keep EE wrap tracking coherent on exit.
Owned emulator26/Xvfb15 checked/terminated, hardware unchanged. Continue complete
cell heads/view state/GPU submission with those constraints, not another cache.
world-texture-v2 archive820 hashes/both exact reader replays PASS. Dynamic
allowance is aggregate capacity, not contiguous-allocation/working-set proof;
qualify full-set pressure/fragmentation before fixed material-head execution.

A44 next dependency implemented PRIVATELY, cold qualified only: actual texture-manager
bounded world reservation, stable core/CLUT allocations excluded from BOTH
eviction tiers, epoch and outside-recording actual GS-fenced release. Existing
align3D INSIDE recording only records a barrier; it is not GS-consumer completion.
Cold one-texture gate now passes as recorded above. V1 fixture generator failed on
void/bool-only span helper for int pickVictim; V2 uses frozen function boundary.
This is a full-cell material-head dependency, NOT generic cache tuning, complete
world GPU submission, full-scene VRAM feasibility or a measured speedup.

A44 actual EE SDK compiler execution now PASS on hidden emulator:12 cells,
60 ordered C/TC list/strip passes,270 decoded parts and54 rejected bindings/
budget/stream cases. Native494-source/ELF binding passes; ELF8d5a5b5d...325f50.
Strict completion reader and seven malformed-witness controls pass. Owned
emulator6309/Xvfb6226 identities checked and terminated after this cold gate;
computer/PS2 unchanged. This is NOT new VU/GS-body execution, a raster gate,
whole-cell runtime submission, fixed residency or a speed result. Next submit
actual compiled cell commands with live view/material boundaries and consumer
leases; do not interpret inherited drive timing or this compiler gate as a fix.
cell-sdk-execution-v1 archive814 hashes verify,303 separate input snapshot and
exact copied-reader replay PASS. Current SDK output is geometry only: material
heads/view state/fixed slots and consumer leases must be bound before whole-cell
submission. Avoid simultaneous retention of old A31 bodies and new full-cell
source/body copies; actual RAM budget is part of the integration, not a speed claim.

A44 actual SDK cold compiler entry implemented PRIVATELY for ordered C/TC
list/strip parts, all-six-plane masks, actual QBuffer/program/MSCAL/MSCNT writers
and owned flattened payloads. List capacity retains sixfold output reserve;
72 native-strip inputs are NOT a safe list capacity. Target compile freezes288
included headers; this is not linked execution, geometry/raster or FPS proof.
Runtime caller, full view/material heads, fixed VRAM and DMA/GS leases remain
unbound. Continue actual emitter cold execution and whole-cell submission, not
another source-only model or generic Core replay. See the A44 SDK entry below.

A44 whole-cell source boundary implemented PRIVATELY: ordered textured/untextured
passes, list/independent-strip inputs, bounded partitions, copied material slot
identities, transactional cell rebuild and leases. Distinct intended runtime
route bypasses generic Core and per-package EE classification, unlike A35.
Host optimized+ASan/UBSan840 source cases/1680 failed rebuilds/16 negatives
PASS; real EE object compiles.18-file cell-source-v1 archive hashes verify.
The source model alone is NOT actual VIF compilation, VRAM reservation, shared-VP emission,
source-bank/DMA/GS integration, target execution or a performance result.
Next actual builder/SDK-emitter integration with fixed all-native six-plane
route, cell command ownership, live view boundaries and explicit texture budgets;
do not reinsert generic Core or claim the prototype itself is an EE fix.
See [whole-cell boundary](tyrax2-world-stream-2026-10-11.md#a44-whole-cell-source-boundary).

A43 V1 physical is REJECTED, not a qualified FPS result: all six trips and
restoration complete, but night CONTROL phase3 has outside=1. Job696033 starts
kind1 at a1=6, ends observed at a1=827/endAge=3/valid0; elapsed420208 Count
ticks (~1.425ms), beyond unchanged288*512 completion gate. The event cannot
distinguish late observation from actual late copy; do not waive or price it.
Candidate moving retention activates only ONE frame in day519 and ONE in
night469. This limited activation does not support a substantial moving fix.
Owned client42256 checked/stopped, hardware ON. Rejected archive998 hashes
verified; copied reader reproduces exact physical rejection and emulator report
byte-identically. --rejected-physical archives failure explicitly, never a green
physical report. A41 default safety remains unproven by successful prior series.

User-supplied architectural critique reviewed against current source/A18/A20/
A31/A35/A40 evidence. Useful distinct lead: owner-compiled world CELL command
path plus pass-scoped shared VP/uniforms, bypassing generic Core AND current
view-dependent per-package routing. A31 retained bodies and A35 strip-only
frontend admission are not this whole-world route; A35 did bypass frontend for
its admitted subset and failed net, so claiming every prior attempt cached only
the tail is false. Current Core already reuses consecutive model/view transforms.
Frozen replay totals are not a moving recoverable-EE ceiling; proposed5-8ms is
unmeasured. Fixed residency needs VRAM budget/owner epochs, not just removal of
LRU. Cache publication is still needed at compile/rebuild and for live uniforms.
Near-only clipping/scissor is not automatically safe for perspective/GS range,
strip parity, near/far crossings or companion passes. Dynamic wheel world bake
is present but must be priced against extra per-wheel draws, not assumed dominant.
Recommend a separate static terrain-only prototype retaining passes/LOD/geometry,
explicit cell lifetime and fixed bounded material set, with identical shader first.
Structural packet/routing equality is not its invariant; geometry/order/ownership
and image correctness remain invariants. Declare any numerical image differences
before qualification, not a retrospective tolerance to make a result pass.
No new world-cell compiler or measured saving implemented by this review.

A43 V1 complete emulator PASS:54 exact rasters,20 rejecting controls and
ownership-mask restoration; moving retained waits zero (GS already ready),
so no moving activation/performance conclusion. Native/linked qualification
precedes physical deployment. Owned369/358 identities verified/stopped;
clean303 inputs staged, editor-equivalent PS2 deployment in progress.

A43 V1 full native build PASS,496 mirror/source files and shipped
ELFfe8ccc35001ac343ff834b2c72027bf10302cff9124a907bae43909858cfd872.
Linked four-root/six-callback IRQ closure PASS. Component V10 archive37 hashes
verified. Hidden emulator369/Xvfb358 running this exact ELF; no PS2 result yet.

A43 source candidate begins: optional takeMain retainUntilReady returns WaitNative
while Armed and old native queue/GS FINISH/GIF idle are incomplete. It consumes
no FINISH and leaves IRQ claim eligible; once ready, main either observes an IRQ
copy or withdraws for unchanged fresh-field fallback. No ISR waiting, extra field,
budget reduction, per-bag hook or geometry change. Notifications and A41 copy ON
all arms; only retention toggles. Component V10 passes131 optimized/sanitized
guards plus644 actual packet cases/five EE IRQ entry checks. Initial V9 test
incorrectly expected two FINISH clears on fallback; actual fallback also clears
a stray latch before copy, so exact expected count is three. Failed evidence kept;
no protocol relaxation. Full native/linked/output/activation/PS2 gates pending.

A42 V1 physical full gates PASS:54 exact rasters,18 rejecting controls,
2943 moving copy completions, positive kind3 moving activation day2/night17,
zero late/outside/refusal/drop and final ownership-mask restoration. OFF/ON/OFF
notification copy-kick Hz day57.789437/57.789431/57.789380 (19 long intervals
each), night51.908696/52.346750/51.810958 (71/67/72 long intervals). Day net FAIL,
night net PASS but small/unreplicated; stable60 FAIL both. Copy median0.181956ms,
max0.205644ms. Do not promote notifications or call this the substantial fix.
native-ready-game-v1 archive998 hashes verified; copied readers replay BOTH
reports byte-identically. Completed client36644 identity checked/stopped; devices
remain ON. Next: qualify delaying main withdrawal until old native readiness,
without main stealing an already-claimed copy, clearing FINISH early, extending
physical budget or waiting inside IRQ. This is a source policy candidate,
not yet proven attribution of all remaining stalls. A42 reverse is untested.

A42 V1 full emulator reader PASS: same ELF1cdb92ca...86f147,496 native
files,54 exact main/env/display rasters,18 rejecting controls and final
DMAC/timer/GS/INTC restoration marker. All six moving phases finish; late-ready
candidate receives535 VIF/535 GS notifications per moving phase but zero kind3
moving copies. HELD candidate has two kind3 copies in every held set. This
qualifies notification correctness, NOT physical activation or performance.
Owned emulator26/Xvfb15 identities checked and stopped after completion.
Clean303 inputs deployed by editor-equivalent network reset; PS2 client36644
started successfully. Physical same-ELF OFF/ON/OFF notifications pending.

Source observation for interpreting A42: complete() immediately calls takeMain;
takeMain withdraws an Armed job before consumeOldMain waits for native readiness.
The protocol correctly forbids ISR claims after withdrawal, so notifications
cannot rescue that interval. This is not yet a measured moving cause. Do not
change frozen A42 V1 or relax its positive physical activation gate; inspect
retaining ownership through native completion only after this result.

A42 V1 linked audit now PASS: four IRQ roots/six precise queue/window callback
call sites, no queue/cache/kernel/wait closure. Hidden emulator running same
ELF1cdb92ca...86f147; first candidate held has two actual kind3 native-triggered
copies,32 VIF/35 native GS notifications, zero errors. Complete54-raster/
18-control/restoration gate still pending; partial activation is not pricing.
Archive runner supports A42's18 controls while A41 retains16 exactly.

A42 linked audit's second gate incorrectly counts every lw t9 as a callback load;
compiler also uses t9 for ordinary data. Correct qualification follows each of
the six actual jalr call sites to its preceding callback-field load0/4 (no
intervening t9 overwrite), still exactly two calls in each named handler.
Both failed audit logs and exact disassembly are preserved; no firmware change.

A42 V1 native496-file/ELF1cdb92ca...86f147 PASS; component V8 now91/644
optimized+sanitized/five EE entry checks PASS,37 archive hashes verified.
Initial linked IRQ audit rejects the old two-indirect-call assumption: compiler
inlines constructor-bound queue/window callbacks separately into START, native
and GS FINISH handlers (six calls). Actual disassembly has exactly loads0/4 into
t9 and two calls per these three roots. Revised gate binds that precise layout,
four entry roots and same callback closure, not arbitrary indirect resolution.
Template late-support=false folds new retry logic away; V1 frozen support=true
already has same behavior. Full linked/raster/activation/PS2 qualification pending.

A42 private native-ready-game V1 generated. Reader now source-binds constant
copy ON vs notification toggle, all old gates plus ready-mode/activation negatives
(18 total), actual kind3/witness equality and final IRQ/timer restoration marker.
Linked audit adds the actual nativeISR root; exact indirect closure must still
be inspected after linking. Native/component/emulator qualification pending.

A42 full-game late-ready fixture implementation begins. A41 copy stays ON in
ALL arms; only native-ready VIF/GS notification toggles OFF/ON/OFF. Separate
read-only DMAC observer leaves queue ISR OFF and never advances queue. One-shot
native GS IRQ preserves FINISH on refusal; both completion orders retry safely.
HELD-only START deferral requires a real GS/native IRQ copy (kind3), not only
component tests; moving no artificial delays. Same288-tick budget, Hybrid2,
54-raster/native/closure/activation/physical gates required before price.

A41 late-ready adapter V7 component PASS:90 controls in optimized and ASan/UBSan,
644 actual packet-validator cases per host mode, EE compile/60-byte adapter ABI
and four direct IRQ relocation checks. No runtime/FPS claim. Frozen component
archive v7 preserves sources/compiler commands/logs/disassembly; hash inventory
verified. Initial host linker spent minutes in I/O wait, process remained live
and progressed; no restart or timeout interpreted as failure.
Next full-game integration must reserve a separate VIF completion observer while
keeping TYRA_VIF1_QUEUE_ISR OFF. It must NOT invoke onComplete/start/cache work.
GS native FINISH and VIF may arrive either order; a masked one-shot GS handler
must preserve an unconsumed native FINISH if not ready and distinguish copy FINISH.
Use unchanged hardware budget, both-target output/ownership gates and actual
late-ready activation evidence before price. Keep A41 START-only as A/B baseline.

A41 late-ready component begins: START delegates to nativeReadyISR; that entry
rechecks all old-job/FINISH/GIF/window/ownership guards without opening a field.
New controls cover32 readiness masks, duplicate/main-withdrawal cases and all
six START/VIF/GS orders with valid vs expired hardware budget. No ISR waiting,
queue advance or cache operations. Native notification/timer/DMAC reservation,
linked closure, full output and PS2 benefit remain unqualified; no runtime change.

A41 reverse-v1 physical now completes all gates:54 exact rasters/16 controls,
1943 moving copies, positive moving ISR in ALL four candidates, no error counters.
ON/OFF/ON copy-kick Hz day57.559/47.656/57.450,
night51.247/40.074/51.230. Both net gates PASS; stable60 still FAIL. Candidate
long intervals day21/22, night77/77; night p95~33.647ms vs OFF33.375ms still
slightly worse. Copy median0.183295/max0.204044ms. Both orders now reproduced;
no default promotion/scene ceiling claim. game-reverse-v1 archive998 hashes
verify; both copied readers replay byte-identically. Owned client2588 stopped
after full completion; computer/PS2 remain ON.

A41 next source lead (not yet measured): blankStartISR opens the field and makes
ONE readiness attempt. If native completion arrives later within the qualified
blank, no entry retries: copyFinishISR only retires an already-started copy.
Protocol claimISR already requires an open field, old-job identity, ownership,
unsubmitted prefix and immutable packet, while adapter also checks actual
remaining hardware budget. Investigate bounded native-ready IRQ notification,
not polling/waits or a reduced safety budget. GS FINISH and VIF completion may
arrive in either order: preserve native FINISH for fallback, mask rather than
spin on an unready FINISH IRQ, and do not permit a new prefix to reuse source.
This is NOT A39's rejected main/per-bag ready publication under Hybrid3. Source
shows a potential missed opportunity, NOT that it explains all21/77 misses.

A41 reverse-v1 native496-file binding/ELF1510ae79...973cf8a and linked IRQ
closure PASS. Full hidden emulator completes six phases,54 exact rasters and16
rejecting controls;2524 copy completions,14 held ISR plus one moving ISR, zero
error counters. First IRQ audit was invoked before native binding finished and
failed on missing report; reissued after verified binding, not firmware failure.
Owned emulator/Xvfb identity-verified stopped after completion. Clean303 inputs
staged; same ELF deploying through editor procedure. Reverse PS2 price pending.

A41 first-order game-v9 archive now preserves998 files/all immutable PS2 launch
inputs; all hashes verify and pinned readers replay BOTH targets. Reverse fixture
begins from same bound A38 parent, changes mode order only to ON/OFF/ON (moving
AND held), all Hybrid2/54-raster/ownership gates unchanged. Revised reader requires
both candidates beat middle OFF and positive ISR in each actual candidate; full
native/emulator qualification precedes reverse physical pricing. No new counters.

A41 V9 clean physical-v3 completes all six phases and passes the full reader:
496 bound sources, same ELF2f6a2e99...cdb9f7,54 exact main/env/display rasters,
16 rejecting controls,971 moving copies (day160/night221 ISR), all paired
FINISH/window witnesses; no late/outside/refusal/drop. Copy median0.183302ms,
max0.204512ms. Hybrid2 OFF/ON/OFF copy-kick Hz day47.201/57.559/48.225,
night40.303/51.230/40.188. Net gate passes BOTH; stable60 fails BOTH.
Candidate long intervals>25ms day21/night77 vs OFF112/103 and172/173. Day
p95 improves33.367->16.966ms; night worsens33.367->33.648ms, so this is NOT
a no-regression/default promotion. Results: private
F:/Projects/tyrax2-hybrid-copy-game-20261011-v9/physical-check-v3.json and
physical-v3 (clean303 inputs). Reader replay is byte-identical, SHA256
008b87d566d0778d9ed2f1d348bcca6b10ad9a7a3798e3e1ea6624ef0fafeea2;
full repo archive/reverse order remain pending. Mean non-pacing scope rises~3ms
because new completion/fallback waits
sit outside the old pacing scope; do NOT label this extra EE packet preparation.
This demonstrates a substantial presentation scheduling issue without buying
a third buffer, NOT that all remaining limits are EE or that scene60 is impossible.
Next: preserve/replay this qualified run, reverse ON/OFF/ON with identical
contracts, investigate residual readiness deadlines; no blind timing/cache tweak.

A41 V9 first physical launch reaches phase5 before freepad DMA Busy; incomplete,
no accepted performance result. An initial tail-only observation incorrectly
called this startup failure. User-reset retry completes all six phases, but
copying the failed attempt's bin also copied outputs: strict input pins reject
drive-probe.csv. Keep BOTH raw attempts; do not relax the reader. Stage from
original game/bin with motion-run.py stage FIXTURE --stage-dir ATTEMPT_DIRECTORY
for a clean retry. Checker --physical-dir selects that attempt without weakening
source, ELF, launch-input, raster or completion gates. No default promotion.

A29 begins a private actual-generator bounded-output prototype after A28.
Prior A24/A25/C06 mechanisms were checked. First test list-input ring output
against the original per-triangle shader with per-kick RAM snapshots; this is
an intermediate gate toward native-strip VU input, not another EE cache.
Host V6 passes 1280 cases/29392 triangles, exact ordered vertex/state words,
immutable uploads and 3 rejection controls, including native strips/parity,
all64 masks and shared lighting/env/spot. Actual VCL/DVP assembly PASS; C
strip376/TCstrip404 words. Private late tag reloads fix C register pressure.
Next integrate normal VIF/DMA, folded-light/paint ABI, resident sets and guards
before physical timing. See A29.
No runtime gain/default claim.
A29 V8 now checks hybrid list/strip, current folded-light and compiled paint
ABI:7680 cases/176352 triangles and actual assembly PASS. Runtime V1 native
binding481 files and six hidden-emulator trips complete, with60 exact cold
rasters, SDK header/terrain contracts and resident billboards. Candidate main
set2034/2042 words is confirmed from linked symbol ranges. V1 is preliminary:
old packet-shaped hashes differ and activation is not separately gated, so
NO physical deployment/pricing yet. Revised canonical-input oracle passes4480
host cases/four controls. Next build V2 and its rejecting reader, then emulator
and real PS2. V2 now passes native481-file binding and the full six-trip hidden
emulator:60 exact rasters, equal canonical triangles, positive native activation
only in candidate, fewer positive SDK-verified headers, all22 reader controls.
Owned emulator stopped. Same ELF is running physical day/night Off/On/Off;
V2 physical now completes all gates but NOT the performance gate: mean savings
only0.035635 day/0.042599 night ms, candidate42.130/37.131 Hz and night p95
regression0.226854 ms. Keep private; no reverse micro-tuning. Source review
finds native admission only on refined strips, leaving ordinary unrefined roads
at16 tiny jobs/66 inputs. A29's next --native-fallback revision targets that
excluded branch with unchanged masks/triangles and distinct cold activation.
This is not A24's raised fan-out cap. Devices stay on; allowance90% remains.
V2's646-file archive reanalyzes byte-identically, unchanged. Broader A29 V3
now has native481-file binding and begins fresh hidden-emulator qualification:
first checkpoint6 fallback jobs/226 triangles, all752 canonical triangles equal.
No physical V3 price or default claim yet.
V3 now completes full hidden-emulator qualification:60 exact rasters, positive
native/fallback admission, canonical inputs and26 rejecting controls; first day
headers242->179. Owned emulator stopped. Same ELF is deploying to PS2 through
the editor-equivalent network reset; price remains pending.
V3 physical now completes correctness and first-order performance gate:
same-ELF mean savings0.649116 day/0.666708 night ms, spatial0.786443/0.613253;
candidate p95 improves, but43.304/38.433 Hz is NOT60. No promotion/ceiling proof.
Archive the completed broader run. Next source questions are private ring's
introduced per-triangle GS-state repetition and EE's retained fine classification/
safe-Cull list repacking under the new bounded-output contract. Read A24/A25/
C06/C11/C18/H02 first; new input/output guards are mandatory for route changes.
Details and explicit non-claims are in the A29 page.
V3's645-file archive reanalyzes both reports byte-identically, inventory unchanged.
All owned emulator/file-server processes stopped after completed exports. PS2
and computer remain on; last allowance90% remaining. Goal remains ACTIVE.
The state-once follow-up passes host11520/264528 cases, five controls and291
empty-first cases. V9 whole-set2044 is rejected; V10 shares input-step loads
and selected C394/TC466 yield2038/2042, preserving billboards/fences. Rejected
V9 artifacts93 files and V10 qualified artifacts94 files are inventoried.
Partial runtime V4/V5 symbol guards fail before build; actual-source inspection
fixes four spellings per image. Fresh V6 is building; no hardware price yet.
V6 now has native481-file binding and full emulator60-raster/26-control PASS;
linked2038 residency retains billboards. Same ELF deploys to real PS2; price
pending. Host V10 read-only recompilation reproduces oracle and20 images with
unchanged94-file inventory. No default or60fps claim.
The first V6 physical attempt stops exporting during night candidate phase4:
four travel windows and20 main/env pairs exist, but no DRIVEDONE. This is
INCOMPLETE, not accepted pricing, nor established renderer/console failure.
Preserved unchanged at the fixture's physical-interrupted directory. The
owned client2608 was stopped and a fresh303-input staging launched the same
ELF through the editor network-reset procedure; client24416 starts rendering.
Complete/reject this retry before another route change. Last allowance89%
remaining; no shutdown condition applies while independent work remains.
A30 prepares the cold pre-route source/delivery/residual contract while that
retry runs; no runtime route or V6 source change. Prior A24/A25/C06/C11/C18/H02
checked. The proposed whole partial-strip VU route removes fine classification
and safe-Cull list repacking, not another cache. See
[A30 requirements](tyrax2-coarse-strip-2026-10-11.md); host execution pending.
V6 retry also stops in night candidate phase4 at frame9120, without DRIVEDONE.
Both attempts are physically incomplete and unpriced; no cause is established.
Owned server24416 stopped after unchanged-output recheck. Preserve the external
physical-status.md and both attempt directories; no state-once promotion.
A30 cold helper now passes17920 cases/seven controls optimized and ASan/UBSan;
its archived source/result inventory is external host-v1. Build the coarse EE
route against qualified A29 V3 images, active in BOTH arms; no shader change
inside the new contrast. Devices remain on; independent work is available.
A30 V1 native build/binding482 files PASS; ELF73cb2132...a17ebb4. Hidden
emulator769/Xvfb760 runs six-trip correctness with qualified V3 ring images
constant in both arms. Actual cold reader synthetic positive/14 controls pass;
new archive guard explicitly distinguishes source/residual equality from changed
post-route input. No physical A30 pricing yet. Host V2 repeats sanitizer tests.
A30 V1 emulator completes all six trips/60 exact rasters/29 negatives and2034
resident words. Flag audit fixes inherited report assignment that mislabeled
whole post-route inputs equal; preserve the initial report, reanalysis passes
with that flag false and separate source/delivery/residual/baseline gates true.
Generated-report flag control added. Owned emulator stopped; same ELF staging
for physical A/B. No runtime/default change from this reporting correction.
A30 same ELF now runs physical six-trip fine/coarse/fine day/night after fresh
303-input staging/editor-equivalent reset. Owned server33384 starts successfully;
hardware qualification/price pending. Last allowance88% remaining. Goal ACTIVE,
no default promotion,60fps claim or scene ceiling proof; devices remain on.
A30 V1 physical pricing aborted: code reinspection finds introduced EVERY-frame
false/true ring drains in main loop. Reject performance apparatus; preserve
unchanged V1 proof/ELF and partial output. Owned server33384 stopped. Generator
now drains only when coarse mode changes; cold arm/restore boundaries remain
off-price. Reader guards conditional main-loop setter; fresh V2 required.
A30 V2 actual main-loop256-frame/two-control test passes and rejects V1. Native
build/binding482 files passes; fresh ELF09a9fbb0...012ed00a starts hidden
emulator31/Xvfb20 for six-trip qualification. V1 remains unchanged/rejected for
performance. No physical V2 run yet; full goal ACTIVE and devices remain on.
A30 V2 full emulator qualification passes six trips/60 exact rasters/29 negatives,
2034 residency and source/delivery/residual/baseline gates with honest false
whole-input flag. Owned emulator stopped. Same ELF physically launched through
editor reset/fresh303-input staging; server16524,04:05:51, startup confirmed.
No hardware price yet. Finalized18-file host V3 archive includes actual-switch
fixtures and corrected report-flag tests. No public/default change or60 claim.
A30 V2 physical completes all six trips/60 exact rasters/29 negatives plus
source/delivery/residual/SDK/quiet/terrain/residency gates. First-order gain
0.738364 day/0.743663 night ms, spatial0.778940/0.699887; candidate p95 improves.
Only46.555/40.338 Hz, NOT60. This is incremental coarse-vs-fine under the SAME
ring shader; no cross-ELF sum, pure EE attribution or ceiling proof. Owned
server16524 stopped after completed exports. Archive V2 before next decision;
keep private pending order/production qualification. Larger EE cause unresolved.
A30's next order check uses --reverse coarse/fine/coarse with unchanged shaders,
geometry, cold-arm sequence and timer-free travel. Coarse stays mode7; adapt
inherited A28 reverse role labels/contrasts accordingly. Do not reuse its
semantic mode inversion. Archive/reanalyze V2 before launching the reverse run.
A30 V2's654-file archive reproduces both reports byte-identically with unchanged
inventory. Initial log-hash guard correctly rejects emulator post-analysis output;
preserve pre-stop report and requalify closed log before archiving, without
waiving any check. No owned server/emulator/compiler process remains. Reverse
fixture/reader support is implemented and cold reader controls pass; reverse
runtime derivation is the next action. Goal remains ACTIVE, not60 or ceiling proof.
Reverse V3 actual-switch controls and fresh native482-file binding now PASS;
ELF907e82b3...a53b37b begins full hidden-emulator qualification. Source shaders
remain A29 V3 repeated-state images in both arms; no reverse hardware price yet.
Existing V2 heavy-frame/capture correlation is attribution only. C03/A14/A16
already cover capture freezing and target rejection; no repeated lead is opened.
V3 now completes all six hidden-emulator trips and full60-raster/29-negative,
source/delivery/residual/baseline/SDK/quiet/terrain/residency gates. Owned
emulator31/Xvfb20 verified and stopped before closed-log qualification. The
first reader attempt correctly rejected its hard-coded phase1/mode7 quiet
mutation: reverse has mode0 there. Mutation now follows the planned mode;
actual generated quiet/focus reader passes both orders/two positives/eight
controls. No check is waived. Same-ELF reverse physical A/B is next, unpriced.
V3 reverse physical now starts successfully through the editor reset, fresh303
staged inputs, owned server33320 at04:40:10+02:00; first cold export confirmed.
This is a live run, not accepted hardware pricing. Last allowance87% remaining.
Larger follow-up question is documented in A30: prepared fixed-world commands
with live camera/pass inputs, NOT an A18 frozen startup capsule or generic
P08/H02/C05 cache/direct/fused variant. First audit invariant command words,
identity transforms, mutable streams and resident terrain lifetime; no new
runtime or counter experiment yet. Finish reverse hardware qualification first.
V3 reverse hardware now completes all six trips/60 exact rasters/29 negatives
and full source/delivery/residual/SDK/quiet/terrain/residency gates. Savings
0.740318 day/0.689314 night ms, spatial0.794594/0.695813; each outer candidate
improves and p95 improves. Matches forward0.738364/0.743663. Order qualification
PASS, but candidate46.441..46.785/39.731..39.831 Hz is NOT60. Owned server33320
stopped after exports.654-file V3 archive reproduces both reports byte-identically.
No default promotion or ceiling proof. Next source audit is the documented
live-world producer question; no more A30 micro-tuning. Goal remains ACTIVE.
A31 source audit is now recorded in [prepared world feasibility](
tyrax2-world-stream-2026-10-11.md). P08/H02/C05/A18/A20/A21 and existing baked
routes were checked. Live uniform heads cannot be frozen: fog/light/texture
residency and program state vary. Owner-built geometry plans require explicit
slot replacement and frame leases, not pointer-keyed reuse. Private host source
prototype passes360 partitions, six corruption/13 invalid-source controls and
360 failed-replacement/lifetime checks optimized+ASan/UBSan. Four-file external
inventory preserves sources/results. No VIF compiler/runtime/price yet; next
qualify actual emitted geometry with live heads, bounded shaders and ownership.
A31 actual SDK compiler/execution gate is now implemented privately in a fresh
derived fixture. It runs ONLY final cold observed frames, compares original and
owned-source emitted bodies exactly, and executes the compiled REF with live
heads. Existing prepareStaticGeometry was checked: prewarm/pin is not this
owner-built API. V1 build failed missing ClipTrial include, never deployed;
fresh V2 adds clip_capacity.hpp and builds. Actual reader positive/six controls
PASS; native/full raster qualification is pending. No priced-route/perf claim.
A31 V2 now passes native484-file binding, ELF6ccb6381...70808fab; hidden
emulator372/Xvfb361 starts full six-trip qualification. First two cold candidate
frames each compile/execute40 bodies/20272 words with zero control admission.
Host15-file v3 includes full actual-reader dependencies; optimized/sanitized
fragment positive/ten controls and reader positive/six controls/no-perf flag PASS.
Only partial runtime admission yet; no hardware or performance result.
A31 V2 full emulator qualification now passes all six trips/60 exact rasters/
35 negatives plus source/SDK/quiet/terrain/residency gates and positive compiled
execution in each cold candidate. Emulator372/Xvfb361 verified and stopped before
closed-log analysis. Same ELF stages303 inputs for editor-protocol PS2 deployment;
hardware result pending. No priced-frame route change or performance claim.
A31 same ELF now physically starts via editor reset/fresh303 staging, owned
server44876 at05:10:04+02:00. FRAMEPIPELINE startup confirmed; complete/reject
this specific run before another deploy. No price/performance acceptance.
A31 V2 physical now completes all six trips/60 exact rasters/35 rejecting
controls and all inherited source/SDK/quiet/terrain/residency gates. Each cold
candidate positively executes compiled owned bodies beneath live heads;
controls execute none. Owned client44876 stopped after complete exports.
664-file archive reproduces both reports byte-identically, inventory unchanged.
Cold compiler gate PASS, performanceCandidate=false: no hot-route speedup.
Existing Vif1Queue immutable bank leases/retirement were audited for subsequent
owner integration; C05 lookup/fusion is not reopened. View-specific clip masks
and start/continue cannot be frozen from the cold invocation. Require actual
producer hooks, lifetime and all-view raster contracts before live hardware A/B.
Goal ACTIVE; devices on, latest allowance86% remaining, no shutdown condition.
A31 next cold gate checks view-independent all-six-plane masks on the same
selected native C/TC buffers before owner bypass integration. A24/A25 clipping
omission and A29/A30 conservative-mask work checked: this only broadens mask
bits, retaining source/head/kick state. Require one permitted header difference,
positive changed masks and60 exact rasters; no hot-route/FPS claim. An exact
pass does not qualify full-owner offscreen input or leases. See A31 gate.
World-mask V1 builds/binds485 files but emulator first candidate asserts:
host guard expected6c028000, actual SDK sets unsigned bit14 too (6c02c000).
Reject apparatus, preserve V1 unchanged; no all-plane raster conclusion.
Verified owned emulator385/Xvfb374 stopped. Require exact SDK STCYCL/UNPACK
signature and a new unsigned-flag control, derive fresh V2 rather than mutate V1.
World-mask V2 native485-file binding now passes, ELFe5fe4d6b...47d54c8d;
owned hidden emulator392/Xvfb381 runs six-trip correctness, no hardware yet.
Host optimized/sanitized15 controls and actual reader4 pass; final external
host-v3 has16 inventoried files/full reader chain, host-v2 copying incomplete.
Complete/reject this exact runtime before another route change/deploy.
World-mask V2 now completes full closed-log emulator qualification:60 exact
rasters/39 rejecting controls,30 mask records, positive candidate40 changes in
each group, zero controls and all inherited source/SDK/quiet/terrain/residency
gates. Verified emulator392/Xvfb381 stopped. Same ELF stages303 inputs and
physically starts via editor reset, client45844 at05:31:28.470+02:00. Complete/
reject this exact run; hardware/archive pending. Only selected native cold bodies
qualified, not full owner offscreen input, lifecycle or live FPS. Goal ACTIVE.
World-mask V2 physical now completes60 exact rasters/39 controls and all
inherited contracts. Client45844 verified/stopped after exports;671-file archive
replays both reports byte-identically, inventory unchanged. Selected native
all-plane route qualified, NOT full-owner input/lifecycle or live FPS.
Independent explicit compiled-owner prototype now passes actual engine bank
table tests optimized/sanitized,12 stale-key controls, two-bank destruction/
replacement,512-span saturation and live+retired capacity budget. Queue audit
also requires raw-DMA sequence fences on sourceCopied=false snapshot fallback;
bank bits alone are insufficient. Final10-file host-v3 archive recompile passes,
inventory unchanged. No actual callbacks/producer hooks or hot route yet.
Next actual producer-owned prepared submission with ordinary live heads; retain
all source/offscreen/companion/main-env gates before same-ELF hardware price.
Actual private producer hooks now build world bodies on chunk replacement and
program-epoch transitions, not per-frame world scans. Owner runtime V1 rejects
that accidental scan before building; V2 missing include and V3 namespace/bag
copy integration failures remain preserved, undeployed. V4 links/binds486 files
and starts hidden correctness; first baseline admits7 owners/1942 triangles.
Host-v6 copy-reset attachments and13 key controls pass optimized/sanitized actual
bank-table tests. No complete owner raster proof, new-domain reader or physical
price yet; see A31 for authoritative artifacts and pending gates.
Producer V4 later stops on inherited native-call assertion (obsolete for owner
bypass), preserved/rejected without hardware. V5 scopes owner and ordinary
residual domains separately; closed hidden run completes six trips/60 exact
rasters/15 controls, positive candidate7 owners/1942 triangles and zero control
draws. Explicitly preliminary/no price. V6 binds487 files and strengthens source
proof by decoding actual compiled payload; hidden correctness currently running.
Host decoder560 cases/5040 controls optimized/sanitized;8-file evidence recompile
and inventory unchanged. No physical owner result yet; no60fps/impossibility proof.
V6 now completes closed hidden six-trip correctness:60 exact rasters/15 controls,
30 full-source records with compiled-payload decoding and ordinary residual/input/
header equality. Emulator579/Xvfb568 verified/stopped. This is PRELIMINARY with
performanceCandidate=false; complete moving activation/acceptance reader and
actual-map lifetime/memory gate precede editor-protocol same-ELF physical A/B.
Goal ACTIVE, latest allowance100% remaining, devices stay on. This turn makes
progress through owner integration/correctness, not completion or a blocked turn.
V7 actual off-price moving owner witness is positive (day17 draws/15829 qwords),
but V7 is rejected for causal pricing: baseline also compiled unused owner bodies
on producer rebuild. Require V8 candidate-only hot producer cost, baseline warm
owner release, epoch/role-only sweeps and net-cost flag. Acceptance reader passes
two positives/21 controls and retains full parent travel/raster gates; V8 native
pending. No V7 hardware deployment, performance or capacity-ceiling claim.
V8 native487 binding and closed complete emulator acceptance now PASS: six
trips/60 exact rasters/38 controls and positive moving owners, net producer-cost
guard/free/sweep proof. Same ELF physically runs through editor reset, owned
client34536 at06:25:53.453+02:00; complete this run before another deploy.
Hardware/archive pending. No partial-run speed claim or default promotion.
A31 owner V8 now completes hardware qualification: six trips/60 exact rasters/
38 controls, moving source/compiled/residual/SDK/quiet/terrain/residency gates,
candidate-only producer cost. Net mean gain0.302511/0.392621ms day/night;
spatial0.423909/0.482654ms, candidate49.07/41.31Hz. First-order gate passes,
NOT60fps, no reverse or default promotion, no capacity proof. Owned client34536
stopped after complete exports;670-file archive replays both reports exactly,
inventory unchanged. Leaves Core frontend/live heads/other classes intact;
all-plane whole owners also add offscreen VU work, so net is NOT pure EE time.
Do not reopen lit CPU clipping: with vu1Clipping=true the actual clip function
returns through VU unconditionally. Beam/corona work already closes at H11.
The moving-vehicle source audit is now complete below (A32). Paint evidence is
A08/A11/A13/C16, NOT A19 (A19 is the adjacent-list oracle). A17 already rules
out repeated normal preparation in its qualified driving workload. Local car
geometry and full-visible baked replay already exist; do not build a duplicate
car-body route or rename enabling existing VU paint as a new finding.

### A32 Prepared Draw Frontend: Source Audit (2026-10-11)

Question: after A31 bypasses partial-world package/body preparation for admitted
bags, can an explicitly producer-owned draw description replace the repeatedly
interpreted generic StaPip frontend across static terrain/roads/batches/model
parts, while retaining current visibility and all live state? This is NOT a new
matrix memo, normal cache, body cache or fixed-header micro-optimization.

Audit result: `game_collision.gen.cpp:1838` changes LOD bindings only on an
actual tier change; `:2012` refreshes moving-object matrices without rewriting
local geometry. `game_physics.gen.cpp:1649` folds the current object rotation
into the env basis, and `:1748` materializes colours on EE only when the accepted
paint representation requires it. VU paint preserves this accepted basis rather
than accepting a new camera basis to force readiness. Fully visible/guard-band
bags already reach whole-baked replay after generic preparation. Extending A31
only to vehicles would therefore leave that preparation intact and duplicate
an existing body mechanism on the common direct route. No new vehicle-owner
runtime is justified by this audit; A13's approximately1ms paint offload remains
a separate existing candidate, not the explanation of the entire gap.

The source-backed larger intervention is a prepared frontend contract, not just
a prepared VIF body. Current `stapip_core.cpp:622..1070` derives capacity,
looks up bounds, checks transforms, builds MVP/planes, resolves material/program,
selects texture/light and sends object data on EVERY generic invocation, before
existing replay can return. H06/H08/P02 price pieces and memo variants; A21's
frozen night Setup5.60..5.62ms establishes a substantial inclusive region, NOT
recoverable pure EE time or a current moving-scene saving. A31 leaves this
region in both arms. A26 replaces only buffer-header closure, so it does not
answer this full frontend question. C08 prepares/pins geometry without a bounds
or frontend contract; H02 changes the producer/transport route, not this split.

Before a runtime A/B, establish the following ownership and admission contract:

- Compile source shape, legal stream/program class, package capacity, independent
  main/package bounds and immutable material facts at producer replacement.
  Own the bounds: retaining a generic bbox-cache pointer is not a lifetime proof.
- Preserve live view/model transforms, exact current culling/guard-band routing,
  light reception and selected light, fog decisions, texture/CLUT residency,
  current wrap/filter/blend/depth/DATE state, and all barriers. Do not substitute
  an all-inside classification or a frozen dynamic-light result.
- Explicit invalidation includes stream pointer/count/content generation, LOD,
  bbox version, material/primitive/depth configuration, resident program image/
  address epoch, overrides, scene replacement and producer destruction. Copying
  a bag must reset ownership as in A31. Paint representation is live; mutable EE
  paint must fall back or invalidate, never be silently certified immutable.
- Unsupported classes/features retain the original generic path. Initially
  exclude custom programs, billboards, mutable/skinned/wheel producers and BLSS
  proxy collection; prove supported-class coverage before interpreting a delta.
- Candidate-only compilation/replacement/memory cost must stay in the hot
  candidate when applicable. No whole-world validation/hash/scan per frame;
  no hidden common compilation as rejected A31 V7 did.
- Use actual SDK/program output and independent source/state oracles across
  changed main/env views and source replacement; require exact60 rasters,
  positive moving activation and rejecting stale-contract controls before price.
  Hold A29/A30/quiet/paint policy constant in both arms. Same-ELF gas trips must
  retain roads, live captures and night lights; require order/tail gates before
  any production promotion. No new clock partition is proposed.

Status: source audit changes the next action from another car body to the broad
frontend contract. No prepared frontend runtime, host qualification, hardware
price, new FPS improvement or scene ceiling is claimed. Goal remains ACTIVE.

A32 now implements a private owned-facts foundation in
`tools/tyrax2-prepared-front.hpp`, not the complete Core frontend API. Explicit
generation/source/resource/content/material/program/depth keys admit owned
StaPipBagPackagesBBox values; independent replacement preserves the old owner
on allocation failure, copy-reset attachments prevent portal aliasing, and
generation wrap is rejected. The private prototype caps source count65535 and
parts4096 BEFORE constructing bounds. These per-owner limits are NOT a global
PS2 memory budget or DMA/source-stream lease.

Final host V4 passes512 geometry cases,12288 changed view/model classifications
and15360 stale-key controls, optimized and ASan/UBSan, plus invalid input/limit,
mutation/replacement, allocation failure, copy/self-assignment and wrap checks.
Eleven actual engine headers/sources are bound to A31 V8's native source identity.
The actual bbox constructors, merging and plane/classification code are copied;
only the VU0 assembly min/max leaf and platform/vector/matrix ABI operations
are host shims. Finite host inputs do not qualify PS2 floating-point behavior,
the complete StaPipBag-to-key adapter, SDK headers, raster or FPS. V1..V3 are
superseded host stages, not failed console tests. No console deployment occurred.

The 23-file host source/log inventory
and preserved originals replay from copied C++ without modifying the archive.
Next implement the actual legal-bag adapter and producer replacement hooks,
then route Core through admitted owned facts without freezing live semantics.
Do not sell this bounded-bounds foundation alone as the full A32 optimization;
complete actual-writer/moving activation/raster/memory/net-cost gates first.

A32 runtime V1 now binds489 native sources and routes admitted ordinary bags
through owned validation/capacity/bounds while retaining live head/dispatch work.
Candidate-only producer hooks and baseline release preserve net replacement cost;
global8192-part reservation includes old-plus-new peak. Closed hidden emulator
passes six road/reflection trips,60 exact main/env rasters and47 rejecting reader
controls. Moving candidate windows admit261/276 frontend draws in four frames,
peak reservation2304 parts. Fresh tracked-owner host V2 passes64 budget/copy/
replacement rounds optimized and ASan/UBSan plus the512-case foundation. Actual
adapter mutations are not exhaustively host-qualified. The same ELF was launched
on PS2 with editor-network-reset, owned client27604; hardware acceptance/price
pending. This combines A31 owner + A32 stage, not an isolated A32 gain, complete
frontend bypass or production promotion. See the A32 runtime section in the
linked world-stream report before starting another lead.

V1 hardware now completes six trips/60 exact rasters/47 controls. Combined net
mean savings0.141240/0.235260ms day/night, spatial0.218727/0.230745ms; candidate
47.013257/40.072861Hz. P95 worsens0.898410/0.651225ms against the worse control,
so first-order gate FAILS, private only. No isolated A32 price (different A31
ELF must not be subtracted), no60fps and no capacity ceiling.680-file archive
replays both reports byte-identically, unchanged inventory;31-file tracked-owner
host archive also replays unchanged. Close THIS combined bounds-only policy;
broader generic interpretation/live-head elimination remains unimplemented.
Do not repeat another key/bounds variant or closed road-grouping/ordinary-batch
policy as the larger intervention. Next contract is saved in the report.

### A33 Complete Object Head: Compiled Fragment (2026-10-11)

Review before this lead: A26 changes the two-qword buffer header only; existing
sendObjectData already reuses SDK-generated UNPACK headers. H06/H08/P02 split
or memoize selected work, A31 prepares geometry bodies, A32 bounds-only combined
policy fails promotion. None qualifies a complete object-head/state template.
This lead must not become another header lookup micro-fix: compile the complete
post-wrap object-head fragment with explicitly live MVP, spot data, colour, fog
and TEX0 slots, then remove original interpretation/packing from hot supported
draws. Keep FLUSHE, dynamic wrap, light/fog decisions, texture/CLUT residency,
live program/primitive/depth semantics, barriers and queue lifetime unchanged.

Private ObjectHead::Image currently owns a validated11..32-qword fragment,
not geometry or a whole frame. Typed exact facts/generation and shape govern
admission; malformed command order/registers, stale facts, short destinations
and aliasing live inputs fail BEFORE destination writes. Only the renderer's
explicit clip-constant REF is permitted, with matching epoch: this is NOT an
independent lease/lifetime proof or permission to retain arbitrary DMA refs.
Failed compile preserves the old image. Broad producer fact adapter, invalidation,
net lazy/rebuild allocation cost and hot frontend elimination remain required.

Host V3 compares24576 changed live writes to14 copied actual SDK headers and
rejects61440 stale/command/capacity/alias controls, optimized and ASan/UBSan.
The host oracle uses actual manual UNPACK close with known V4_32 counts, not
target auto-close implementation; actual SDK pointer-layout assumptions are
host64-bit, and -fpermissive permits target-address casts in inline helpers.
No host speed, native auto-close, raster, PS2 or complete bag-adapter proof.
V1 host linked an unavailable external auto-close symbol; V2 switches explicitly
to the SDK manual close. V3 strengthens actual-tag walking and alias rejection.

Runtime fixture V1 generation fails on a nonexistent include anchor before
native build; preserve it. Fresh V2 derives qualified A31 owner V8 (NOT failed
A32 policy), binds489 sources to ELF
8be4b2748cda27277a2910739332bbc32df1e8caa470aa8536ac2eadc012f761,
and starts hidden emulator PID31/Xvfb20. Its actual writer first creates the
ordinary post-wrap bytes; the cold checker compiles/patches those and requires
byte equality, then candidate cold arm1 substitutes the compiled bytes into
the submitted packet. Current MVP/colour are read from semantic arguments;
spot/fog/TEX0 patch values are read from the actual original SDK output. Thus
this qualifies SERIALIZATION/EXECUTION only, not an independent light/fog
algorithm or fast producer adapter. The hot writer remains original. The full
derived reader forbids performanceCandidate/contrasts/firstOrderGate and retains
parent source/residual/raster gates plus cold head activation/mutation controls.
Emulator completion, closed-log qualification and physical proof pending.
A33 V2 now finishes closed emulator six trips/60 exact rasters/30 cold head
contracts/44 rejecting controls; owned emulator stopped. Day27 heads/338qw,
night30/382, candidate only. Actual generated parser1 positive/10 controls and
22-file read-only host replay pass. The inherited reset-string assertion was
updated to include HeadTrial reset, preserving every parent gate. The same ELF
is now running physical qualification via editor reset, client44144. Fixed
TEX1/TEST/ALPHA versus live fog/spot/MVP/RGBA/TEX0/CLUT dependencies and all packet
state/clip-block lifetime requirements are recorded in the world-stream report.
Do not turn the demo's shape cache/SDK-derived facts into a production adapter.
A33 V2 physical now also qualifies six trips/60 exact rasters/30 head contracts/
44 controls; client44144 stopped after completion, closed log accepted. Initial
physical reader still attempted an inherited late firstOrderGate after contrast
removal; both late physical/reverse pricing blocks are removed, no price accepted.
678-file archive reproduces both reports byte-identically, inventory unchanged.
hotOriginalHead=true/performanceCandidate=false, no contrasts or firstOrderGate:
the optimization remains unimplemented, not60fps or a ceiling. Next implement
producer fixed facts and direct live inputs to bypass original packing and broad
frontend, with net compilation/invalidation/fence gates. Do not repeat another
cold-only kernel as a substitute for that requested hot boundary.

### A34 Direct Semantic Hot Object Head (2026-10-11)

Question: does bypassing the original per-draw SDK object-head serializer,
with fixed semantic state owned by a stamped bag and only live fields patched,
materially reduce moving EE preparation? This differs from A26's two-command
header tweak and A33's cold-only template, which retained the original hot
writer and read some live values back from its output. It is not another
geometry/bounding-box cache variant. A31 compiled world bodies are common to
both hot arms; original Core preparation and unsupported heads remain intact.

Require independent semantic MVP/RGBA/spot/fog/TEX0 inputs, fixed LOD/TEST/ALPHA
and clip-reference invalidation, bounded old+new ownership, copy-reset/move
semantics, original packet wrap/fences, cold whole-byte and full parent source/
raster equality, positive moving head admission and net lazy compilation cost.
Use same-ELF day/night road/reflection driving A/B, not emulator timing. No
promotion without these gates or meaningful net gain. V1 build rejected a
wrong GeoPart baseBag field; preserve it, generate V2 using the actual bag.
V2 native490-file binding passes (ELF44cb3fa5...b619f3c). Updated byte-writer
host24576 writes/61440 controls passes optimized/sanitized. Actual owner128
budget/replacement/failure/generation/copy/move rounds and extracted semantic
facts16384 mutation controls pass optimized/sanitized; pipeline fields are host
shims, not native numerical proof. Generated reader1 positive/18 negatives
passes. V2 moving admission reaches323/359 day/night heads but baseline leaves
75 words resident after switching: reject lifecycle acceptance and hardware
deployment, retain the strict zero-baseline gate. V3 adds missing silhouette/
wheel/portal release and immediately frees baseline-only cold head ownership.
Native V3 build and fresh full emulator qualification pending.
V3 then binds490 sources (ELF7488c26e...9a842491f) and completes six trips/
60 exports, but the final NIGHT baseline retains13 words: full lifecycle gate
FAILS again, no PS2 deployment/price. Stop owned emulator after completion.
Do not extend the producer cleanup whitelist yet again. V4 instead maintains
an intrusive registry of actually created owners, backlinked to their unique
attachment; move retargets the backlink, copy resets, destruction unlinks.
Baseline role transitions release every registered attachment, not a guessed
list of render passes. There is no extra steady-frame sweep or registry heap
allocation. New tests include replacing a populated moved attachment, self-move
and complete release. Native V4 qualification pending. The separate22-file SDK
and30-file V3 host archives replay read-only with unchanged inventories; they
do not qualify this new registry or prove a moving speed gain.
V4 native490-source mirror/binding completes, ELF
2692b91c836b3cfe1553c954149370c15d863410d9bb4678542956d2b369dc1c.
Fresh bound host V4 passes128 ownership/budget rounds including complete
registry release/populated move/self-move, and16384 actual-facts mutations,
optimized and ASan/UBSan. All native/host command sessions are completed.
Checkpoint: V4 has NOT launched emulator or PS2. Next use fresh hidden V4
emulator, close/qualify all six trips/60 rasters and zero baseline ownership;
only then stage303 inputs and deploy via editor-default network procedure.
PS2 remains on the previous A33 game; no owned ps2client or emulator is live.
No A34 speed, production promotion,60fps or impossibility proof exists yet.
Next continuation revalidates native490 binding and starts hidden V4 emulator
PID31/Xvfb20 (fixture launch.json is authoritative; verify /proc identity).
Do not relaunch because an observation timeout expires. V4 owner host30-file
archive recompiles/runs read-only including registry moves/release. Allowance
now4% used/96% remaining; no stop/shutdown condition. Physical not launched.
V4 now completes closed hidden emulator six trips/60 exact rasters/30 semantic
head contracts/49 rejecting controls, zero baseline words in all control roles.
Common world bodies and all parent ordered-source/residual gates pass; no
emulator FPS price. Owned emulator/Xvfb stopped only after completion/exports.
303 physical inputs staged, identical ELF deployed via editor network reset,
client28308 at2026-10-11T08:46:52+02:00. Physical execution is in progress;
do not restart on a polling timeout or claim speed before its closed full gate.
V4 physical completes six trips/60 exact rasters/30 heads/49 controls; owned
client28308 stopped after all exports, closed log accepted. Head-only policy
REGRESSES mean0.510422ms day/0.670933ms night; equal-distance regressions
0.613064/0.620994ms. Candidate p95 worsens1.267100/0.991038ms relative to the
worse adjacent control. Candidate44.442764/38.702736Hz, not60fps.
firstOrderGate=false; keep OFF, do not tune the template key into another
mini-cache iteration. This rejects this head policy, not a full frontend
rewrite or a workload-preserving capacity ceiling.684-file immutable archive
reproduces both reports byte-identically, all hashes unchanged. See
A34 evidence.

### A35 Full Prepared World Frontend: Next Boundary

Before selecting this lead, re-read A32's broad source audit, rejected bounds-
only runtime, A31 owner and A34 head price. This continues the still-unimplemented
whole frontend contract; it must NOT recreate A32's bounds substitution or
A34's per-draw semantic-key/head template. A34 establishes that this independently
prepared whole head is slower under current driving, not that all EE work is
efficient. Leave the original head writer common in the next contrast.

Authoritative A31 V8 Core still performs getMaxVertCountByBag, transform-cache
comparisons, generic bbox lookup, capacity/packager setup, material/program/light/
texture preparation and object data at Core render:622..1079. Only THEN can
replayPreparedWorld return at1086..1090. Fully visible bags similarly reach
whole baked replay after this frontend. A31 did not move that boundary.

Next source-backed design is a producer-compiled, typed WORLD draw ticket
submitted before generic Core interpretation, with original live head packing.
Start with stamped baked world-space terrain/roads/proc/static batches, not a
vehicle-only body owner duplicating existing replay. Compile supported stream/
program/material shape and independently owned bounds at producer replacement;
runtime must retain real current outside/inside/partial and guard-band decisions,
current view/MVP/light/fog/texture residency and pass state. Establish BOTH
inside and partial dispatch contracts, not blindly forcing all-plane clipping
or accepting frozen visibility. No per-frame full-world scans or new timer mesh.

Initially exclude mutable/wheel/skinned/billboard/custom/normal-coordinate/BLSS
proxy classes. Copy/move/scene/program epoch/material/source replacement must
invalidate or own correctly, with candidate compilation/replacement inside
its actual cost. Unsupported routes remain ordinary. Required discriminating
gate: positive moving early-Core bypass, exact actual source/state/SDK and60
rasters across day/night main/env, unchanged original heads in BOTH arms,
same-ELF net price and order/tails before promotion.

A35 is now implemented PRIVATELY in world-front-fixture/methods/owner tools,
derived from qualified A31 V8, not A34's head policy. Initial producer hooks
cover stamped unlit multicolor triangle-strip terrain/proc/roads, NOT all
static batches, lists, vehicles or skins. Top-of-Core renderWorldTicket skips
generic capacity/bbox/packager interpretation for admitted draws; real current
outside/inside/partial/guard-band classification, existing transform reuse,
live texture/light/fog and original SDK head writer remain. Both inside baked
and partial owned body dispatch occur; unsupported/stale bags fall back BEFORE
texture/head submission. Independently owned bounds and intrusive attachments
have a1024 old+new ticket cap (not a total heap proof). Complete release occurs
at inactive role transitions only, not through a per-frame scene scan. A31
world bodies are common BOTH moving arms, isolating the new frontend boundary.

V1 generation anchor fails; V2 native build rejects const MVP; V3 native488
binding passes. V4 retains mutable MVP, existing transform-cache reuse and an
actual whole-original-head digest. Native488 binding ELFf8839740...81b passes.
Closed hidden emulator completes six trips/60 exact rasters/30 whole-head
oracles with positive moving inside+partial dispatch and zero baseline owners.
The preflight reader preserves all parent source/state/residual/quiet/travel
gates and adds9 rejection controls; generated parsers pass1 positive/22
mutations. Bound host V2 tests actual extracted admission predicates2496
mutations and64 registry/copy/move rounds optimized+ASan/UBSan, plus actual bbox
foundation512 cases/12288 views/15360 stale controls. Pipeline/VU0/ABI host
shims are component evidence only. Host V1 fails because its TEST allocated
a1025th owner; V2 releases a slot before populated-move testing, no target cap
waiver. Same ELF physical303 inputs deployed by editor-network-reset, client
34280. Physical V4 subsequently FAILS after phase1 completes (10 of30 cold
head records/20 of60 raster exports), during phase2 scene reload with repeated
freepad DMA Busy. No DRIVEDONE; full reader rejects it. Last arena record has
8449KiB free, no logged allocation failure; this does NOT rule out corruption
or establish a pad-only cause. Owned client34280 stopped, no further resets.
Preserve raw failed fixture; do NOT archive as accepted or price partial rows.
Emulator success does not waive this target gate. Next independent work is
producer/body lifetime and full stale-state qualification; physical retry
requires a human power-cycle. performanceCandidate=false: NO pricing,
promotion, recovered5.6ms or60fps claim. Full stale/producer/net-cost/lifetime
qualification remains before a pricing experiment.

A35 continuation: audit actual lifetime, not another cache tweak. Cull baked
arenas register immutable spans; eviction/clear retire them into the two-bank
graveyard, and onFrameEnd frees ONLY after collectRetiredImmutableSpan permits
it. Partial A31 owners wait their raw submission sequence, retire, then collect
after actual bank leases. The new ticket holds copied bounds/model, not a
borrowed arena/bbox/package pointer. No source evidence yet for a lost fence.
Do not change these contracts on the basis of the pad diagnostic alone.

One concrete candidate bug: prepareWorldTicket's cacher.getBBoxes call was
outside its bad_alloc handler. An extracted ORIGINAL V4 producer with controlled
bbox failure terminates with uncaught bad_alloc (producer-host V2). V1 host
compile failed missing M4 assignment ABI bridge, then V2 reaches the real
failure. Move only that call/null guard inside the existing try. New private
V5 native build pending. The expanded host test extracts actual producer code,
with bbox-failure/null/body-failure/body-not-ready/unsupported/replacement and
old+new1024 cap controls; SDK/compiler remain host shims. This is safety work,
NOT evidence explaining physical V4 hang or recovering any frame time.

V5 native488 binding passes ELFd38e2039...f86e4. Bound producer-host V3 and
V4 (explicit core/owner/original-producer pins) both pass optimized+ASan/UBSan:
actual extracted producer controlled failure, state restoration, unsupported
admission, replacement and old+new1024 cap;2496 field mutations/64 registry
rounds plus bbox foundation. The36-file producer-host-v4 archive recompiles/
runs optimized READ-ONLY, full hashes unchanged. Do NOT execute the creation
driver into the archive. Hidden V5 PID46/Xvfb35 is live; full preflight pending.
V5 has NOT been staged/deployed on PS2. Human power-cycle still required.
V5 subsequently completes closed hidden six trips/60 exact rasters/30 whole
original-head oracles/47 rejecting controls. Actual moving inside+partial and
zero baseline ticket owners, all parent source/residual/quiet/travel gates pass.
PID46/Xvfb35 stopped only after DRIVEDONE+60 exports; full closed reader valid,
performanceCandidate=false, no contrasts/firstOrderGate.303 physical inputs
now staged, NOT deployed. Full target and broader stale/producer/lifetime/net
qualification remain; no recovered-time, default,60fps or impossibility claim.

A35 target admission qualification is the next SAME-lead step, after checking
this register and V5's source-only/host limitations. Add an OFF-PRICE cold
probe executing the ACTUAL renderWorldTicket entry with stale source pointers,
count/run/bbox/stamps/epoch/bufferSize/model and unsupported shape/missing owner.
Require a fresh supported ticket, immediate rejection and unchanged current
object packet qword count, then normal positive inside/partial and60-raster
gates. Temporary data restored before rendering; core.synchronizeFrame and
program reset precede probes. This is not a new frame meter/cache tuning or
a pricing claim. Initial V6 native build FAILS test accessor using nonexistent
member objectDataPacket (the writer uses a local alias). Preserve, undeployed.
V7 corrects accessor to actual packets[context]; native build pending. Reader
requires6 target admission records >=39 checks when manifest opts in, rejects5
record mutations; actual generated parsers pass1 positive/27 controls. Updated
reader replays V5 full report byte-identically, without changing old gates.
V7 native488 binding passes ELF9193dd24...a237e; hidden PID26/Xvfb15 running.
First two actual admission checkpoints each44 rejections/current packet qw
unchanged; NOT a full target gate yet. No physical V7 launch. Existing PS2
failure remains unresolved pending the human power-cycle, no repeated reset.

Coverage caveat from ACTUAL producers, not a new price hypothesis: static
batch render uses stripped=false/packageSize=0 (game_collision.gen.cpp:4403),
so A35 strips reject that class. GeoPart env passes bind envNormals as ST and
move topology with LOD (1865..1871); normal-coordinate and lighting classes are
excluded. A35 therefore does NOT remove the whole scene's frontend, and its
possible price cannot prove that unsupported EE work is efficient or a scene
ceiling. Do not broaden classes by dropping these checks; lists/normal/lighting
dispatch need their own SDK/state/source/producer/lifetime contracts first.
V7 subsequently completes CLOSED hidden six trips/60 exact rasters/30 whole
original-head oracles/52 rejecting reader controls, with6 actual44-check
admission probes (264 actual rejection calls/current head packet qw unchanged).
Normal moving inside+partial remains positive; baseline owners zero, all parent
source/residual/quiet/travel gates pass. PID26/Xvfb15 stopped after DRIVEDONE+
60 exports.303 V7 physical inputs staged ONLY, not deployed. Use V7 for next
full physical preflight after human-confirmed power-cycle. No price/default/
60fps or ceiling proof. Stop broadening/tuning hot strip policy without its
physical contrast; independent broader class/producer contracts can be audited.

Broader-class source audit (after re-reading A19/A22/A25/A29/A30/A31/A32):
in the BOUND V7 trial CoarseStrip is active BOTH hot arms. Eligible partial
strips enter fillNativeClipStrip then continue BEFORE EE expansion/refinement.
Do not propose removing that skipped expansion as a new win in this trial,
or confuse this private common policy with ordinary production defaults.
List partial ranges already use fillByPointer with VU clipping; writable
fillByCopy1By3 is EE-clipper-only. Copies still join safe cull groups, but A19/
A22 already qualify/price that boundary with only about0.28ms gain; done-index
vector searches are a previously recorded smaller bookkeeping issue. None is
a new source-backed explanation of the entire gap.

Do NOT broaden worldEligible/ticketShape by dropping guards. Existing world
source walk hashes n-2 alternating strip triangles and WorldPayload::decode
accepts2/3 streams, count<=72 and the native-strip header flag. Static LISTS
need count/3 topology, their actual SDK list header, and clipPackageSize's
whole-triangle rounded cull-capacity/6 bound (not the native-strip72 cap).
Lighting/env require actual getClipProgramByBag ABI and normal/paint semantics,
not pretending normals are ordinary ST. Source-backed next broad contract is
typed general frontend facts at actual producer/LOD replacement, immutable
geometry facts independent of live moving model, current MVP/culling/state,
existing whole-baked inside dispatch and separately qualified partial dispatch.
This is a saved CONTRACT only, not another vehicle body cache or a runtime
fix. Physical A35 price is still missing; do not tune its strip hot policy or
make bigger speculative edits simply to keep an unattended loop busy.

Stop checkpoint: physical power-cycle blocker persisted through V4 failure,
V5 producer qualification, V7 target admission qualification and this broader
source audit. All currently useful independent gates are completed; no live
emulator or ps2client remains. Next discriminating action is full V7 physical
preflight, not more counters or blind policy changes. Goal is BLOCKED, NOT
complete: no60fps/workload-preserving ceiling proof or V7 hardware price.
User goal's failure/no-independent-work power-off condition now applies.
Attempt editor-equivalent ps2client poweroff once (UDP send is NOT standby
confirmation), then schedule Windows shutdown without force-closing apps.
On resume: power-cycle PS2 to ps2link, use staged V7, revalidate sources/ELF,
full target gates first; no partial-run pricing. Devices need to be turned on.
Poweroff sent once to192.168.100.150, ps2client exit0; standby UNCONFIRMED,
raw stop logs in F:/Projects/tyrax2-world-front-20261011-stop-1. Windows shutdown
scheduled by hidden delayed PowerShell helper: sleep60 then shutdown /s /t0,
NO /f. Do not use shutdown /t60 to claim non-forced behavior: positive shutdown
timeout implies forced closing. Unsaved-app prompts can prevent this shutdown;
scheduled is not proof of actual power-off.

Current app goal is ACTIVE: stable day 60 fps with roads/live reflections,
night preferred, or reproducible workload-preserving impossibility evidence.
Main focus is excess EE work; large architectural changes are allowed.
Earlier paused-goal/no-shutdown descriptions below are historical, not current
authority. Latest goal text requires stopping below 60% remaining Codex
allowance and shutting down devices only at completion, that allowance stop,
or an actual PS2 failure with no further useful independent work. Last reading
95% remaining (5% used during A35 preflight). Useful independent work remains
after the failed PS2 run; no shutdown condition currently applies.

A27 baseline audit finds ordinary TYRA_LOG active in the bound A23..A26
lineage, although profiler clocks are disabled. A26 has hundreds of vehicle
and thousands of terrain messages across its complete run; timed-window
presence/cost needs a same-ELF gas-only gate. Original auto header close and
all renderer work remain enabled in BOTH arms. No new timers/caches; require
travel-window output evidence, byte/input oracles and exact all-arm rasters.
Do not call earlier absolute cadence a no-ordinary-log hardware ceiling.
See tyrax2-log-gate-2026-10-11/README.md. V2 now completes native binding,
emulator and physical PS2: exact all-arm images/inputs/SDK bytes, positive
baseline output and zero quiet output. Physical mean savings 1.059739 ms day /
1.162986 night, spatial 1.251112 / 1.257983; first-order gate passes, NOT 60 fps.
Archive 639 files reanalyzes byte-identically. No production promotion yet.
V1 emulator completes trips/readbacks but output-window qualification FAILS:
start marker precedes the final parked frame's ordinary output. Preserve it;
do not deploy/price it or relax zero-output gate. V2 moves start marker to the
end of offset1319, retaining gas-only gate from offset1320. Emulator qualifies
actual host-log-file opens/content, PS2 qualifies console messages.

Active larger EE source lead: vehicle camera shake is applied BEFORE the
terrain residency/LOD focus is selected from cameraLookAt. At car x=0 the
millimetre shake crosses floor-based 32-metre snap boundaries, flipping the
LOD focus between -16 and +16 metres. Archived A27 physical gas poses show
53..55 focus switches per trip vs six using actual car x/z, and baseline logs
show 403..413 buildTerrainChunk completions per trip. This is distinct from
existing half-chunk snapping/view eviction hysteresis, package cache tuning,
H02 direct producer and C05 fused snapshots. First candidate should separate
stable pre-shake terrain focus from visual shake, with normal LOD/edge contracts
and a same-ELF quiet/quiet A/B. This is now qualified below. See A27's
source finding before reopening a competing lead.
Private A28 generator now separates the pre-shake focus and uses quiet logs
in both arms. Cold held-focus equality/LOD-edge checks replace false moving
raster equality. V1 native binding (478 files), hidden emulator and physical
all-six travel/60 held-focus raster/input/SDK/25-chunk edge checks PASS.
Physical first-order savings 1.734962 ms day / 1.500039 night, equal-distance
1.940194 / 1.745236; cadence 43.04 / 37.36 Hz, NOT 60 fps. Owned clients stopped
after completion; devices powered on. Archive639 files read-only reanalyzes
byte-identically. Fresh V2 reverse native build/binding PASS (478 files),
hidden emulator and physical reverse PASS all contracts and timing gates.
Reverse mean savings 1.798894/1.602700 ms day/night, spatial 2.006395/1.663366.
Both-order archives reanalyze byte-identically; owned clients are stopped.
Production 1.173.5 enables pre-shake terrain focus in BOTH camera templates
and the generated example. Host semantic tests, Release editor, fresh FPP/orbit
target syntax and normal example native build PASS. Still NOT stable 60 fps.
Next larger candidate is the bounded VU output/native-strip contract described
in A28's deferred audit, not another cache or clipping-capacity variant.
See tyrax2-terrain-focus-2026-10-11/README.md.

A26 completes native binding, emulator and physical same-ELF moving A/B:
the standard two-qword StaPip header uses manual UNPACK count 2 instead of SDK
automatic length calculation. Actual SDK byte/state oracles, identical held
clip inputs and all 60 main/env rasters per platform pass. Geometry, clipping,
ordering and ownership are unchanged; no caches or new timing scopes.
Physical mean saving is -0.007728 ms day/-0.050117 night, equal-distance
+0.079435/-0.040736; first-order gate FAILS, no useful gain/default change.
Close fixed-header length as a larger-cause lead. Next audit must find larger
active per-bag preparation work with a concrete safe contract, not another
header micro-variant. See tyrax2-fixed-header-2026-10-11/README.md.

A25 completed on emulator and PS2: retain the original A23 StaPipCore TU and
qbuffer fills byte-for-byte, changing only final clip-program selection to cull.
Six same-ELF baseline/candidate/baseline trips cover roads/live reflections,
day/night. All cold held-pose clip-input stream hashes agree; return-control
rasters agree. Candidate rasters lose clipping as expected. Physical saved
work is NEGATIVE: -0.152587 ms day/-0.101300 night, spatial -0.158010/-0.121424;
entry cadence stays about 36.3/33.7 Hz. No useful consumer-bypass gain.
This supports prioritizing EE preparation, not a blanket proof VU/GS are free.
Do not subtract A24 and A25 across different ELFs to assign 3 ms to EE.
The historical multi-pass audit also rejects blindly sharing base/shadow/env
routes: distinct subsets/lifts and zero complete repeated geometry keys in
A02b. No production fix/default/version change. Next work stays on concrete
active EE preparation, not additional clipping/cache tuning.
See tyrax2-clip-consumer-2026-10-11/README.md.

Completed A24: user-requested complete NoClip contrast saves 2.869/3.116 ms
day/night, spatial 3.121/3.188 ms, with zero clip submissions and large raster
changes. Cadence rises to 44.27/37.56 Hz, NOT 60. This is destructive net
attribution across preparation/VU/GS/waits and changed moving views, not pure EE.
V2's refined-only grouping lacks physical activation. Corrected V3 covers
ordinary unrefined strips too, with positive wider-job witnesses and exact
main/env rasters. Its physical grouping gain is mixed: day -0.018 ms, night
+0.148 (control drift 0.229 ms); no default promotion or larger fix follows.
Close this grouping proposal rather than trying more caps/cache knobs. The
subsequently authorized same-ELF consumer-only bypass retaining original EE
clip preparation is now completed as A25 above, with no useful gain.
Full A24 protocol/results: docs/tyrax2-clip-capacity-2026-10-11/README.md.

Completed A23: twelve same-ELF full-throttle trips in both orders/day-night
pass on emulator and PS2 with roads, three vehicles and live reflections.
All 144 readbacks and moving byte oracles agree. Physical night saves
0.129..0.242 ms, but reversed day is -0.049 ms on equal-distance means and
p95 exceeds the control by 0.179 ms (predeclared tolerance 0.15).
The regression-free promotion gate FAILS; candidate stays private and version/
production defaults stay unchanged. Do not cherry-pick night or reopen the
frozen A22 result as if it were invalid. See the A19 report's A23 section.

A23 source lead, subsequently tested by A24 above: fixed sixfold clip reserve despite an existing conservative
plane mask. Refined strips merge Cull ranges but not neighboring Clip ranges.
The inspected TC many-colour class can safely group about three times more
input triangles for one active plane while retaining a 91-qword reserve.
This was an unpriced capacity proof, not a fix or an FPS claim. Preserve outer
packages/order/classification, mask-zero all-plane fallback and legacy/custom
program caps. Its bounded grouping intervention now has a mixed A24 result;
do not reopen it as untested. See the A19 report's clip-output-reserve audit.

Completed A22: user requested a concrete fix A/B, not more attribution. Review of
Setup rejected reopening already-tried clip-plane reuse and all-hit normal
preparation. Geometry still gathers contiguous 2/3-part lists unnecessarily.
The new private trial actually skips those copies on ordinary producers, with
same-ELF off/on repeats, off-clock byte oracles and actual-bypass raster checks.
A19 established eligibility only; its retained copy is not an A/B speed result.
Physical NIGHT accepts 17/18 groups, 17,040 bytes/frame. All four interventions
save 0.276..0.287 ms against both adjacent controls; all 20 rasters equal,
1152 samples, zero descriptor/state mismatches. The 587-file immutable archive
passes read-only reanalysis. Small real gain (~1%), not the whole StaPip gap.
Candidate stays private pending moving/mutable-source and lifetime checks.
See A22 result.

Earlier offline checkpoint: A19's contiguous-list oracle passes four emulator
states / 80 GS images. It identifies 11-20 redundant gathers (13,392-20,448
bytes) per checked frozen frame, not a measured speedup. Keep it private and
lower priority. The earlier offline-only hold is superseded by the user's
2026-10-10 reset confirmation and the recovery section below.
A19 audit and evidence.

Previous physical checkpoint: A21 all-ordinary frozen NIGHT split passes.
Object/bounds/state Setup 5.60..5.62 ms, Geometry dispatch/package work
6.81..6.83 ms, preparation Queue 4.04 ms, residual 2.43..2.44 ms. Both timed
repetitions agree; observer adds about 0.80 ms versus Off. Both StaPip regions
are substantial, not one overwhelming cause. Do not claim a speedup from
cross-ELF A20/A21 totals. A21 result.

Previous physical checkpoint: A20 all-ordinary frozen NIGHT split passes.
Preparation is 19.34 ms: StaPip excluding queue 12.88, queue 4.01..4.02,
unclassified 2.44 ms. Timed observer adds 0.462..0.465 ms versus Off; two
repetitions, stable counts and identical rasters. Finish queue 9.80 ms includes
completion waits, not an EE construction bill. Next narrow StaPip, not generic
cache/queue rewrites. A20 evidence.

Previous physical checkpoint: clean A18 V4 DAY now passes: ordinary
25.74..25.93, list 25.20..25.23, command 9.53..9.56 ms, equal rasters.
V5 NIGHT now passes after exact-sizing diagnostic storage: ordinary
29.22..29.39, list 28.98..28.99, commands 10.70..10.73 ms, equal rasters.
V4/V3 night failures had bad_alloc before freepad; the precursor was previously
missed. Later-view/parked hardware prices remain pending. No live-60 claim.
See the A18 resume section below. No production default or authored-map changes.

Previous checkpoint: A17's six physical day/night drives find 3,450 post-warmup
normal-preparation calls, all hits, zero roots. Synthetic direct-mapped churn
exists but does not occur in this workload; no normalization-policy fix.
Six emulator routes agree. A17 evidence.

Previous checkpoint: A16 fixes current-bag TEX1 order (288 corrected packet checks,
zero errors; 96 legacy failures). A private reflection-target reject passes
paired emulator/PS2 raster checks but six physical day/night trips show no
useful speed gain: -0.063/+0.024 ms savings against mean bracketing controls.
Gate stays private/OFF. See A16 evidence.

Previous checkpoint: A13 V2 completes all six physical EE/VU/EE day/night trips.
Integrated paint saves 1.040-1.195 ms day and 1.027-1.103 ms night versus the
bracketing controls; equal-distance estimates are 0.926/1.017 ms. Candidate
entry/present rate is 38.317/34.235 Hz, NOT sustained 60. Production stays OFF.
See physical results and overnight summary.
The user initially requested a PAUSE after this test; the A16 authorization
below now resumes bounded corrective work and subsequent measurement. Reverse preparation was stopped after one emulator
phase, with no physical deployment. The user also REVOKED shutdown permission:
do not power off either PS2 or the computer. Historical A06 hardware blockage
is superseded by the successful daytime recovery.

## Resume here: current user-approved direction

### A22 actual gather bypass (2026-10-10)

Completed as described above. One editor-equivalent reset released owned A21
client 42440 and launched fresh client 22152, still alive after full export.
No power-off and no retry reset. Frozen ordinary-source copy removal saves
~0.28 ms, with unchanged queue ownership, rasters and launch-input pins.
Next qualification for this candidate is moving/mutable-source behavior, not
more attribution. Do not count this small gain as a fix for the larger gap.

### A21 object versus geometry (2026-10-10)

User says continue. Narrow A20's 12.88 ms StaPip region using its same observer
controls and queue exclusion. Geometry starts before the existing Dispatch
marker and includes deferred packet closure; Setup includes bounds/matrices/
texture/light/object uniforms. Geometry includes cache policy, not just copies.
See the A21 section in the A20 report. Host tests pass both record layouts;
next native/emulator/reader gates, then one editor-protocol physical night.
Completed A20 owned host PID 37688 is the only client to release. No new user
reset has been confirmed; use the ordinary editor reset, not the exception.
Completed: both host layouts, 478 native files, emulator/PS2 night, 28 reader
rejections and both read-only archive reanalyses pass (586 archived files).
Fresh physical host PID 42440 stays alive; no additional reset/shutdown.
Setup 5.60..5.62 and Geometry 6.81..6.83 ms are both material; Queue 4.04 and
residual 2.44 remain. Net observer ~0.80 ms. No production changes or live FPS
claim. Next audit/select one concrete avoidable operation and qualify a bounded
intervention; do not respond with another broad set of counters or reopen
closed generic cache/inverse/clip-plane trials without new evidence.

### A20 preparation split (2026-10-10)

User authorizes separating StaPip packet construction from ownership/DMA
assembly. Use A20 scope/observer plan:
all-ordinary frozen A18 V5, repeated Off/Count/Timed/Count/Off, exclusive
StaPip/queue buckets plus residual, preparation and finalization separate.
Do not repeat the older moving queue audit or interpret inclusive waits as
pure EE work. First host nesting/wrap controls, then native/emulator/reader
gates and one editor-protocol physical night. No production fix until priced.
Completed: host ASan/UBSan, 478 native-source binding, hidden emulator,
24 reader rejection controls and physical night all pass. One normal editor
reset released completed A18 PID 17744; fresh A20 host PID 37688 stays alive.
No additional reset or shutdown. Archive has 586 verified files and both
read-only reanalyses pass. The dominant preparation bucket is StaPip 12.88 ms
versus queue 4.01..4.02, with 2.44 ms explicit residual. Both timed repetitions
agree; observer net is about 0.46 ms against Off. Next split per-object
bounds/state preparation from geometry-package dispatch/fill/command building,
keeping queue work excluded. Finish queue includes waits; do not call it EE
packet assembly. No production change or live-60 claim.

### A18 physical recovery (2026-10-10)

The user now confirms the console is reset, superseding the offline-only hold.
Resume CLEAN V4 physical pricing, not A19's gather-oracle build. Verify its
bound sources/ELF, clone into fresh evidence directories and first launch with
the documented user-confirmed-reset exception (no additional network reset).
There are no live ps2client processes at recovery. Later launches use the normal
editor protocol; stop on a wedge, preserve partial evidence and do not loop
resets. Require complete PRICEDONE, unchanged inputs and main/env rasters for
each accepted result. No shutdown permission, production/default changes or
live-game 60 FPS claim.

Clean V4 day is complete: ordinary 25.744655..25.932887 ms, list
25.197230/25.225687, commands 9.563365/9.533562; all rasters and input pins pass.
Night FAILED after stage 7 with `std::bad_alloc` BEFORE freepad DMA Busy.
Correction: V3 night's preserved log also has bad_alloc before freepad; the
earlier account missed that precursor. Do not attribute the failed starts
solely to deployment or claim a proved IOP root cause. No complete night price.

A18 V5 recovery hypothesis: the diagnostic reserves 4 MiB although accepted
closures use about 1.3-2.4 MiB. Size the validated CNT/REF/END closure first,
allocate exactly its 64-byte-rounded ownership footprint (same 4 MiB hard
limit), preserve full payload validation and ownership, and log allocator
arena/used/free-block counts only at off-clock stage boundaries. These heap
counts are NOT total available EE RAM. This may relieve memory pressure;
bad_alloc alone does not distinguish exhaustion, fragmentation or corruption.
Require host size-oracle rejection/boundary tests, native binding, all four
emulator rasters, then fresh physical controls after user recovery. V4 day
remains accepted, but do not derive allocator or sampler cost across ELFs.
Recovery completed: user reset again, V5 day/night emulator qualification
preceded a no-network-reset night launch. All four V5 emulator states now pass
(80 images); physical night passes all ten stages/1,152 samples and equal main/
env readbacks. Closure allocation is 1,992,256 bytes rather than 4 MiB, with no
allocation failure in this run. Host size oracle passes 3,120 cases under
sanitizers; reader rejects 14 negative controls. Owned host PID 17744 stays
alive after complete export; old PID 39300 was released. Nothing is powered off.
See the A18 report for immutable, reanalyzed archives and exact stage prices.

Next: distinguish StaPip packet construction from ownership/snapshot/enqueue
using a coarse disjoint split or an intermediate owned-chain intervention that
retains normal submission. First audit scope/callers/lifetimes and observer
controls. The 18.50..18.67 ms night command contrast is not memcpy-only cost,
pure EE arithmetic or a production speedup. Do not reopen generic cache tuning.

### A19 offline contiguous-list audit (2026-10-09)

The user cannot reset PS2 and requests code/emulator work only. Leave the
console and its waiting host untouched; no deployment or shutdown. A18 clean
hardware pricing remains pending, not superseded by emulator timings.

Question: does list subpackage grouping copy streams that already form one
contiguous range? `renderSubpkgs` groups accepted lists in original accepted
order, but `fillByCopyMax` / `fillByCopy1By2` always gather them into a pool.
Test a private pointer-only alternative for adjacent ranges of the SAME bag,
with every active stream contiguous and unchanged topology/count/clip mask.
Keep all existing grouping, queue snapshots, leases, barriers and clipping
capacities. This is not H02's direct packet producer, P08's command memo or
P09's expansion-cache lookup. It removes a redundant representation change,
not a safety check or a draw. Nonadjacent groups retain the original copy.

First run an actual-copy oracle: execute the original gather, compare all
resulting active stream bytes/metadata against the candidate, then submit the
candidate through the ordinary queue. Require positive activation and matching
main/reflection GS readbacks, including ordinary returns, in A18's four frozen
states. Retain the original copy in this qualification build: NO performance
claim or physical pricing from it. If activation is absent/small, record that
and do not promote it as the explanation for the A18 gap. A future counter-off
same-ELF candidate/control experiment needs physical PS2, mutable-source and
non-recording lifetime qualification before any production default change.

Completed offline: native binding (477 files), 224 actual-helper host checks
under ASan/UBSan, deliberate corrupt-copy SIGABRT and seven reader rejection
controls pass. Four same-ELF emulator states complete; all 80 main/env GS
readbacks match within their state, including ordinary returns. Day/night each
admit 20/22 gather groups (20,448 bytes); later view 15/20 (17,616); parked
11/11 (13,392). Archives and read-only reanalysis pass. The candidate only
substitutes owned descriptor arrays and retains the original copy/oracle;
neither normal producer lifetime nor a speedup is established. Keep private,
lower priority; do not mistake this modest byte opportunity for A18's whole
endpoint difference. The source audit also records repeated clip-plane math
after a failed guard-band shortcut, without reopening P03's rejected memo.
No production/default/map changes; owned emulators stopped, PS2 untouched.

### A18 approved three-path experiment (2026-10-09)

The user approved ordinary scene / owned descriptor list / owned command
stream comparison instead of another fine-grained timer census. Follow the
A18 plan and gates. First qualify
one frozen driving state with actual main/env GS readbacks; only then broaden
coverage and price the three routes. The old 23.33/10.73 ms V7 result is a
validation-policy contrast WITHIN terminal replay, not ordinary versus replay.
Do not transplant its hardcoded program/startup capsule into the current game.
Production defaults and authored maps stay unchanged. Never power off either
machine. A18 V5 descriptor replay passes all four main/env emulator images
after pinning automatic interleave order. Physical PS2 also passes all four
main/env images and checked descriptor comparisons (104 draws / 108 events).
Same-ELF all-ordinary emulator controls pass. The command continuation now has
an interim daytime physical three-route result: ordinary 25.62..25.84 ms,
list 25.21..25.23 ms, commands 9.53..9.56 ms, equal main/env rasters. These are
frozen synchronized costs, not live FPS. Clean V4 suspends an inherited outer
drive sampler; all four emulator states now pass. Its final physical rerun
and hardware coverage of those states remain pending.
V3 night is incomplete after freepad DMA Busy; one editor-equivalent reset did
not recover PS2. User was asked to power-cycle. Owned waiting PID 20552 belongs
to command-price-20261009-v4/physical/editor-deploy.json; verify before reuse.
No shutdown authorization. All owned emulators are stopped. The physical
directory is failed launch evidence; stage a fresh one after recovery.

### A17 optimization overhead audit (2026-10-09)

The user asks whether optional EE optimization costs exceed the work saved.
Do not respond by enabling every historical candidate or removing DMA safety.
Current priority is a newly identified mechanism, not a measured frame win:

- `StaPipCore::ensureEnvNormalsUnit` executes before whole-bag visibility.
  The default-ON `TYRA_VU1_EXP_ENV_NORMALIZED` moves normalization from VU
  to EE, claiming once-per-array work. Its 32-slot direct-mapped table uses
  `(address >> 4) & 31`. Stable arrays 512 bytes apart collide; each can
  evict the other's normalization stamp on every alternating submission.
  Every miss walks all normals, including a sqrt/divide for nonzero normals.
  The first-four-only log cannot establish that later frames have no work.
- `tools/verify-env-normal-churn.py` extracts the actual function body and
  observes sqrt calls in a C++17 ASan/UBSan host harness. Two 16-normal arrays,
  100 frames: distinct slots = 32 calls; colliding slots = 3,200 calls;
  distinct slots with legitimate content mutations = 3,200 calls. All checks
  pass. Source SHA256:
  `51b7af70c4ad04b5bdea0da2e9bc5c3e95db4995c7a1fbbb803c369a0d99c00b`.
  This proves the mechanism, NOT actual map collisions, PS2 cost or raster
  equivalence. Floating-point re-normalization is not necessarily bit-idempotent.
- Next gate: an unpriced real-workload census of normalization misses,
  normals visited, slot conflicts versus actual version/count changes, and
  whole-bag rejection after normalization. Export outside measurement windows.
  Include day/night, driving and turns; do not log each miss over ps2link.
- Only if that census establishes recurring work, test normalization at the
  producer's actual normal-stream mutation boundary (including LOD/rebuild),
  or the complete legacy EE/VU policy pair. Never disable only EE normalization
  while retaining a VU image that assumes unit normals. Preserve stream stamps,
  DMA ownership and retained capture basis; qualify pixels before physical
  off/on/off pricing. Existing historical wins for this flag remain evidence
  for their workloads, not proof against this collision mechanism.

Secondary source findings: list `renderSubpkgs` builds two index vectors and
linearly searches the completed list after copying safe groups together. This
bookkeeping is simplifiable, but `isGuardBandOnly` itself is just a bool read,
not another plane test. Copy-removal changes dispatch/order economics and needs
its own contrast. Treat this as lower priority than possible per-vertex EE work.
Whole-bag baked validation loops are compile-time diagnostic paths, OFF by
default; do not mistake them for ordinary per-frame work. Matrix/clip-plane
micro-optimizations already have H07/P03 negative or negligible results.

Runtime follow-up: A17 census
uses a source-bound private clone of A16 with the rejected target gate OFF.
Host observer accounting and all six routes on BOTH hidden PCSX2 and physical
PS2 pass. Physical post-warmup: 1,440 parked/turn calls plus 2,010 driving
calls, ALL hits, ZERO normalized vertices/roots. Four misses / 10,344 roots
occur per phase only in warmup. No unchanged-key repeats or rejected-after-
normalization bags. Close recurring normalization as the explanation for this
workload; do not promote a cache/normalization fix. The synthetic weakness
does not establish its frequency in other layouts. No intrusive timing claims.
V1 failed linking diagnostic counters and was never launched; V2 defines them
in the common probe header. Production remains unchanged. No power-off permission.

### A16 approved correction and measurement plan (2026-10-09)

The user explicitly approved addressing A14/A15 findings and then measuring
where the renderer stands. This supersedes the daytime audit-only pause, not
the prohibition on shutting down either machine. The app goal still contains
the obsolete overnight objective and is paused: create_goal refused replacement
because it is unfinished, and the exposed update_goal API cannot edit objectives
or resume them. Do not falsely complete it. This section is the current work plan.

Objective: reduce unnecessary EE preparation at its source while preserving
rendering and DMA correctness; qualify individual changes before combined
physical PS2 pricing. Stable 60 Hz remains the eventual target, not a promised
outcome of this batch. Preserve authored maps and unrelated edits.

1. Reproduce current-bag TEX1 filter lag using actual emitted commands, fix
   ordering, and cover alternating nearest/linear, replay/partial routes and
   batch boundaries. Treat as correctness, not an assumed performance win.
2. Qualify an additional conservative reflection-target OUTSIDE test before
   texture/light/packet work. Preserve existing guard-band routing for accepted
   bags and test edge/near/far geometry, target/display aspect and view restore.
   Count actual rejected submissions and verify raster before speed claims.
3. Audit visible reflection demand and capture pass necessity against their
   consumers. Do not suppress companion passes, reuse bases, or enable existing
   coarse proxies without proving semantics or explicitly reporting quality loss.
4. Preserve established ownership, synchronization and clip output budgets.
   Reopen no cache/zero-copy/packet-fusion dead end without new evidence.
5. Once qualified, run same-workload controls/candidate/controls on physical
   PS2 with roads, reflections, day/night driving and camera turns. Keep VU
   paint policy fixed across the contrast; report mean/p95 work, presentation
   rate and coverage, not pure-EE claims. Use editor-equivalent deployment.
6. Archive source/ELF/input identity, correctness evidence and failures. If
   hardware wedges, do not loop resets; retain partial evidence and continue
   useful offline work. Never power off the PS2 or computer.

Status: item 1 is implemented/qualified (1.173.4). Item 2's private gate passes
isolated emulator/PS2 raster checks, but matched physical pricing rejects its
promotion: no useful gain, with worse night tails. All six complete trips and
bound evidence are in A16. Each sampled driving frame still submits a vehicle
environment pass; blanket no-consumer capture suppression is not supported by
this workload. Item 3 remains an audit/qualification task, not an enabled policy.
The larger objective is NOT complete. Do not combine unqualified fixes or
restart this gate with arbitrary tuning; require a new producer-level mechanism.

### Packet walkthrough A15 (2026-10-09)

The user requested the full EE preparation path and a comparison with intended
responsibilities before another change. [A15 walkthrough](tyrax2-scene-work-audit.md#a15-ee-packet-preparation-walkthrough-2026-10-09)
separates scene submissions, VU ranges, command packets and frame transport;
records direct replay versus partial-strip work, ownership and synchronization;
and lists hazards for A14's target-matching rejection. It also identifies TEX1
serialization before `setInfo` applies the current bag's filter. This is an
unfixed source-level correctness finding, not an FPS explanation or target
reproduction. Qualify alternating filter modes and batch/replay boundaries
before moving state setup. No build, runtime edit or deployment in this audit.

### Daytime source audit A14 (2026-10-09)

After pausing hardware tests, the user requested inspection of reflection
call sites before measuring again. [A14 source findings](tyrax2-scene-work-audit.md#a14-reflection-call-site-audit-2026-10-09)
identify square 110-degree projection versus deliberately widened 154-degree,
main-display-aspect classification planes. This admits off-target work; actual
scene coverage, safe target-matching rejection and physical price are unproven.
It is distinct from A03/A04's slice-bound tightening. Prefer an additional
conservative OUTSIDE gate first, without changing accepted guard-band routing.
Also confirmed: capture demand counts loaded resources, not visible consumers;
ordinary terrain companion passes enter capture; the reflected-object loop
lacks the main loop's coarse precheck. The reuse-budget-zero dead work is not
active here (budget is 4). No new runtime change or deployment.

Important correction: reflectionGroundProxy ALREADY implements a coarse ground
stand-in and is disabled in this authored map. Older documented outdoor gains
come with changed reflected shading and are not current TyraX2 prices. Do not
propose implementing this feature again or enable it silently. The seven model
proxies already exist too. Power-off remains forbidden; hardware testing stays
paused after this source review.

### Overnight autonomous campaign (2026-10-09)

Historical authorization below: the later daytime request explicitly revokes
step 6's shutdown permission and restores availability for hardware recovery.

The user explicitly authorized continued evidence-driven cycles without replies
or physical resets. Persist each plan/result before moving on. An active Codex
goal tracks the campaign. Do not confuse an emulator's speed with PS2 success.

1. A04: preserve A03 sky decisions but prepare exact slice boxes in sky geometry
   construction. Validate parent-relative boundaries, versions, rebuilt storage,
   fallback and cold equality to A03; then same-ELF turn/drive A/B/A on PS2.
2. If A04 helps, qualify moving rasters, scene reload and day/night; otherwise
   close this implementation lead rather than tuning arbitrary caches.
3. Reconstruct remaining dominant road/night preparation work from existing
   source-bound evidence. Pick one new mechanism with a falsifiable intervention,
   separating per-bag state, pass multiplication and first-use preparation.
4. Implement bounded private candidates, run host/native/emulator checks, deploy
   with the editor protocol, analyze and archive both wins and regressions.
   Repeat only while there is a new justified mechanism, not the same matrix.
5. Success requires sustained nominal 59.94/60 Hz on physical PS2 through camera
   turns and full-throttle driving with roads, reflections and all three vehicles,
   including night, without simulation slowdown or unqualified visual omissions.
   Verify tails, pacing, cold/revisit behaviour and output correctness, not just
   an average FPS counter. No degraded scene is a success substitute.
6. Only after verified success, preserve all evidence and safely power off PS2
   and the computer as requested. Do not discard unsaved unrelated work.
   If hardware wedges, do not loop resets or ask the absent user; retain logs
   and continue meaningful offline work. Report actual external blockers.

Completed overnight A04: precomputed sky bounds are also NOT promoted.
PS2 turn off/on/off work is 15.298425 / 15.540215 / 15.345551 ms;
48.166 / 47.846 / 48.166 Hz. The same 16,734 -> 6,126 clip-call reduction
still regresses work by 0.195-0.242 ms. Removing the input-vertex scan did not
make the policy profitable. Different ELFs do not isolate that scan's price.
Six physical/emulator phases, complete prepared-hit coverage, zero misses and
cold equality checks pass. Close sky refinement for now, no further micro-tuning.

Completed A05 road geometry audit: 66 unchanged chunks / 26,835 vertices on
both targets, but float payloads differ between PCSX2 and PS2. Four single-run
chunks (zero-based 23..26, diagnostic IDs 24..27) admit piecewise affine
longitudinal reduction under a 1e-5 world-space surface error bound. Emulator
234 -> 39 vertices, physical 234 -> 66. Only 168/26,835 input vertices disappear
on PS2: do not portray this as the whole bottleneck. It includes hot road ID24,
not the other hot ID34. Triangle-intersection checks cover the full UV domain;
GS clipping/depth/raster agreement is still unqualified. No drawing changed.

A06 bounded draw-only reduction of those four chunks is implemented privately.
Derive/validate selection against ACTUAL runtime positions/UVs, not emulator-only
indices; a fixed private union needs independent checks on both target streams.
Keep original ProcChunk contact/receiver arrays and identity/bounds intact.
Use owned immutable draw arrays with the existing BagArray binding/lifetime
contract, never swap a bag pointer transiently before deferred submitHeavy.
Hold VU paint constant, sky refinement OFF, and other roads untouched. Require
original/reduced stream witnesses, complete target consumption, host surface/UV
oracle, visual/depth qualification and same-ELF turn/drive A/B/A. A small win
does not establish 60 Hz. A06 V1 passes host/native and all six emulator phases:
the union of both targets' retained stations is 234 -> 72 vertices, with a
full UV-domain surface error check on each input set. At construction the
runtime requires exact XYZ/W, STQ/W and RGBA agreement with one whole captured
platform stream before binding separate owned draw arrays. Emulator turn work
falls by 0.185-0.192 ms and drive by 0.128-0.193 ms, NOT physical PS2 prices.
PS2 completed phases 0 and 1, then repeatedly reported `freepad: DMA Busy`
while loading phase 2. The complete-run analyzer correctly rejects this capture:
no DRIVEDONE, no second turn control or driving phases. Do not publish a PS2
optimization price from the two available phases. One editor-protocol launch
was used; no retry/reset loop. The owned host server remains alive for recovery.
Stationary candidate/control image differences exceed the off/off temporal
control; geometry proof is not visual qualification. A06 is NOT promoted.
Additional A06 gate: XYZUV equivalence does NOT preserve nonlinear per-vertex
dynamic lighting or fog automatically. Road batch info can pick dynamic lights;
the colour VU program evaluates their contribution at the surviving vertices.
Night and fog output need explicit shader-attribute/raster qualification, or a
correct eligibility fallback. Do not switch off lights/fog to manufacture a win.

Next offline direction: qualify C16's private VU paint path, which already has
repeatable physical gains of about 1.9-2.1 ms in both orders. This is NOT another
performance remeasurement. Close target-assembled output, mode/fallback and
lifecycle obligations before touching production defaults. Start with a bounded
deterministic emulator EE/VU/EE output test that holds accepted paint basis,
geometry, camera and reflection texture content equal. Include ordinary cull and
near-plane clipping, scene/LOD rebuild and mode transitions. A stationary full
scene screenshot with independently advancing animation is not that oracle.
Audit custom-program/legacy-clipping fallback separately. Hardware qualification
remains blocked by the A06 IOP stall; do not reset the absent user's console.

A07 first bounded implementation: an isolated StaPip paint raster harness,
using the same private target-compiled C16 shader code. Render deterministic
geometry with varying normals, a fixed generated texture, fixed camera and
three accepted bases in EE/VU/EE order; cover inside and clipping cases. Read
the completed CT32 GS buffer through the existing synchronized libdebug capture
protocol, preserve raw RGBA and actual qbuffer route counts. Reject empty images,
missing cases, failed readback and off/off drift. This first step qualifies
shader output only, NOT scene lifecycle, gameplay performance or fallback.

A07 completed (emulator only): V2 renders all 27 cases with nontrivial output,
exact EE/VU RGBA equality for all nine groups and exact EE/EE controls. Actual
near-crossing cases submit four clip qbuffers across four draws; inside and
view-edge cases submit one cull qbuffer then reuse it. Native source/ELF binding
covers 468 files. V1 is rejected because its harness omitted TerrainGame's
clipMargin initialization, leaving clipping cases entirely blank; the analyzer
caught this. V2 reproduces the game's initialization BEFORE setRenderer.
Readback is completed CT32 GS output, not a window screenshot or host shader
simulation. This is still a bounded synthetic shader test, not vehicle-game
lifecycle, physical raster, night/fog or production qualification.

Next A08: preserve accepted paint basis across EE/VU mode changes. The private
C16 generator only refreshes paintForward/paintHalf inside paintChanged and
only on the VU branch; a mode switch with unchanged quantized key can therefore
reuse stale EE colours or an uninitialized VU basis. Prior phase reloads conceal
that transition. Define and test representation readiness independently of the
visual paint acceptance gate: fallback must materialize the LAST ACCEPTED basis,
not silently accept today's camera basis or relax hysteresis. Geometry/LOD
identity and custom-program/legacy-clipping eligibility must invalidate the
appropriate representation. Integrate privately and test actual lifecycle
before considering production defaults. Hardware remains stalled; no resets.

A08 implementation in progress: a private State retains accepted basis and
tracks EE colour readiness separately; normal source/count/content stamp also
invalidate representation, and the distinct-normal map now checks that stamp.
The acceptance gate and quantized hysteresis remain unchanged for held geometry.
Every 30 frames the diagnostic uses EE, VU, real legacy-clipping fallback, VU,
real program-override fallback, VU. Authored LOD tiers are forced every 240
frames; normal three-phase scene reloads remain. An expensive every-vertex EE
colour oracle and accepted-basis bit checks deliberately invalidate any timing
interpretation. Host ASan/UBSan passes 6000 transitions and stale-colour control.
V1 generation stopped on an incorrect include anchor before build/deploy; V2
uses game_runtime.gen.hpp. No production defaults or authored maps changed.

A08 V2 completed native/emulator qualification: 468 source files bound; all
three phase/reload exports valid. Visits 4000/4062/4062, zero oracle errors,
held-key EE materializations 22/20/20; all six modes and both available authored
tiers exercised. Each phase includes 662 legacy fallback and 660 override
fallback submissions. TMDL inspection confirms Ravager/Pica/Strix have only one
far tier each; requesting tier 2 clamps to tier 1. Unused vehicle files with
two far tiers must not enlarge this test's coverage claim. Six malformed-log
controls reject missing completion, transition, fallback, LOD or oracle success.
No timing or new physical gain claim. Screenshots cover all three phases and
the first was inspected; no full-scene pixel-equivalence claim.

Next A09: audit the production integration surface for C16 + A08 (engine bag,
capability query, object uniforms, shader generator, generated GeoPart and paint
loop). Preserve existing dirty work and all legacy/custom fallbacks. Add the
actual implementation only with focused generator/host/emulator tests, including
night/fog and rebuild paths. Do not enable a new default based solely on this
synthetic raster or intrusive lifecycle test. Physical requalification remains
unavailable while the console repeats freepad DMA Busy; no reset loop.

A09 implementation: the generator now owns an explicit private paint emitter,
not part of the built-in catalogue or runtime defaults. All 45 ordinary output
files are byte-identical before/after; rebuilt editor --vu-check passes.
8,400 host draws match C16, including TC/TD shared-image paths, with three
specular-zero negative controls. The scalar oracle still sees <=0.000396729
error and identical tested GIF words. The target test selects separate named
whole images, retaining current FOLD/ENVN bodies, with the generator's prepass
transplanted into each. TC, TD and TCE wrappers must choose the SAME clip image.
A09 V3 completes all 54 nontrivial GS readbacks across fog off/on: all 18
EE/VU groups and their EE/EE controls are exact RGBA, and fog changes all 27
matching pairs. First 27 also match the older A07 raw pixels exactly. Native
binding covers 473 sources; linked images are Cull TCE ENVN PAINT and ONE
shared Clip TC FOLD PAINT. V2 is rejected by the unchanged >128 unique-RGB
gate: strong fog reduced two view-edge bases to 110/115 colours. V3 extends
the fog end from 12 to 24 near distances; it does not weaken the oracle.
Final generator ASan/UBSan and editor --vu-check pass. No game night, physical
timing or 60-Hz claim. No production bag, uniform or game-template integration
is enabled yet. Both owned emulator processes are stopped, PS2 server retained.

Next A10: implement the engine opt-in capability and A08 accepted-basis lifecycle
behind a whole-image compile-time gate that defaults OFF. Preserve all baseline
images byte-for-byte and TC/TD/TCE shared ownership. A capability query must
cover build flag, VU clipping, MVP, normals-as-coordinates, many colours, no
lighting/billboard/custom override; the send path must not silently run stale
EE data when it rejects paint. Integrate BOTH generated GeoPart definitions
and the paint loop, including normal-stamp invalidation. Qualify disabled
output, enabled native build, live EE/VU/legacy/override/LOD/reload transitions
and actual night with roads. Keep heavy correctness runs separate from timing.
Physical requalification is still blocked by the same A06 IOP stall; no reset
or shutdown and no default promotion until real qualification is possible.

A10 engine integration is in progress, separating the public engine contract
from the generated-game lifecycle step. `TYRA_VU1_EXP_PAINT` defaults to zero:
paint fields, checks and uniforms compile out; original VCL files remain
unchanged. Four separately generated whole images cover raw/ENVN cull and
raw/FOLD shared clip. All TC/TD/TCE wrappers select the same clip image.
The capability additionally checks the env resident-class bit: a program can
fall back even without overrides. Reject non-HIGHLIGHT2 or missing streams.
An invalid requested paint draw asserts in debug and is skipped under NDEBUG,
never silently reusing stale EE colours. The caller still owns correct fallback.
Native enabled/disabled raster tests will exercise the actual public API and
eligibility changes before generated-game integration. Unit normal and accepted
basis contracts remain explicit; do not promote defaults from this harness.
A10 engine contract is now qualified in both builds: 17 runtime capability
checks plus null rejection, 54 exact fog-off/on readbacks per build, and all
pixels equal A09. Native source/ELF bindings cover 471 files each. Disabled
ELF has no PAINT image symbols; enabled retains one shared Clip TC FOLD PAINT
and Cull TCE ENVN PAINT. Host checks compile the actual capability/guard under
ASan/UBSan, reject a removed NDEBUG-return guard, and preprocess all 32 wrapper
combinations. Four actual-image numerical/GIF oracles and rebuilt --vu-check
pass. The 15 original cull/clip VCL files remain byte-identical. This is engine
API/output qualification, NOT game lifecycle or a physical speed measurement.

Next A11: integrate A08 accepted-basis/EE-readiness state in BOTH GeoPart
templates and renderEnvPass behind the engine flag. Set HIGHLIGHT2 before the
capability query, clear stale paint requests for non-paint draws, and preserve
the last accepted basis across VU/EE transitions without relaxing hysteresis.
Normal source/count/stamp and in-place rebuilds must invalidate state and the
distinct-normal map. Use the new public API, not another injected capability.
Qualify both game templates, default-OFF output, scene reload/LOD/override/
legacy transitions and actual night with roads. Historical C16/A08 generators
have exact pre-A10 source anchors; do not run them against the new API or weaken
their anchors to make an unrelated build pass. Use bound historical snapshots
for their reproduction and a fresh integration harness for A11. PS2 remains
stalled; no physical success, reset loop or shutdown is authorized by evidence.

A11 implementation now separates accepted paint basis from EE-colour readiness
in both GeoPart templates under `TYRA_VU1_EXP_PAINT`. The loop clears stale VU
requests, queries the public API after setting HIGHLIGHT2, and invalidates on
normal source/count/stamp changes. EE fallback uses the stored accepted basis;
normal-map invalidation includes the content stamp. Existing in-place rebuild
invalidation forces reacceptance and palette materialization when required.
The actual template block passes 6,000 host transitions (381 acceptances,
185 held-basis refreshes), including source/count/stamp/LOD/rebuild changes and
non-paint draws. Three bad map-stamp/basis/readiness mutations fail. Default-OFF
preprocessor tokens and both GeoPart paint declarations match the before copy.
Native night/road lifecycle now passes: 471 bound files, ELF
`623dd458b0674c55c8ce9903a0f7b01a459ac4397e4a5d980d5c31ea541ac698`, three
complete reload/sweep phases, 12,124 visits, 62 held-basis refreshes, zero errors.
Each phase confirms midnight, 66 road chunks and eight lights. All six modes
and both authored tiers appear; nine parser mutations are rejected. The target
compiler also accepts fresh FPP/orbit physics TUs/headers with flag 0/1.
The vehicle-playground example is regenerated; no authored setting changed.
In-place rebuild/source edits are host tests, not injected target cases. This
run does not include full-throttle driving or paired whole-scene GS readbacks.
See A11 evidence.
All 673 archived files verify against SHA-256. Owned emulator/Xvfb are stopped;
authored full/no-road manifests retain their original hashes, and A06's physical
CSV remains unchanged at 578,112 bytes / 02:05:20 with its host server alive.

Next A12: qualify the same integrated path during deterministic full-throttle
driving, including accepted basis under moving/rotating object transforms,
day/night roads and reloads. Keep scalar/readiness checks, require actual travel
and final completion rather than reusing the parked-sweep labels or timing parser.
No timing conclusion from intrusive checks. Hardware pricing/default promotion
still require recovery of the physical PS2; the user is absent, do not request
input or loop network resets. Retain the host server and do not shut down.

A12 is staged from A11's verified 471-file binding, with no production source
changes. It restores full throttle/releases the handbrake, retains camera
sweeps and forced fallback/LOD, alternates authored day/night/day mood, and
requires z>=108 before the 1,200-row capacity (rather than ending at the parked
sweep's fixed frame). Record paint visits specifically while speed exceeds 5,
split EE/VU, and validate CSV route/gas/camera coverage. No new engine hooks or
timing conclusions; this is the missing moving-object correctness case.
A12 V1 completes all three trips with zero paint errors, but the inherited
observer scans the entire dynLights array rather than [0,dynLightCount), so
the return-to-day reports eight stale enabled slots. clearDynLights resets
only the live count; consumers honor that count. This is an observer bug, not
proof that night lights remain rendered. V2 counts the active prefix and
requires zero day lights/nonzero night lights; retain V1 and rerun qualification.
A12 V2 passes that rerun: 511/489/507 rows, over 182 units of actual travel per
phase, camera sweeps about -60..+60 degrees, 0/8/0 active lights and 66 roads.
Paint records 8,981 visits with zero errors; moving visits are 1,138 (541 VU,
597 EE). Both authored LODs/all six modes appear; ten lifecycle and five route
negative controls reject malformed evidence. Three pre-drive screenshots are
smoke checks only. See A12 evidence.
All 1,039 archived files verify, including both 471-file bindings. Both owned
emulator runs are stopped; the authored map hashes and stalled physical CSV
are unchanged. No physical deploy or production default changed during A12.

Next A13 is preparation for hardware recovery, NOT another emulator price:
derive a lean same-ELF EE/VU control from the integrated game, remove every-
vertex diagnostics and forced fallback/LOD, restore ordinary VU clipping and
normal LOD policy, and bind a fresh native build. Keep roads, live capture,
three cars, authored day/night and deterministic full throttle/camera sweeps.
Validate control selection and complete travel in the emulator; stage a fresh
physical bin but do not deploy while the A06 host/IOP remain stalled. Then the
remaining speed/default decision genuinely requires the physical target; do
not invent further unpriced microfixes or weaken the sustained-60 criterion.
A13 V1 built and bound but was NOT launched: current-engine staging inherited
TYRA_HARDWARE_TRACE=1 (runtime trace was not armed). V2 explicitly compiles both
hardware trace and frame profiler out, and its analyzer checks those source
definitions instead of trusting the historical hardcoded DRIVEINIT label.
This is a private measurement-build correction, not a production default change.
A13 V2 binds 470 files and ELF
`996b459f73ca13173e741c373cef57b9fd459262e92dd38ab82b3cfb40b46162`.
Six complete emulator trips produce 3,799 rows (3,079 driving): all requested
paths execute, live captures remain, standard LOD/clipping replaces A12's forced
states, and nine malformed-data controls reject incorrect evidence. Source
and symbols confirm no paint oracle or hardware trace. The paint loop matches
production apart from declared gates/counters. No emulator prices are emitted.
Physical stage: `F:/Projects/tyrax2-paint-qualification-20261009-v2/physical/bin`,
303 pinned files; no deployment record exists. See A13 evidence.
All 1,003 archived files verify. Both A13 native bindings are retained; V1 was
never launched, V2's owned emulator/Xvfb are stopped and final analysis matches
its final log. Source, skill-twin and Python syntax checks pass.

Provenance correction: A12's archived final log includes a post-completion tail
written after its original analysis. The immutable files/CSV/source remain
unchanged and the archive hash manifest is valid; the report's embedded log
hash was stale. A13 host/ preserves a full reanalysis of that immutable archive,
identical except for the finalized log hash. A11's hash already matches. Stop
owned emulators before final analysis; the archive writer now enforces this.

Blocked audit after A13: the same external condition has persisted across A06
and more than three consecutive continuations, including A11/A12/A13. PID 13224
still identifies the owned A06 ps2client, its log ends with freepad DMA Busy
ID=3c4d0002 / SB_STAT=00f00160, and the 578,112-byte CSV remains at 02:05:20.
Bounded offline correctness/integration and a lean physical fixture are ready.
Further performance/default decisions require the actual PS2; unpriced cache
or geometry changes would not answer the question. Preserve the file server,
do not reset in a loop or ask the absent user, and mark the goal blocked rather
than repeatedly promising another measurement. No success or shutdown claim.

Daytime recovery (2026-10-09): the user physically reset PS2 and revoked all
shutdown permission. Reverified all 470 A13 V2 source hashes and 303 staged
inputs, then deployed ELF 996b459f... through the editor-equivalent helper.
The previous owned A06 client (PID 13224) was replaced with PID 3748; the new
game initialized its pad and completed the first three daytime road trips.
This supersedes the historical hardware blocker above. Require all six phases
and the physical analyzer before quoting a price. Prepare a separate reverse
VU/EE/VU fixture with the same generator and ordinary scene policy; qualify
its native binding and emulator controls before a second physical deployment.
Keep original archives immutable and preserve this hardware evidence separately.

Daytime result: all six physical phases pass, with 2,598 rows / 1,878 driving,
live captures, ordinary LOD/clipping, 66 road chunks, 18 pins and three cars.
Day EE/VU/EE work is 18.658/17.618/18.812 ms; night 20.867/19.840/20.942 ms.
VU p95 work is 24.932/27.311 ms: no stable-60 claim. Nine malformed controls
are rejected. Archive snapshot reanalysis reproduces every result; only the
log hash changes for the post-completion tail, and both reports are retained.
The user requested a pause while reverse qualification was being prepared.
That native build binds 470 files, only mode order differs, and emulator phase0
completed before intentional shutdown of the owned emulator/capture/Xvfb.
No reverse physical launch or complete reverse qualification. Keep owned PS2
file-server PID 3748 alive; no power-off or automatic continuation. The next
gate after explicit resume is reverse order, then remaining physical visual
and whole-image/default-off checks, not another speculative microfix.
The separate physical archive verifies all 823 files. Its complete archived
fixture passes the same analyzer independently of the live host. Authored full
and road-free map hashes remain unchanged. Skill twins match after the standard
path/instruction-file substitutions; no owned emulator/capture/Xvfb remain.

A05 source mechanism: the builder already reduces
lateral spans, but its collapsed path still emits two vertices per station
along the road. Capture actual positions AND UVs plus chunk/strip boundaries,
then determine whether straight affine longitudinal spans contain redundant
samples. Do not assume flat-looking roads are affine, relax tolerances blindly,
or change road collision/light-receiver data. This is not P04/P05 grouping:
keep chunk identity/visibility and investigate the geometry inside each chunk.
Only a demonstrated surface/UV-preserving opportunity justifies a private draw
candidate, followed by raster/depth checks and same-ELF PS2 pricing.

Completed A03 (2026-10-09): the sky-only on-the-fly tight list-bounds trial
is NOT promoted. Host, cold emulator/PS2 witnesses and six-phase accounting
pass, but fewer clip calls did not buy faster frames. PS2 turn off/on/off:
15.107914 / 15.349806 / 15.123478 ms work, 48.166 / 47.952 / 48.166 Hz.
Sky clip calls fall from 16,734 to 6,126 across 720 turn frames, yet the
candidate costs 0.226-0.242 ms more. Driving has no demonstrated gain either.
The proposed follow-up was exact slice bounds prepared with geometry;
A04 above has now tested and rejected promotion of that implementation too.
Do not repeat A03 unchanged, enable it globally, or infer that clipping is
the whole bottleneck. Details and limits: [A03](tyrax2-scene-work-audit.md).

Confirmed by the user on 2026-10-08: prioritize **A01, scene/work amplification**.
The working question is "why do we request this much work?", not "how can we
cache or execute each small operation faster?" The user suspects a larger
preparation/design problem, comparable to the ground-shadow asset work found
inside rendering. Treat that as a hypothesis to investigate, not a proven EE
bottleneck or permission to assume that every slow frame has the same cause.

After compaction or handoff, read this section and A01 before taking action:

1. Reconstruct a few representative stationary, turning and driving frames as
   object/chunk -> view -> material/pass -> source geometry -> classification
   reason/clip plane -> qbuffers -> vertices/bytes -> transport. Distinguish
   necessary work, duplicated work and work whose necessity is still unknown.
2. Reuse the existing source audit and P06/P07 evidence first. Do not claim the
   parked-turn evidence already covers full-throttle driving. Record missing
   fields explicitly; collect only the bounded snapshots/events needed to fill
   those gaps, then analyze them offline.
3. Focus on actual clipping need versus conservative box routing, multiplication
   across reflection/companion passes, overlapping surfaces, and frame-triggered
   geometry rebuilds, allocations, texture acquisitions and uploads. Low target
   resolution or few authored objects does not imply little EE preparation.
4. Produce a concrete ledger explaining where the work grows BEFORE selecting
   a fix. Any new measurement must distinguish named explanations and have a
   decision gate; another aggregate render timer or FPS-only matrix is not enough.
5. Only then bound the identified operation's cost and change its policy or
   representation, preserving drawing, simulation and resource-lifetime rules.

Do not resume cache/TTL/hash-size/group-size tuning by default. P08's command
memo is rejected (PS2 regression); P09 was stopped on user redirection after
cold emulator checks, with no completed price or physical deployment. Neither
is a pending production fix. A cache proposal needs new evidence from A01 that
it addresses the identified cause, rather than merely making excess work cheaper.

Preceding source evidence: A02b's nine-frame StaPip audit passed host, emulator and
PS2 validation. Physical capture contains 1,027 bags / 892 actual clip calls,
zero unknown owners and zero overflow/accounting errors. Tight boxes of the
actual clip inputs could route 351 calls safely without clipping and reject
183: 534 opportunities in these selected frames, NOT a 60% speed gain.
311 overlap the known road-strip lead; **223 are outside roads**, including
132 sky calls. Terrain base, already refined, has zero tight-box opportunities.
The warm repeated turn has no new-body misses but 172 actual clip calls;
44 come from reflection sky alone. See [A02b](tyrax2-scene-work-audit.md).

The new source mechanism is list-slice bounds: VU clip inputs use roughly
max/6 vertices, but classification merges old max/3 boxes, including vertices
outside the input slice. A03 tested that mechanism for sky: conservative
reclassification works geometrically, but the per-frame implementation loses
on PS2. A04's precomputed follow-up also loses; that branch is now closed.
Full moving raster/depth qualification remains open. No production fix yet.

Preceding A02b physical V4 ELF `98cc8ea98d4dbca424a334af3083ce589fc31cfaa6e3585eb09b4b9ce458059e`
started on the first editor-protocol launch and completed all six phases.
303 launch inputs and both original maps stayed unchanged. Owned host and
emulator processes are stopped; console remains at the completed test.
The protocol is in both testing skills and `tools/tyrax2-deploy-ps2.ps1`:
one reset, exact cleanup/marker/settle, keep live on silent log; no retry loop.
Frozen evidence: `docs/tyrax2-scene-capture-2026-10-09`. V3 is separate
superseded emulator evidence (missing shadow labels), never PS2 evidence.

Previous completed step: A01 includes an offline reconstruction of 72 P06
physical road frames (2,220 bags), per-view/ID work and three direct-route
first-build outliers of 1.47-2.06 ms. See [the scene work audit](tyrax2-scene-work-audit.md).
These outliers are not clipped; later same-angle cycles replay in 0.05-0.15 ms.
This is NOT a controlled bake price or a reason to resume cache tuning.
Logical VU chunk counts include derived whole-bag replay, not EE allocations.
The full current-scene stationary/turn/driving lineage is NOT yet complete;
A02b's owner/view/pass/plane capture is completed above, with event and
non-StaPip coverage still open. A02a
implements a bounded first-build event slice on unchanged P06 road windows:
`tools/tyrax2-first-build.py`, host checker and analyzer. Emulator completed
all three phases and 2,220 records with four additional negative controls.
Physical first boot wedged before gameplay with freepad DMA Busy; preserved
under the private fixture's `physical/rejected-freepad`. The user restarted
physically and the same ELF was launched bare, without another network reset.
Physical retry completed all phases, 2,220 bag records and all parser gates.
Three large first-use road events spend 0.879-0.910 ms in stream bake/finalize
and 0.378-0.434 ms in the bounds prefix, without scratch growth. Whole-turn
observer off/on/off is 15.854457 / 16.063458 / 15.886539 ms; no fix was tested.
See the scene audit and 529-file frozen A02a archive for source-bound evidence.
No audit fix has been implemented. Owned host/emulator processes were stopped;
PS2 was left running the completed test, not reset or shut down. Confirm its
state before another deployment; retain the rejected freepad boot history.
Keep the user's original maps untouched. This section takes priority over
historical "next step" wording in earlier experiments and archived reports.

## Established attribution, not the next optimization prescription

The strongest remaining lead is **EE-side preparation and delivery of road
packets**, especially per-bag state preparation and chain submission. P01 now
prices road package creation/classification at only 0.033 ms per turn frame;
do not keep treating that operation as the dominant current road cost.
This is a supported direction, not an identified bad line
of code or an exact pure-EE time measurement. Input DMA/VIF and scheduling remain
in the latest interventions.

P02 splits preparation rather than finding one dominant operation: bounds/view
0.574 ms, object preparation 0.418 ms, texture/program 0.258 ms, lighting/fog
0.127 ms and head 0.064 ms. Exact road clip-plane memoization (P03) then saves
only 0.023-0.035 ms and is closed. Reducing road submissions with immutable
draw groups (P04) does give a mean gain in both PS2 orders, but has order-sensitive
magnitude and worse maximum work. Smaller pairs (P05) retain a mean gain but
also regress maxima in reverse order. Both policies remain private.
Do not mistake either candidate for an integrated stable-60 fix.

The decisive current sequence is a daylight parked-car camera sweep on the
full authored map, with private VU paint enabled and normal reflection captures:

| ID | Intervention | Physical PS2 result | Decision |
|---|---|---|---|
| R01 | Keep road physics/storage/receivers; omit accepted road submissions | 15.425 / 12.463 / 15.405 ms; 48.166 -> 59.609 Hz | Road drawing path has about 2.94-2.96 ms net cost. Not a fix: asphalt disappears. |
| R02 | Keep road submissions and VU work; use XYZF3 to suppress primitive kicks | 15.496 / 15.487 / 15.434 ms; about 48 Hz | No useful gain. Do not prioritize road texture/fill-rate optimization on this evidence. |
| R03 | Keep no-kick in all arms; bypass VU vertex work and most GIF output | 15.428 / 15.403 / 15.363 ms; about 48 Hz | No useful gain. Prioritize preparation/input delivery, not another VU arithmetic rewrite. |
| R04 | R01 at night, with eight active lights and unchanged road receivers | 18.752 / 15.502 / 18.809 ms; about 32.5 -> 47.9 Hz | Road omission saves 3.25-3.31 ms, but night still adds 3.039 ms with roads not drawn. Night passes remain a separate problem. |

All four are completed PS2 interventions with emulator/validation evidence.
R01/R04 have identical sampled day/night poses and 302 matching non-ELF assets.
R03 retains actual road uniform uploads and changes the selected VU mode only.
Do not subtract timings from these different ELFs to publish additive stage
costs. A/B/A values are elapsed work excluding measured pacing, not raw CPU
instruction time. These are parked camera turns, not full-throttle driving.
Source, limits and hashes: [reflection-turn report](tyrax2-reflection-turn.md).

## Completed recent experiments

Status meanings: **Integrated** = enabled production path; **Private** = useful
or bounded result, not enabled; **Closed** = no useful gain in the tested scope;
**Attribution** = diagnostic only; **Rejected** = invalid experiment/evidence.
Savings from separate rows must not be added without a combined test.

| ID | What was checked | Result and scope | Status / do not repeat without a new reason |
|---|---|---|---|
| C01 | Cache misses during vehicle-free rotation | 192 new misses and 192 expirations, no sampled key mismatch or budget eviction; 2.42 MB live against 4 MiB | Attribution. Not evidence that the cache is too small. [Census](tyrax2-motion-census.md) |
| C02 | Longer baked lifetime, 250 -> 1000 frames | Rotation saves about 0.307-0.309 ms; qualified driving does not establish it as the moving-vehicle fix | Private; do not re-propose TTL as the general solution. [Census](tyrax2-motion-census.md), [drive](tyrax2-full-throttle.md) |
| C03 | Freeze shared reflection capture while driving | Existing comparisons isolate only part of moving-frame cost; do not explain it all by capture | Attribution, superseded by more specific paint and geometry tests. [Drive](tyrax2-full-throttle.md), [reflection isolation](tyrax2-road-free-reflections.md) |
| C04 | Partial preparation and explicit waits; exclusive partial-path breakdown | Selected partial work, expansion/copy and submission already measured; elapsed scopes include stalls | Attribution. Read these before adding another generic Core/partial timer. [Drive breakdown](tyrax2-full-throttle.md#exclusive-partial-path-breakdown) |
| C05 | Fused snapshot/native submission and one-search immutable lookup | Fused route regresses; one-search mixed/incomplete, combined arm not completed | Closed/incomplete, not an accepted optimization. [Submission trials](tyrax2-full-throttle.md#submission-fixes-2026-10-07) |
| C06 | Reuse partial baked bodies and strip expansion | Combined counters-off driving gain 0.514/0.536 ms; qualified together | Integrated in 1.173.1. Do not propose enabling them again. [Combined qualification](tyrax2-full-throttle.md#combined-geometry-reuse-qualification-2026-10-08) |
| C07 | Remove duplicate recorded env-map barrier | No useful gain; recorded ordering command is not an ordinary EE FINISH wait | Closed. Synchronous fallback and other barriers are not cleared. [Clip/barrier trial](tyrax2-full-throttle.md#clip-expansion-and-reflection-ordering-follow-up) |
| C08 | Prepare and pin static-batch geometry at load | No useful physical gain, near-full cache residency; six routes complete | Closed for this policy. Not a rejection of every load-time architecture. [Prepared static](tyrax2-full-throttle.md#preparedpinned-batch-result-2026-10-08) |
| C09 | Cache static-batch package descriptors / exact fixed-view decisions | Tiny/mixed differences in three fixed views; only one partial bag per view | Closed for tested batches; roads/terrain/moving views were not covered. [Static plan](tyrax2-static-plan.md) |
| C10 | Attribute real Core calls to producer and view, including deferred draws | Terrain: 3.496 ms in car-free rotation, 70% of clip chunks, 11.46 inputs/chunk; road main Core about 1.60 ms in that older workload | Attribution. Not a simultaneous total or current road-only price. [Producer report](tyrax2-producer-attribution.md) |
| C11 | Conservative partial-strip classification for terrain | Qualified host/target oracles and motion tests | Integrated in 1.173.2. [Terrain strip ranges](tyrax2-strip-subranges.md) |
| C12 | Road-free driving hitch: PNG ground-mask acquisition inside render | Localized 29.568 ms acquisition; complete small shadow sets preloaded | Integrated in 1.173.3, <=512 KiB policy. Do not blame that hitch on VU dispatch. [Hitch report](tyrax2-road-free-hitch.md) |
| C13 | Road-free straight driving after integrated fixes | Three routes at about 59.94 Hz, no >25 ms entries, max work about 15.60 ms | Qualified scope only; does not certify turning, roads or night. [Hitch report](tyrax2-road-free-hitch.md) |
| C14 | Freeze vehicle paint generation during parked turns | Strong gain; includes mutable-stream/cache effects, not only colour arithmetic | Attribution; leads to C16. [Turn report](tyrax2-reflection-turn.md) |
| C15 | Scatter/memcpy paint update | No repeatable useful mean gain | Closed. [Turn report](tyrax2-reflection-turn.md) |
| C16 | VU input paint prepass preserving accepted basis/quantization | About 1.90-2.07 ms road-free gain in both PS2 orders; about 1.85-1.90 ms at night with roads | Private, NOT default. Needs production generator/fallback/lifecycle/GS-raster qualification. [Turn report](tyrax2-reflection-turn.md) |
| C17 | Same-source road x night factorial | Both factors matter; runtime removal of road geometry shifts ride/camera by 0.12 | Attribution with explicit confound; R01/R04 remove the height confound. [Factorial](tyrax2-reflection-turn.md) |
| C18 | Enable partial-strip refinement on road bags | 0.258-0.388 ms in both orders; still 48.166 Hz; separate range oracle passes | Private, not main road cost or production-qualified. [Road refinement](tyrax2-reflection-turn.md) |
| C19 | One constant colour for ordinary road/junction bags | -0.003/+0.040 ms; unchanged FPS | Closed. Spill/edge varying alpha must remain. [Colour trial](tyrax2-reflection-turn.md#constant-colour-road-trial) |
| C20 | Repack roads for 90-input single-colour capacity | First 0.071-0.090 ms; overlap-aware 0.159-0.192 ms, still about 48 Hz | Closed, not promoted. Byte-identical triangles/ST checked; no more packing variants without a materially different mechanism. [Repacking](tyrax2-reflection-turn.md#repacking-follow-up) |
| C21 | Burnout debugger comparison | Reusable subchains / compact input formats are useful architecture references | Reference only, NOT a PS2 timing or equal-workload proof. [Bounded conclusions](tyrax2-full-throttle.md#burnout-reference-bounded-conclusions) |

## Earlier work that must be consulted

These existing reports predate the current road-isolation sequence. They are
indexed here to prevent rediscovery; their precise fixtures, elapsed scopes,
observer taxes and acceptance gates remain in the originals. They are not
silently repriced on today's map or revalidated by this consolidation.

| ID | Existing work / recorded decision | Evidence |
|---|---|---|
| H01 | Whole-scene terminal preparation and sealed replay. Different completion/backpressure; not a pure GPU timer. | [Scene isolation](tyrax2-scene-isolation.md), [EE/VU0 audit](tyrax2-ee-vu0.md) |
| H02 | Direct-producer route: corrected activation and exact rasters, but physical regression; not promoted. | Route record, overlap |
| H03 | Whole-frame/Core, stripped/list work and dispatch-tail clocks already measured. Do not sum across apparatus versions. | [Selected scopes](tyrax2-minimal-scopes.md), frame/Core, prefix/submit |
| H04 | Road allocation inventory was NOT copy traffic: original-road copied bytes were zero. | Road owner census |
| H05 | Snapshot/native pass breakdown and immutable-borrow lookup; no robust lookup gain. | Snapshot breakdown, borrow trial |
| H06 | Bounds/package/texture/program/light/object-data partition and five-part bounds follow-up already exist. No one bounds region dominates the older pose. | [Core prefix partition](tyrax2-core-prefix-partition.md) |
| H07 | Matrix-key comparison unresolved; clip-plane optimization failed real PS2 bits; corrected variants regress or are mixed. | [Core preparation experiments](tyrax2-core-preparation-experiments.md), same-call reuse |
| H08 | Inverse/key math, local-light choices and object-data breakdown; memoization gains small/mixed. | Inverse census, [night shift](tyrax2-night-shift.md), EE math |
| H09 | Interleave benefit measured; previous mixed N/N-1 scorer labels are not valid current-frame attribution. | [Interleave](tyrax2-interleave.md) |
| H10 | Player-only light receiver diagnostic saves time but removes lighting participation; far-light gate regresses. | [Receiver experiment](tyrax2-player-light-receivers.md), [far-light gate](tyrax2-far-light-gate.md) |
| H11 | Night producer observer: selected flashlight scopes inactive; beam/corona work small. Eleven-box batching falls back because lights differ. | [Night producers](tyrax2-night-producers.md), [main batching](tyrax2-main11-batch.md) |
| H12 | Sky RGB-only retint retention and paused-clock work already investigated. | [Sky retint](tyrax2-sky-retint.md), [paused clock](tyrax2-paused-clock.md) |
| H13 | Validation/TEX1, adjacent reuse and PMU experiments have prior physical gates and limitations. | [Proposal trials](tyrax2-claude-trials.md), [EE source audit](tyrax2-ee-performance-audit-2026-10-05.md) |
| H14 | Empty-map buildup and batching contrast: batching helps strongly, but authored Vehicle Playground already enables it. | Buildup, both batching orders |

## Rejected comparisons and operational traps

- Removing authored road references invalidated ground-shadow bakes: zero-pin
  trials are rejected, not accepted as a faster road-free control.
- The user's separate No roads project also differs in vehicle metadata. It is
  useful for reproduction, not an exact one-variable control of the full map.
- Repacking v1 failed compilation and was never deployed. Its build log is a
  failure record, not a measured result. A later corrected fixture has another ELF.
- Early driving reload warmup was insufficient. Use the qualified protocol, not
  the pilot delta. A no-start network launch is not a renderer runtime failure.
- Emulator elapsed times and Burnout save-state debugger observations are not
  physical PS2 prices. Screenshots are not exact raster equivalence tests.
- Nonzero frame-profiler settings can introduce drains. Ordinary experiments
  keep these disabled; sparse clocks need their own observer controls.
- Roads are generated at scene load. Road height queries already have an index.
  Flat/affine lateral span reduction, plane caching and geometry reuse already
  exist. Do not propose them as missing without inspecting their actual use.
- Deferred road work runs inside the object loop. Preserve producer ownership
  until execution; timing only `renderRoadChunks()` misses that work.
- Source audit: do not mistake the guarded `FlushCache(0)` in
  `stapip_qbuffer_renderer.cpp` for a flush on every submitted road bag.
  `TYRA_VIF1_QUEUE_LAZY_FLUSH` defaults to 1 in `vif1_queue.hpp`; that branch is
  disabled in these fixtures. Native-prefix writeback still exists. This is a
  control-flow finding, not a measured zero cache-maintenance cost.
- Use the editor-equivalent owned-listener network reset and ten-second settle.
  Keep automated PCSX2 hidden unless the user needs to inspect it.

## Active next step

**P01 - Road packet preparation audit. Status: completed on host, emulator and
physical PS2. Attribution only; no production change.**

Question: which repeated road-only preparation operation remains expensive
after suppressing both raster and VU vertex/output work?

Before another deploy, compare the current road path against C04, C09, C10 and
H03-H08. Identify a concrete operation and why existing measurements do not
answer it. Prefer a narrow source-backed intervention or selected-road scope
to another whole-scene profiler. Keep geometry, poses, captures, cache defaults
and synchronization unchanged. Separate clock/census qualification from a
candidate's physical timing. Input transfer is still an alternative explanation.

Decision gate: a repeatable substantial selected operation justifies a fix;
tiny/mixed results close that candidate. Do not treat an empty/inactive scope
as proof that the subsystem is free. Do not promote diagnostic omission modes.

Source audit: package arrays already use grow-only pools; whole-visible bags
can replay a whole baked stream, while partial strips retain classification and
per-slice dispatch even when expansion data is cached. Submission snapshots
the chain, leases immutable inputs, then emits native recording references.
The counted snapshot already avoids a separate sizing pass. Reintroducing
pools, immutable borrowing or fused submission is not a new proposal.

The older C04 driving ledger priced create/classify at about 0.72 ms and
exclusive submission plus chain/payload copies at about 2.01-2.03 ms. These
are whole-workload partial scopes, NOT today's road-only costs.

Executed contrast: normal daylight road rendering, constant private VU paint,
clocks off/on/off. Tag road bags at construction so deferred execution retains
ownership. Measure selected Core, its dispatch tail and nested exclusive
create/classify/fill/packet/submit/copy buckets. This reuses the old scope
definitions, but selects actual road calls in the current rotating workload
after integrated terrain fixes. No raster, VU, geometry or synchronization
omission. Price observer overhead against both clock-off controls. Submission
and copies triggered inside a road call can include a mixed pending packet;
label them road-triggered, not proof of road-only payload bytes. Explicit wait
subtraction is not added; elapsed scopes are not pure CPU instruction time.

Fixture: `F:/Projects/tyrax2-road-prep-day-20261008-v1`. ELF SHA256
`623d195b3560c555aa53b97ca79ac77cbbdfec41353d59537594e99fda739530`.
Physical clock-off/on/off work: **15.707472 / 15.879180 / 15.790464 ms**;
cadence **48.166029 / 47.792674 / 48.166054 Hz**. Enabled-clock overhead is
0.089-0.172 ms relative to the two controls. Common compiled probe overhead
is not priced. Do not subtract a uniform observer tax from individual buckets.

Selected-road elapsed means in the clock-on turn (720 frames):

| Region (inclusive) | ms/frame |
|---|---:|
| Entire road Core call | 2.873287 |
| Dispatch tail, all routes (historical CSV `partialTicks`) | 1.421719 |
| Core before that dispatch scope, by subtraction | 1.451568 |

Nested exclusive buckets, a DIFFERENT partition of those same Core calls:

| Operation | ms/frame |
|---|---:|
| Package record creation, excluding classification | 0.015946 |
| Package classification | 0.016645 |
| Instrumented qbuffer fill/copy | 0.008443 |
| Packet construction, excluding nested children | 0.135469 |
| Queue submission, excluding measured copies | 0.621381 |
| Chain/payload copies | 0.055261 |
| Core remainder outside these six buckets | 2.020144 |

Do not add the two tables: they overlap. A submit can flush pending work even
before dispatch, so bucket totals cannot all be assigned to the dispatch tail.
On average there are 31.492 selected Core calls, but only 2.215 package-create
calls per frame. The clock-off controls have zero scope values. All three phases
retain 568 paint updates and 1440 vehicle body submissions; capture counts are
174/179/174 as in the existing apparatus. Same camera trajectory, 66 road chunks,
18 shadow pins and zero active daylight lights pass the gates.

Qualification: 467 source/native-mirror files and ELF agree; 303 staged inputs
retain their hashes, both original user project manifests are unchanged. Host
tests exercise the actual clock header (off/unselected controls, prefix and
nested buckets, balanced scopes, wrap and recursion rejection) and ownership
injection sites. Both targets complete 2520 rows and reject 14 parser negative
controls. Emulator work is 7.571/7.662/7.582 ms, all 59.94 Hz; these are NOT
hardware prices. Middle-arm screenshot retains asphalt, with no obvious new
corruption; it is not a pixel-equivalence oracle. Owned emulator/host processes
are cleaned up; network reset status is archived, not visually verified.

Evidence: physical analysis,
emulator analysis,
host checks,
source binding,
archive hashes.

Decision: close creation/classification as the MAIN road bottleneck for this
specific sweep. Prioritize repeated per-bag preparation BEFORE dispatch, then
the queue bookkeeping and unassigned dispatch work. R01's omitted-road delta
and this Core price are similar, but different ELFs/observers prevent claiming
an exact identity or that the remaining cost is pure EE arithmetic. No fix is
qualified by these clocks alone.

**P02 - Repeated road state preparation. Status: completed on host, emulator
and PS2; attribution only.**

Start from P01's 1.452 ms pre-dispatch region, not another whole-scene timer.
Audit bounds/transform keys, texture residency, light selection and object-data
preparation against H06-H08. Seek a reuse contract for unchanged road data while
keeping per-view transforms, texture state, lights and mutable uniforms live.
Roads share info bags with other procedural geometry: never alter the shared
descriptor as an ambient road-only switch. If a selected prefix split is still
needed, state exactly which cache/fast path its result would accept or reject.
Do not revive the rejected generic fused-submit change merely because Submit
has a visible price. Any candidate needs counter-off A/B/A, output equivalence,
and view/lighting/lifetime checks before production promotion.

Planned P02 discriminator: extend the same selected-road off/on/off apparatus
with five disjoint pre-dispatch regions: head/validation; bounds plus model/view
and package setup; texture residency plus program selection; light/BLSS/fog
selection; object uniforms plus remaining preparation. `--prefix` creates it,
`--road-prefix` validates the additional columns. This is not a repeat of H06's
old whole-scene stationary night workload. Bounds dominance directs an exact
versioned prepared-bounds/transform candidate; object-data dominance directs
uniform-template reuse; texture/light dominance requires inspecting actual
residency/selection before caching. Reject speculative fixes in cheap regions.
The first prefix fixture (`...road-prefix-day-20261008-v1`) failed during
generation on Windows' implicit cp1250 decoding after repeated UTF-8 writes.
It was never built or deployed. V2 uses explicit UTF-8 for this generator and
UTF-8 mode for its child generators; this failure is not a renderer result.

V2 physical off/on/off: 15.957117 / 16.079275 / 15.915799 ms,
48.112253 / 47.581880 / 48.166029 Hz. Observer delta: +0.122158/+0.163476 ms,
including scheduling/adaptive-order effects; common compiled footprint unpriced.
Disjoint selected pre-dispatch means: head 0.064460, bounds/model/view/package
setup 0.574318, texture/program 0.258130, light/BLSS/fog 0.126599, object-data
and route preparation 0.418126 ms. There is no single 1.45 ms operation.
These five regions sum to 1.441633 ms; their clock gaps are not redistributed.
Both targets complete 2520 rows and 16 parser rejection controls; host tests
also cover prefix stage transitions, off/unselected scopes and early exit.
ELF `43aa6c323d079e735731219a5bc25332ad328f8427afd39daa3aa6f50043625c`,
467 source/mirror files verified. Evidence: P02 archive.
Next: test the P03 exact-key reuse contract in the largest region. Avoid
reopening tiny head/validation tweaks. Automatic interleave remains enabled
in P01/P02, so their observer deltas are not merely clock instruction costs.

**P03 - Exact-key road clip-plane memo. Status: closed, negligible physical gain.**

P02's completed clock-on PS2 phase localizes about 0.574 ms in bounds/model/view
preparation (full control series still being finalized when this candidate is
registered). Source `computeClipObjectSpacePlanes` recomputes eight planes for
each requesting bag, even when multiple roads share identical MVP inputs.
Unlike H07, this reuses results ACROSS road calls, does not specialize arithmetic,
and pins interleave to 2 in ALL arms to remove the old adaptive-order ambiguity.
Do not compare its absolute times to P01/P02's automatic-interleave ELFs.

Private candidate: one Core-owned road slot keyed by exact MVP and computed
near/far bits; non-road calls do not evict it. All eight original planes are
copied on hits. Mutable matrices, camera/reflection views and clip distances
must miss when their bits change. Cold offsets 1000..1099 recompute every hit
with the original body and compare actual-target plane fields bit-for-bit.
Timed windows have no nested clocks or oracle work. Host mutation/alias tests,
positive cold hit/check witnesses, ordinary drawing and counter-off PS2 A/B/A
are required. A tiny/mixed gain closes this candidate; never promote on a host
math oracle alone or bypass the original key checks.

Completed PS2 off/on/off: 15.343796 / 15.320404 / 15.355499 ms, all 48.166 Hz.
Savings only 0.023392/0.035095 ms: not promoted. Cold controls report 1600 calls,
zero hits/checks; candidate reports 1600/1600/1600. Every target comparison is
bit-identical. Host ASan/UBSan checks pass 2000 mutation sequences against the
original function, including a corrupted-cache negative control. Both targets
complete 2520 rows and 13 parser negative controls. Emulator contrast is mixed
(-0.012686/+0.005883 ms); not a hardware price. ELF
`64ddda9e424e7d60f2099f177e79f88ab12495ae66afd2a8e3329b06afcace99`.
Evidence: P03 archive.

**P04 - Draw-only road grouping. Status: private gain, tail regression; not promoted.**

P01/P02 show repeated per-bag overhead, while P03 eliminates a concrete repeated
calculation for negligible gain. Test reducing the NUMBER of Core calls instead.
This differs from C20 (strip packing inside unchanged bags) and C08/C09 (static
batch cache policy, not road grouping). Build immutable draw-only groups of up
to four consecutive compatible opaque strip chunks. Keep original procChunks,
physics, receivers, per-chunk distance/frustum acceptance and primitive order.
Use a group only when every member was individually accepted; otherwise draw
original members. Existing vertices/ST/colours must concatenate byte-for-byte
on complete strip-run boundaries. Dynamic lighting/BLSS fall back to originals.
Both arms allocate the same group storage and pin interleave to 2; only drawing
selection changes. Require fewer actual submissions, equal accepted/submitted
input counts, unchanged poses/pins and target geometry witnesses before timing.
This private daylight test does NOT qualify new clipping routes or GS pixels;
any useful gain needs additional output/lifecycle validation before promotion.

P04 v1 is excluded: its complete-run eligibility guard found ZERO groups in
the emulator, so no PS2 deployment occurred. Road tail runs are only padded to
a multiple of three, not a full 75. V2 pads BETWEEN concatenated chunks by
repeating the prior chunk's last XYZ/ST/colour, verifies every added item on
target, and accounts for padding separately. Original stream bytes and real
triangle order remain unchanged; submitted inputs equal accepted originals
PLUS explicit degenerate padding, not the original raw count alone. Price this
extra VU input work as part of the candidate, never hide it as equal workload.
Source review also found that `blendingEnabled` defaults true even on ordinary
opaque-alpha road material. V2 was superseded BEFORE build/deploy: V3 preserves
that material state and requires every source vertex alpha to be 128 instead
of treating the blend-enable bit as evidence of transparency. Spill/edge chunks
remain excluded. This is not a proof about texture alpha or GS raster identity.

V3 emulator completes all 2520 rows and 13 parser rejection controls. Cold
windows retain 3200 accepted bags and 1213500 original vertices in every arm;
the candidate submits 2600 bags with 6000 explicit padding vertices (200 merged
groups). Thus the measured cold reduction is 18.75% of road submissions, not
all road calls eliminated. Host ASan/UBSan dispatch checks pass 512 visibility
and gate combinations. Load-time target byte comparisons pass. Screenshot
inspection finds roads still drawn, but is not exact raster equivalence.
The private generators and host checks are explicit `.gitignore` exceptions
so the reproducible apparatus is retained with this register.

Forward PS2 off/on/off: 15.227332 / 14.939064 / 15.271691 ms, or
48.166054 / 48.273776 / 48.165963 Hz. Savings are 0.288268/0.332627 ms;
candidate maximum work rises to 24.440 ms from controls 21.954/21.828 ms.
This is not a stable-60 fix. All 2520 rows and 13 parser rejection controls
pass on PS2; cold submission/input witnesses agree with the emulator. ELF
`9ef3d974e57ff9898129d949fa2f0ba359edaecf20ca6c357a02e157c8d1ce78`,
467 source/mirror files verified. Before changing grouping policy, repeat
on/off/on using generator/analyzer `--reverse` to reject arm-order bias.

Reverse PS2 on/off/on: 14.915226 / 15.660989 / 14.932208 ms,
48.327921 / 47.739782 / 48.219816 Hz. Both directions favor grouping, but
reverse savings 0.745763/0.728781 ms differ substantially from forward
0.288268/0.332627 ms; do not publish a precise isolated per-bag price. Candidate
maxima 25.999/25.653 ms exceed the middle control 21.825 ms. Both targets again
complete 2520 rows and 13 parser rejection controls, with matching cold
geometry/submission witnesses. Native agreement covers 467 files and ELF
`af13ccff7c71ea7b8019ddec41a0d4c548f91a5f15071ce3c056ca4016132dce`.
This is sufficient to try P05, not to promote the four-member policy.
Evidence: forward,
reverse,
rejected zero-group v1.

**P05 - Road grouping coverage with pairs. Status: private gain, tail regression; not promoted.**

P04's four-member policy reduces cold submissions only 18.75%, because a group
is usable only when ALL original members are accepted. Keep that correctness
gate, but generate pairs (`tyrax2-road-groups.py --pairs`) instead of groups up
to four. This is a different coverage tradeoff, not another clip-math cache.
Both arms still build the same immutable group storage; original inputs,
physics and per-chunk acceptance remain unchanged. Require greater cold
submission reduction than P04 before interpreting this as improved coverage;
reject a negligible/mixed counter-off PS2 contrast. Do not compare absolute
times across ELFs or presume smaller groups are faster. Only deploy after
P04 reverse-order confirmation retains a useful gain. Raster, lifecycle and
night qualification remain prerequisites before any production promotion.

Emulator cold candidate witness passes the coverage gate: 23 built pairs,
3200 accepted bags -> 2400 submissions (25% reduction), 1213500 original
vertices plus 12600 explicit padding vertices. P04 was 2600 submissions with
6000 padding. More coverage therefore also costs more padding; do not pretend
this isolates only the number of Core calls. Host dispatch checks pass 512
cases; native binding verifies 467 files and ELF
`680c6d624e74840a8ecd9c5aa765768e4c462cb29a59bce6b38a246c9e9af34f`.

Forward PS2 off/on/off: 15.156803 / 14.863259 / 15.219264 ms,
48.166034 / 48.219836 / 48.165929 Hz. Savings 0.293544/0.356005 ms;
candidate max 20.722941 ms versus controls 21.761902/21.729716 ms. Both targets
complete 2520 rows and 13 parser rejection controls. This order avoids P04's
tail regression but does not establish a materially larger mean gain. Confirm
on/off/on before retaining the pair policy as a private candidate. Do not
continue arbitrary group-size tuning without a new mechanism: 25% fewer cold
submissions still leaves this workload around 48 Hz.

Reverse PS2 on/off/on: 14.681206 / 15.206119 / 14.759944 ms,
48.819852 / 48.112333 / 48.764691 Hz. Savings 0.524913/0.446175 ms confirm
the mean direction, but candidate maxima 23.217828/23.101271 ms exceed the
middle control 21.005771 ms. Thus the forward-only tail improvement does NOT
generalize to both orders. Both targets complete 2520 rows and 13 parser
rejection controls; the cold counts match forward exactly. Native binding
verifies 467 files and ELF
`55612657f9ca8db955f406852e18f8c263a361d1e131c4817a52c22d835e1d4d`.
Forward/reverse source manifests differ only in the probe mode sequence header
and unused Docker path configuration (also true of P04).

Decision: retain the pair prototype and evidence, but do NOT enable it in
production or claim stable 60 Hz. Arbitrary group-size tuning is closed.
Before promotion, localize the repeatable early-turn tail regression and prove
actual target raster/state equivalence, bounded storage and mutation/scene/view
lifetime handling. Grouping changes clipping routes, draw interleave and cache
identity as well as Core-call count; the measured gain is not a pure call-tax
number. Dynamic lighting currently falls back, so night performance is not
qualified. No production renderer/codegen behavior changed in P01-P05.

Evidence: pairs forward,
pairs reverse.
All accepted physical P04/P05 deployments retain 303 unchanged staged input
hashes. Owned hosts/emulators were stopped; final network reset returned 0
(ps2link screen not visually verified). Both original user project hashes remain
unchanged. The rejected P04 v1 never ran on physical PS2; v2 was never built.

**P06 - Original-road windows and whole-frame wait leaves. Status: qualified, dispatch lead.**

Registered before execution: baseline original roads, constant VU paint,
fixed interleave 2, observer off/on/off. P01/P02 did not measure waits outside
selected road Core calls; P06 records explicit non-pacing wait leaves across
the entire sampled frame and per-road/view counters in eight-frame windows at
turn offsets 12, 24 and 180, repeated over three cycles. Stable original chunk
IDs follow deferred execution; no grouping or renderer optimization. Native
submissions, actual nonempty prefix flushes, capacity/fallback causes and wait
entry points are separate counts. Export RAM ledgers only after a sample phase.
Reject overflow, nested wait accounting, missing windows/ownership, pose drift,
or inactive observer. Price observer effect in the same ELF. Explicit waits
are not every EE stall, and a wait can complete the previous frame; road-triggered
queue copies can contain mixed owners. Require reproducible baseline bad frames
and a specific differing operation before proposing a one-variable intervention.

V1 generation failed a source-anchor multiplicity assertion and was never built
or deployed. V2 distinguishes VIF and GIF capacity paths and barrier/arena retry
reasons. Host actual-header tests pass off-clock, view/ID, per-bag counter,
wrapping clock, nested-wait rejection and overflow controls with ASan/UBSan.
V2's inherited reflection fixture omitted the optional partial-replay and
cached-expansion hooks although their columns existed. Its zero values are
UNOBSERVED, not zero activity. Do not accept V2 as complete cache attribution.
V3 wires those actual paths explicitly; source checks and positive runtime
activity requirements reject the former blind spot. V2 can retain only its
independently qualified wait/transport and ordinary dispatch evidence.
Do not require positive partial command replay on every road workload: clipped
expanded strips do not qualify for that cull-only replay path. V3's cache gate
requires installed source hooks plus positive expansion and some command replay;
zero partial-only replay is legitimate when those hooked paths are ineligible.

V3 completes 2520 main/frame-ledger rows and 2220 road records on both targets;
16 baseline and six trace rejection controls pass. Native binding verifies 467
files and ELF `62a84ed0aa8a7a869f5001e79be62da240676273b42388884f7fcea93cbebc25`.
Physical off/on/off work is 15.554310 / 15.745781 / 15.594152 ms, with
48.166030 / 47.792668 / 48.166137 Hz. Observer net perturbation is therefore
+0.191472 / +0.151630 ms, not a free observer or an isolated clock price.
Selected road Core averages 2.711779 ms during turns. Whole-frame explicit
non-pacing waits average 0.004249 ms (VIF 0.002520, GS 0.000474, other
0.001255); maximum across all 840 observed frames is 0.044305 ms. Each frame
has one nonempty native prefix flush, with zero observed capacity/fallback
flushes, drain calls, barrier flushes or arena retries. This does not exclude
EE memory/cache stalls, preemption or uninstrumented downstream effects.

Repeated heavier windows have 2.79-3.30 ms road Core versus 2.01-2.05 ms in
the lighter windows; explicit waits stay around 0.004 ms. Main-view road IDs
24/34 each issue 16 clip chunks and 192 expanded vertices from cache, together
costing about 0.69-0.73 ms in repeated heavy windows. Partial baked replay is
ineligible here; it serves cull slices, not this expanded clipping route.
Do not infer a missing geometry cache or price pure EE time from this span.
V3 screenshots captured only phase 2 (late helper); V2 captured all three.
Neither is exact raster equivalence. P07 tests the localized dispatch lead.
Evidence: accepted V3,
superseded V2.

**P07 - Two clip-heavy road chunks, destructive attribution. Status: measured, not a fix.**

P06 identifies original road IDs 24 and 34 (66 vertices each) repeatedly issuing
16 clip chunks apiece. V2's independently observed physical Core times total
about 0.76 ms in the main-view window starting 1572, despite much larger nearby
bags costing less. P07 keeps per-bag preparation and every other road unchanged,
but omits ONLY their final partial-strip expansion/clip-dispatch loop in mode 6.
This is not R01's omission of all road submissions and is NOT an optimization:
selected road triangles disappear. Price counter-off off/on/off, constant VU
paint and interleave 2; require exact ID/count/strip identity and positive cold
eligible/skipped witnesses. Count repeated-position degenerate triangles off
clock to inform a later representation hypothesis, not to silently remove them.
Proceed only if P06 V3 confirms active partial/expansion cache accounting.
Reject a negligible/mixed gain; never promote this omission or equate its elapsed
difference with pure EE instruction time.

Both targets complete 2520 rows and 12 parser rejection controls. Native binding
verifies 467 files and ELF
`a52c6f3ed9a285bae6b2865540f5a979af875bf92a57d92ba0a917fabbf5853d`.
Host ASan/UBSan checks pass 50 actual gate cases. All cold arms have 100 eligible
calls / 6400 triangles; only the candidate skips 100. ID 24 has no repeated-position
degenerate triangles and ID 34 has two. This is not mostly empty geometry.
Emulator screenshots cover all arms; candidate inspected, not raster equivalence.

Physical off/on/off work: 15.165731 / 14.717206 / 15.124383 ms, saving
0.448525 / 0.407177 ms. Loop rates: 48.166030 / 48.764609 / 48.166031 Hz.
Max work: 21.694797 / 19.741869 / 21.559435 ms. Emulator gain is
0.327179 / 0.338312 ms, not the hardware price. Reflection captures differ
174/179/174, so the contrast includes scheduling/workload consequences and
omitted downstream rendering, not just EE dispatch instructions. Only the
forward order was measured; no precise general per-chunk cost is claimed.

Decision: the selected path has a repeatable adjacent-control cost, but removing
it does not approach stable 60 Hz. Do not ship omission or continue tuning
arbitrary group sizes. The bounded next candidate is reusable VIF command bodies
for versioned cached clip expansions, preserving the original clip program,
triangle order, capacities and stream lifetime. First inspect which per-chunk
words vary; require target command/state and raster equivalence before pricing
in both orders. This differs from the existing cull-only partial replay and
from C18's geometric subrange refinement. If command bodies cannot be reused
without relaxing lifetime/state rules, reject that candidate rather than adding
another broad cache. No production renderer/codegen defaults changed here.
Evidence: P07.

All three physical fixtures retain 303 unchanged staged inputs. Both original
user project hashes remain unchanged. Evidence archives freeze private source,
logs and analyses; their tools directories snapshot the final diagnostic tools
(including corrected validators), not the tools that originally accepted V2.

**P08 - Clip command-body memo. Status: rejected, slower on PS2.**

Source audit finds clip TC stream commands depend only on current stream
addresses, vertex count and single-colour selection. Keep the dynamic standard
header (including clip-plane mask, depth and PRIM), object uniforms and program
kick on their original paths. Start with IDs 24/34, TC only, a bounded direct-map
cache of command words copied into the existing live packet, not cached payload
or a new DMA lease. Existing submission snapshots still resolve the current
stream addresses. Thus geometry mutation needs no cached-payload invalidation:
only command bytes are memoized, and current pointers/count are the full key.
This deliberately retains the chunk loop; it prices a narrower implementation
before attempting whole-stream replay. Require actual target fresh-writer byte
comparisons, positive hits, zero mismatch/overflow and unchanged drawing witnesses.
Price counter-off off/on/off, constant VU paint/interleave and original roads.
Reject negligible/mixed gain; no promotion without independent state/lifetime
and raster qualification. This is not P03's plane-math memo or P07's omission.

V1 native/host build passed, but the emulator's first cold command-byte oracle
asserted before pricing; never deployed on PS2. Source inspection of PS2SDK
shows its DMA tag bitfield writer leaves PAD bits 16..25 untouched. V2 preserves
the current destination's PAD bits on replay, just as the original writer does,
then compares full emitted words. It does not mask differences in addresses,
VIF commands, QWC or tag control bits. Require actual target equality before
accepting this explanation; host synthetic padding checks are not sufficient.

V2 completes 2520 rows and 14 parser rejection controls on both targets. Cold
actual-writer checks: PS2 792/1600/594, emulator 792/1600/1386, all zero mismatch.
Candidate cold hit rate is 1600/1600 on both. PAD differences occur on both
targets; replay preserves them and all emitted words match. Host sanitizer
tests pass key mutations, corrupt-body rejection, replacements, PAD preservation
and output canary. All three emulator smoke captures exist; candidate inspected,
not exact raster qualification. Native binding verifies 468 files and ELF
`0c469ef5942edd305d7c00ae1d98b303542d419d7a20b6ac6cccc75c77918832`.

PS2 off/on/off: 15.265059 / 15.771481 / 15.322876 ms,
48.166033 / 47.687033 / 48.165905 Hz. Candidate is SLOWER by
0.506422 / 0.448605 ms. Emulator is also slower by 0.044486 / 0.031194 ms.
Reflection captures are 174/179/174; do not label the difference a pure writer
price. Reject this implementation; no reverse-order promotion trial warranted.
Do not infer that all command replay is useless: this memo retains every chunk
loop, dynamic header, lookup and queue operation. The implementation is private
and does not justify another arbitrary cache-size/hash tuning series.
Evidence: rejected V1,
completed V2.

**P09 - One expansion lookup per original strip. Status: stopped on user redirection; unpriced.**

Independent of P08 command memo: `renderStrippedPkgs` invokes `fillCachedStrip`
for every tiny clip chunk, rebuilding the same package-wide key/acquiring the
same full expansion each time. For IDs 24/34 only, acquire at the first chunk,
then derive later slices locally within that synchronous package loop. No new
cache, no pointer retained across packages/frames, no changed qbuffer cadence,
program, header, primitive order, clip capacity or DMA ownership. Failure of the
first acquisition falls back to ordinary expansion. No renderer cache eviction
runs inside subsequent clip/flush calls; normal queue leases remain untouched.
Cold target comparisons invoke the original lookup and compare all derived
stream addresses, count, mask and bag/topology against its result. Require
positive reuse/check equality, zero fallback/mismatch and original identity.
Price counter-off off/on/off, then reverse only for a useful gain. This prices
redundant expansion lookup, not command construction or a new geometry cache.

Host actual-loop tests pass 175200 mocked combinations. Native binding verifies
467 files and ELF `a108f35bf73f5fe0348bd1b4ad502c490da285f3a611d892adc4b5028ba7c75f`.
Emulator control cold witness: 100 loops / 1600 acquisitions. Candidate cold
witness: 100 loops / 100 acquisitions / 1500 reused slices and comparisons,
zero mismatch/fallback. The user redirected the investigation to scene/work
amplification before completion. Owned emulator/capture were stopped; there
is no completed timing contrast, physical launch, raster qualification or
production change. Do not resume merely to finish a cache-related series.
Evidence: interrupted P09.

**A01 - Scene and packet preparation audit. Status: source audit, no new deployment.**

User-directed change of question: not "how can this repeated operation run
faster?" but "why is this amount of work requested at all?" Keep this audit
in the same decision register. Facts below distinguish the authored original
from the quiet, constant-VU-paint P06/P08 diagnostic build; those are not the
same settings as a launch from the editor. In particular the authored project
still has showProfiler=true, whereas the timing fixture disables its observer.

Authored `main` inventory, read from the project and its referenced object JSON:
135 objects = 70 models + 36 boxes + 10 areas + 8 point lights + 7 roads +
3 vehicles + 1 player. Terrain dimensions are 320 x 320. Nineteen objects
explicitly opt into reflection drawing (seven models with reflection proxies,
twelve boxes). These are authoring counts, NOT visible objects/draw calls or
active lights. The daylight diagnostic reports zero active lights and 66 road
chunks. There are no authored mirror, portal or camera objects in this scene.
The main authored project hash remains
`3c20bfeac1de6e74c923b6474069f3adbb36947ad3cfca35ac458c9bd9aca69c`.

Current preparation flow:

1. Scene loading constructs geometry: roads become independently bounded
   chunks and strip runs; static objects become material/cell batches;
   terrain has resident chunks and optional companion pass bags. Road spline
   tessellation is NOT a per-frame job. In the generated source, the active
   road builder is `game_vehicles.gen.cpp`; a large old copy in
   `game_collision.gen.cpp` is commented out and must not be audited as live.
2. Before the main world, a shared reflection capture may run: update its
   eligibility/content key, switch view/target, draw sky, resident nearby
   terrain, roads and selected reflected objects, then restore the main view.
   This configuration uses one shared level-forward probe, not a probe per car.
   Ground radius is 20 and target 128 x 128, but accepted terrain/road chunks
   still enter the normal renderer. `renderTerrain()` includes any existing
   layer, AO, ground-shadow and emissive bags; target size alone does not
   simplify geometry or remove those passes. Seven building proxies already
   exist: do not propose "add reflection proxies" as if none were present.
3. Main view: sky, terrain residency update and terrain passes; collect accepted
   static batches/roads after producer-level distance/AABB checks; procedural
   geometry; object/vehicle materials plus optional AO/emission/env companions;
   wheels, translucent/effect passes and 2D. Deferred road/batch execution is
   interleaved with objects, so call-stack phase names do not identify owners.
4. Every accepted bag enters StaPip Core: derive legal program/package capacity,
   acquire bounds, transform/test bounds, prepare MVP/texture/light/state and
   object uniforms. The engine repeats a main-box check after the producer's
   coarse AABB check. This is real duplicated classification, but P02 does not
   show that deleting that test alone would recover the missing frame budget.
5. Wholly accepted/guard-band-safe bags can use the direct route. Partial bags
   split into original packages and classify them against view/clip planes.
   A screen-edge intersection alone need NOT invoke clipping: a guard-band
   route already exists. The missing fact is which exact clip plane and which
   actual triangles force the costly route in the problematic frames.
6. An unrefined 66-vertex strip has 64 triangle slots. When classified for
   clipping it expands to 192 list vertices. TC's 75-vertex nominal capacity
   becomes a 12-input-vertex clip budget (divide by six, round to a multiple
   of three), so that one strip becomes 16 calls of four triangles each.
   The sixfold reserve protects clipping fan-out: do not simply raise this
   capacity. P06/P07 confirm this multiplication for IDs 24/34. It does NOT
   prove that all 64 triangles really intersect a clipping plane. Road
   subrange refinement was already tried in C18, with limited gain; repeating
   that same switch is not this audit's next step.
7. Each resulting qbuffer carries streams/count/mask/topology and selects a VU
   program. Packet building writes the current header, stream commands and
   program start/continue; normal queue snapshot/borrow rules secure input
   lifetime. Buffers and packets are then coalesced into native transport.
   A chunk, bag, qbuffer, packet and DMA launch are DIFFERENT units. P06 saw
   one native prefix flush per sampled frame, not one DMA launch per tiny
   clipping chunk. More qbuffers can still multiply EE preparation work.

Audit questions, not confirmed bugs:

- **Geometry amplification:** for each costly owner/pass/view, how many source
  triangles are wholly inside, wholly outside, or actually intersect the active
  clip planes? How many are nevertheless submitted through the worst-case clip
  route because their enclosing strip/box intersects? Include world bounds and
  screen coverage. This separates representation/partition problems from
  necessary clipping; triangle count alone or another cache-hit count cannot.
- **Pass amplification:** which terrain/road/object geometry is revisited in
  main versus reflection views, and which companion passes are present? A
  128-pixel reflection using main-view geometry and terrain companions is a
  concrete audit target, not yet proof of waste or permission to remove shadows.
- **Coverage overlap:** terrain under opaque roads and repeated companion
  surfaces can duplicate preparation. Correctness depends on gaps, slopes,
  layers and reflection visibility; an overlapping AABB alone proves nothing.
- **Frame-sensitive side effects:** name actual rebuilds, texture acquisitions,
  changed data stamps, uploads and allocations with their triggering owner.
  Ground-shadow acquisition already demonstrated why a render scope can hide
  asset work. Its known small-set preload fix must remain enabled; do not
  assume any new hitch has the same cause without an event.

Next artifact should be a small packet-lineage ledger for representative
stationary/turn/drive frames: original object/chunk -> view -> material/pass ->
source geometry -> accepted/rejected route and plane -> qbuffers -> submitted
vertices/bytes; plus rebuild/acquire/upload events. Reuse P06 ownership and
existing traces first. Add only missing reasons/geometry snapshots, captured
to bounded RAM and exported after sampling, with no dense scope-clock mesh.
Analyze that frozen data offline before choosing a renderer change.

Only after the ledger identifies an unexpected multiplier, perform a runtime
producer/pass omission to bound its cost, then change the responsible policy
or representation while preserving visible output. Do not delete authored
roads or regenerate altered bake identities to isolate rendering: that changes
contact/camera/receiver inputs and repeats an earlier invalid comparison.
Do not turn all these questions into an unbounded matrix of FPS-only tests.

Source anchors (frozen P06 V3): `game_physics.gen.cpp` renderScene:1063,
shared capture:1415, terrain main:1557, material passes:2061, renderTerrain:5922;
`game_vehicles.gen.cpp` buildRoads:14, chunk thresholds:294,
renderRoadChunks:6172, submitHeavy:6230, dripHeavy:6242;
`game_collision.gen.cpp` renderStaticBatches:2691, procFinishChunks:2958.
Engine: `stapip_core.cpp` Core preparation:613, partial route:1205,
renderStrippedPkgs:1302, clipPackageSize:1513; qbuffer renderer clip:2043,
packet building:2767. These anchors locate the audited code, not timing scopes.

## A35 V7 Physical Resume

2026-10-11 14:00 CEST: user-confirmed ps2link recovery; goal resumed.
Unchanged V7 ELF9193dd24...a237e deployed with the documented already-reset
exception, owned client37164. This is the pending full physical correctness
gate, not new counters, a new optimization, or accepted speed evidence. Require
six complete trips, 60 exact rasters, 30 original-head oracles, actual admission
rejections and all inherited source/lifecycle gates. Preserve partial failure;
do not price it. Evidence: F:/Projects/tyrax2-world-front-20261011-runtime-v7/physical.

V7 physical outcome: complete six-trip DRIVEDONE valid=1; full closed-log
reader accepts 60 exact rasters/30 original-head oracles/52 negative controls/
264 actual stale rejection calls. Positive moving activation and baseline owner
release pass. No hardware price accepted; preflight deliberately forbids it.
Client37164 stopped after exports. First archive attempt rejects historical
emulator log hash drift; unchanged closed log reanalysis passes full gates.
Next: qualify isolated net frontend pricing, preserving common A31 body costs,
candidate-only ticket preparation/release and all correctness gates, then
same-ELF both-order PS2 A/B. Do not promote based on preflight timing columns.
The678-file V7 archive
replays both full reports byte-identically with preserved tools and validated
inventory. Goal ACTIVE/unmet; devices remain powered on, no default changes.

## A35 Net Frontend Qualification

Continue the accepted V7 strip boundary, not a new optimization. Question:
does its existing physical Off/On/Off series price ticket creation/release and
moving producer rebuilds rather than hiding candidate cost in both arms?
Extract and compile the actual warm helper, Core producer wrapper and ticket
admission guard; test steady frames, role/epoch transitions and rebuilds, with
rejecting missing-guard/transition/per-frame-sweep variants. Require unchanged
bound sources and the full physical preflight before computing net contrasts.
Price only physical evidence; preserve the preflight manifest/report as
nonpricing. Small or inconsistent gains reject this policy, not the whole EE
hypothesis. Both physical orders are required for promotion; no extra reverse
boot is warranted if the first order already fails materially.

A35 net outcome: extracted warm/wrapper/guard host V3 passes512 frames/4
controls; V1 missing capacity shims and V2 negative unused-variable compile
attempts rejected. Separate net reader reruns full physical preflight and12
negative report controls. Day mean saving0.007684ms is inconsistent against
adjacent controls (-0.036706/+0.052075); night REGRESSES0.086226ms with
candidate p95 24.426517 vs23.712084/23.764079. Both first-order gates FAIL.
Keep OFF; no reverse boot/promotion. Initial load excluded, moving rebuilds
included; strip-only boundary, not pure EE or a full-scene ceiling proof.
See net rejection evidence.
Next must identify a different substantial mechanism from existing packet
lineage/caller evidence; do not tune the failed ticket policy or blindly extend
it to lists/env/light classes. Goal remains ACTIVE/unmet, devices powered on.

## A36 Recorded Command Lineage

Before new runtime work, decode the already accepted A18 physical command
closures offline. A18/A20/A21 establish substantial descriptor-to-command
elapsed work; A31/A34/A35 do not identify the dominant remaining command
classes. Difference: no new target instrumentation or clocks. Map exact bound
ELF VU images into the captured resident image, flatten the owned linear DMA
closure under its actual FrameChainArena dense-copy contract, and use the
bound actual Vif1ChainCheck::Stream as command/payload boundary oracle.
Require exact closure/log/report pins, complete reference bounds and cursor
closure, accepted command/call counts and rejecting truncation/address/payload
controls. Report unmapped programs explicitly; do not invent draw owners or
EE/VU cycle prices. This is an offline structural ledger, not another FPS
test or permission to omit geometry. Choose a different substantial mechanism
only after seeing which real command classes remain dominant.

A36 historical physical day/night decoder completes759/805 launches, every
call maps to an exact resident image range; four corruption controls pass
per closure. Night Cull_TC510 calls/1632192 UNPACK bytes, mostly393 strip
headers versus117 lists; Clip_TC240/146176; env27/98256. Header counts agree
with input lengths. Largest MVP-delimited run46 calls/3390 vectors; owner
identity is unresolved.827 VIF FLUSH are not EE launches or permission to
remove fences. V1 rejects stripped ELF lacking symbols; pinned nm/image repair
passes. See A36 ledger.
No runtime change, new hardware test or timing. This is older A18, not current
A35: next obtain a matched current-baseline closure under qualified synchronized
freeze/ordinary-return raster gates before attributing a new optimization.

A36 current-capture continuation (in preparation): clone bound V7 into a fresh
private fixture, keep A35 OFF and common A31/A30 baseline unchanged. Freeze the
day-drive pose at offset1380, suspend the outer motion harness, force live
reflection capture in every ordinary stage, fence presentation every frame.
Capture one dense owned closure through the existing A18 actual parser/copy
checks, and label each descriptor by actual scene-owner pointer plus main/env
view. No replay, new clocks or production policy change. Owner scans are
qualification-only and must never be priced as renderer performance. Require
one captured submission, stable resident program image, ordered descriptor
checks and exact main/env ordinary-return rasters before interpreting traffic.
Unknown owners remain explicit; descriptor identity is NOT yet command-owner
binding. Existing historical A36 and V7 net rejection remain authoritative.

Continuation completes on hidden emulator and PS2: native491/ELF82a32534...bd58,
eight exact ordinary-return rasters each, zero descriptor mismatches, one
flush/submission, stable program image, four report/raster and four DMA/VIF
negative controls each. Physical657 launches, Cull_TC455/1489872 UNPACK bytes.
Road descriptors28/17967 main source vectors (46.48%); includes Core rejects,
NOT emitted work or EE time. Freeze poses differ by real dt; qualify per-target.
Unknown owners explicit; no command-owner binding or price.602-file archive
replays qualifiers unchanged and physical decoder byte-identical:
current V7 capture.
Next caller audit must consult existing road investigations first. No promotion.

A37 interrupted-PC feasibility: road caller and queue audit confirms counted
snapshot/no second inline copy, prior C05 fusion regression; do not reopen them.
H13 PMU restrictions checked. SDK gprof samples the last instrumented function
entry, NOT callback pc_value: reject it for actual instruction profiling.
Shared timer callback is a possible raw-PC channel, pending installed-code and
two-leaf target calibration. Bounded host store/delayed-IRQ policy implemented;
no runtime sampler, target result or performance inference. Require IRQ-off
export/lifecycle, multiple intervals, unmapped/kernel bins, intact scene rasters
and a separately priced substantial fix. See
[A37 contract](tyrax2-pc-sampling-2026-10-11.md).

A37 calibration now passes native490 and both targets: emulator375/PS2372
raw samples,97..100% correct callback leaf hits at997/1733us, ZERO CP0 EPC leaf
hits. Use SDK callback PC, not EPC read inside handler. Zero overflow/late
callback, distinct linked ranges, five reader controls per target. Installed/
mirror libkernel hash and actual forwarding disassembly pinned.519-file archive
copied readers replay byte-identically. No renderer profile/FPS conclusion yet;
next ordinary day/night moving sample with preserved baseline/return gates.

## Maintenance rule

A41 V9 emulator correctness now PASS:496 native-bound files,
ELF2f6a2e99...cdb9f7, all54 main/env/display rasters exact,1262 owned copy
completions/eight HELD ISR claims,16 rejecting reader controls. Moving emulator
ISR0 permitted only because separate held positive test qualifies actual entry;
physical moving still MUST be positive. Linked application IRQ closure PASS.
No hardware price yet. Stage helper excludes hybrid-copy CSV outputs so emulator
witnesses cannot become mutable physical launch inputs. Console/computer ON.

A41 V9 full emulator completes;54 main/env/display equality and copy witnesses
pass up to reader negative-control stage. First reader incorrectly mutates already
zero MOVING ISR count, so inactive-ISR control is vacuous. V2 mutates actual
positive HELD ISR instead; candidate moving event counts are matched exactly
to copy completion/ISR witness, require moving ISR positive only on physical.
Firmware/log/pixel gates unchanged; preserve first failed reader log. Recheck
pending, no PS2 price/default yet.

A41 reader review adds elapsed EE Count bound<288*512 ticks as well as physical
edge ages: ages could alias a later whole field after delayed GS/END handlers.
This independent duration rejection prevents that false inside-blank evidence;
add over-budget negative. It qualifies observed completion against experimental
admission, not universal copy WCET. No firmware/timing-mode change.

A41 full-game gate now also requires actual CT16 display readback: existing
36 CT32-main/env equality alone cannot show that the immutable copy reaches
scanout correctly. V9 generator extends existing aligned-row tape export to
third display surface (54 rasters), original OFF-vs-ON full pixel equality.
V8 is already frozen/built and tests ISR/prefix lifetime only; it does NOT have
this new output gate, so must not be promoted/deployed as qualified. Need V9
native/full raster+IRQ tests before physical price. V7 linked application ISR
closure audit passes, six reachable routines/two source-bound indirect callbacks,
no kernel/cache/heap calls; BIOS routing/WCET not proved by that audit.

A41 V7 native496/ELFb9264ebc...435be boots with installed1/81qw; actual held32
and moving535 copies complete inside emulator window, no late/drop/refusal.
BUT ISR0: fast emulator always reaches main fallback before a ready START.
Not independent dispatch qualification/physical price. V8 adds one40ms HELD-
only delay after NEXT begin, preserving new unsubmitted clear prefix and old
native presentation across START, identical ALL arms and outside moving price.
Held frames otherwise use normal async Core (retire after loop); require positive
ISR AND fallback there. Physical moving still requires ISR>0; emulator moving
counts do not determine physical throughput or enforce artificial slowdown.

A41 V6 correctly gets IMR7700: SDK unmasks VSINT while GS INTC is disabled,
not an enabled foreign GS owner. V7 admits ONLY known7700/7f00 with GS INTC OFF,
masks VSINT before taking GS IRQ, uses frozen owned7f00, restores saved mask.
Source audit also catches startup warmup SDK wait OR-clearing pending native
FINISH: private warmup acknowledges ONLY CSR8 before fresh VSINT polling, preserving
old FINISH. This is startup qualification, not blanket vsync skipping. V7 native/
target validation pending; V6 refused and never priced. Timers/other IRQ gates
remain unchanged. No hardware scheduling or FPS claim from these source fixes.

A41 V5 isolates refusal: T0/T1 zero, masks1802/pendingd, purported IMR551b6000
is a CSR mirror, NOT another timer owner. Linked GsGetIMR/Put are syscall70/71;
SDK exposes both. PCSX2 GS read implementation mirrors CSR for write-only IMR.
V6 uses main GsGetIMR, frozen owned full mask stores in ISR, main GsPutIMR restore;
NEVER read-modify-write GS IMR. Ownership gates retained. V4 full run finishes,
but reader also catches exactly first-gas flips0 due common capture-boundary
retirement before begin; bind that explicit one-row contract, no generic relaxation.

A41 full-game V4 native build succeeds,496 bound files/ELFb37d8abc...e6bcdb.
Hidden emulator rejects activation: installed0/words0/refused32 in held arms,
candidate moving isr/fallback0. This is NOT a successful A/B or price. Preserve
attempt; V5 adds one main-thread reservation refusal record to distinguish
actual timer/GS ownership. Do not relax ownership gates based on emulator alone.

A41 integration catches real lifecycle gap in component V3: next-bank begin
zeros nativeInFlight while old Core presentation still exists. Queue V4 now
uses dedicated nativePresentationSequence: publish AFTER final flush, preserve
through next begin, revoke AFTER callback BEFORE new enqueue. Actual-source
65536-state/240-admission optimized+sanitized/EE tests pass; ordering source
guards pass. Adapter V6 adds completion ISR,46 guards/644 packet tests pass.
Full-game V2 native callback-name failure and V3 ambiguous generator anchor are
preserved, not target evidence. V4 native build underway; no performance claim.

A41 full-game fixture integration begins from bound A38 reverse (NOT A39),
Hybrid2 in ALL arms, original81qw immutable copy and actual old-native queue API.
Core retires the copy before source reuse; START/END/one-shot FINISH handlers
do not build packets/wait/advance queues. Physical dual timer guard uses an
experimental288-tick admission budget, NOT a qualified game WCET. In-game
completion witnesses and full main/env rasters must pass before PS2 pricing.
GS IRQ is masked on first delivery even if GIF STR still busy; main/end poll
may finish later, with physical late evidence preserved. No default promotion.
Adapter completion entry adds four mock controls (46 total), checks pending.

A41 actual queue ownership API begins: read-only canCopyCompletedNativeFrame
matches nativeInFlight/last-started sequence, empty ring and idle VIF DMA. It
accepts not-yet-serviced old completion metadata without advancing; completed
must match when not running. Logical submitted/current pending prefix are NOT
actual consumer identity. Call only serialized main/ISR; GS FINISH/window/source
ownership remains separate. No ISR caller or runtime behavior change yet.
Actual-source extracted16384-state truth table now passes optimized/sanitized,
60 admissions and zero metadata changes; EE object with mutable target setup
passes. V1 target mock could fold pipeline=false; V2 avoids this vacuity. Queue
identity does not prove foreign-GS exclusion or Core ownership. Full engine/native
link and actual ISR caller still pending; no runtime price. V3 runner checks EE
loads/no stores/calls. See adapter queue-v3.

A41 original-copy V3 completes BOTH targets with pinned actual79qw+2 FINISH,
14 columns and all200704 pixels/6 reader negatives. Actual PS2 median0.179562ms,
max0.180206ms;120/120 hardware-aged copies inside blank, restored T0/T1. This
supports scheduling investigation: isolated copy itself fits observed1.43ms
blank, NOT universal WCET/IRQ dispatch/game ownership or EE gain/60fps evidence.
V1/V2 test-pattern/readback failures rejected before PS2 and preserved; original
emitter unchanged. Owned34720/256/245 stopped after completion; devices ON.
Next actual game ownership/immutable copy guard integration, full raster/blank
completion and Hybrid2-only A/B. See adapter copy-budget-v3.

A41 isolated original-copy budget trial begins after physical timer preflight.
Extract actual bound A40 getRasterTarget/emitRasterRestore/emitHybridPresent
bodies unchanged; stub dispatch ONLY during immutable packet construction.
Full448x448 CT32->CT16,14x32px columns, original dither/restore plus SDK FINISH.
Four-quadrant source/destination readbacks and packet dump precede acceptance.
Measure120 fresh-blank copy DMA+owned GS FINISH completions with dual hardware
ages. Isolated consumer cost, NOT in-game WCET/EE gain/scene FPS. Require native
source pins, emulator pixel coverage, physical completion/late accounting first.
V1 completes emulator120 copies but source pattern fails coverage (one dominant
color/black boundary), so REJECTED before physical deployment. V2 replaces only
test-pattern drawing with explicit integer SPRITEs; actual copy methods unchanged.
V2 still rejected: SDK file screenshot input is unsuitable here; destination
passed words instead of blocks and source file has a two-pixel row displacement.
V3 uses existing tape-style aligned row ps2_screenshot(address/64) raw readbacks;
no copy-emitter edits. Preserve both failed emulator attempts, no physical pricing.

A41 isolated timer preflight now completes actual PS2 and hidden PCSX2 with SAME
ELF6489c9ee...0653a,120 START/120 END, restored T0/T1, zero errors and six reader
controls. Hardware blank823..824ticks (1.428819..1.430556ms), emulator841..842:
do NOT transfer emulator timing budget. Opposite gate polarity confirmed here;
isolated free timers do NOT prove full-game ownership. Original conversion copy
and dispatch budget remain UNQUALIFIED. Editor reset succeeds despite ICMP false.
Owned41540/40/29 stopped after full completion; devices ON. See adapter preflight-v1.

A41 isolated hardware timer preflight begins: same A40 480p signal, reserve T0/T1
ONLY when MODE/pending/mask show idle, no scene/copy. Install bounded START/END
capture handlers; opposite hardware reset ages plus polling expose polarity and
intervals independently of handler latency. Restore original count/compare/mode
and interrupt enables, remove own handlers before export. Scope is isolated
reservation/timing, NOT in-game ownership or original-copy budget/FPS. Qualify
emulator boot/teardown before editor-equivalent physical launch.

A41 dual-edge window predicate implementation begins after hardware source audit:
opposite hardware-reset counters can distinguish active blank independently of
delayed handlers. Private fail-closed predicate requires qualified epoch/modes,
field/active bounds and remaining original-copy/dispatch budget; no MMIO or timer
reservation yet. Exhaust edge/read/delay positions before actual target preflight.
This is physical copy safety, not another packet-cost attribution counter.
Component now passes280800 modeled edge/read/budget positions (20000 safe
admissions) and137 negative controls in optimized/ASan+UBSan plus EE compile,
24/32-byte ABI. No MMIO/physical qualification or Core integration. Archive
window-v1 pins original predicate/driver/runner/logs/object; target reservation,
polarity/bounds and original-copy dispatch budget remain the next gate.

A41 hardware-window source audit: Sony timer gate-edge resets support independent
event age, NOT qualified blank/copy timing. Actual A40 linked init binds SDK T2
(InitTimer2/INTC11); linked alarm setup and Tyra clock exclude T3. T0/T1 source
search absence is not kernel/module ownership proof. BUSCLK/256 avoids ordinary
field-length16-bit wrap, but edge polarity, actual blank width, startup epoch,
reservation and original-copy budget need target qualification. No hardware
register changed or deployment. Next establish this fail-closed guard before
Core/queue integration; do not trust delayed START/END handler timestamps.
Pinned ELF/source observations.

A41 adapter V5 component now passes42 mocked-hardware controls plus644 EXACT
extracted backend packet validation cases in optimized and ASan+UBSan. Real MMIO/
cache backend explicitly instantiates/cross-compiles with `_EE`;60-byte adapter
ABI assert. Own old-FINISH clear before copy; new FINISH+DMA completion before
release; prior IRQ state restored; direct ISR relocations have no kernel/cache/
heap refs (NOT full indirect closure). No target integration/deployment/price.
Source review finds delayed-IRQ gap: even END-handler completion is not physical
blank proof. Require explicit window callback covering actual event age/budget;
unbound target backend FAILS CLOSED. Next inspect hardware timer/gate and kernel
semantics, then bind actual queue/framebuffer owners and original immutable copy.
V2 missing initializer_list fails host compile; V1/V3/V4 earlier component evidence
not target-qualified. Adapter constraints/evidence.

A41 private copy protocol component now passes144 guards/161280 event schedules
in BOTH optimized and ASan+UBSan host builds; EE relocatable compile verifies36/
12-byte state/readiness ABI. ISR/fallback claim exclusion, packet/job provenance,
source release after copy DMA+owned FINISH, retained blank-end completion and
fresh-field fallback; wrap/multi-job/late cases pass. Review catches same-blank
fallback risk and adds withdrawn-field guard BEFORE accepted V1. Actual adapter/
MMIO/cache/atomic transitions NOT implemented/tested; volatile is not a lock.
No target deployment/net/60claim. Next implement the immutable packet and actual
single-old-job adapter, extract/mock its actual code before native/full output
qualification and Hybrid2-only physical pricing. Protocol evidence.

A41 source/binary audit closes: current A40 ELF/.sym exact text and492 bound
sources, linked graph_wait_vsync0x1017d8 clears old latched VSINT before polling
a new event.32 host low-event cases/four instruction mutations, NOT target timing.
Identical VSINT can describe active blank or old completed blank, so naive wait
skipping is unsafe; no candidate/default. EE already records N+1 while N runs.
Before new actual submission, complete old DMA/GS, then Hybrid2 fresh VBlank/copy;
copy start remains tied to EE callback arrival. A possible missed deadline is
not a measured causal count. Proposed next architecture: prebuilt immutable
Hybrid2 copy armed for independent VBlank start, explicit old-job/FINISH/GIF/
source-lifetime and atomic ISR-vs-fallback ownership. Different from rejected
A39 renderer callback/per-Core hook, no runtime implementation yet. Must qualify
copy completion within blank, not just drawing readbacks. Audit/protocol.

A40 V1 first physical order now completes BOTH targets/full36-raster/17-control
gates. Day58.703/59.378/58.591Hz, night54.338/56.679/54.092; candidate integrated
non-pacing saves1.063..1.119/1.130..1.212ms. Both net PASS, both stable60 FAIL
(day5late,night29).617-file copied readers replay exact; reverse/default-off
image-cost/production integration open. Devices ON. Not an EE solution via
promoting triple buffering. A40 result.

Burnout buffer recheck: all six saved states FRAME1/2 CT32/block2240, both GS
dumps active DISPFB2 CT16S/block0 and fixed decoded display fields, plus dithered
CT24-to-CT16S output copy. This resembles Hybrid2, not ordinary CT32 rotation;
corrects any provisional suggestion that eliminating a copy matches Burnout.
Metadata does NOT establish EE wait timing/physical cost; focus next source audit
on synchronization/copy submission dependencies, retain CT32x2 as diagnostic
alternative only. Bounded report.

A40 V1 closes native492/ELF9f811be6...8b9b6 and hidden six-trip/36 exact-image/
17-control gates, positive paint activation and exact+3qw/head. Reader first
attempt excluded added words on only one side; repaired symmetric comparison
plus separate exact delta, unchanged ELF/source/raster contracts. Physical first
network-reset start has no output and ping times out; preserve failed deployment,
no game-crash/performance attribution. Same ELF retry after user reset pending.

User correction: Hybrid3 is not the desired EE solution. Source audit confirms
Hybrid2 fixed CT32 draw/CT16 display with post-vsync copy versus Hybrid3 unscanned
copy/FINISH then ISR queue. Thus A38 changes scheduling AND storage, not pure
buffer-count or EE speed. Next design (not implemented) ordinary CT32x2 versus
Hybrid2 isolates a different architecture but still changes VRAM/output format;
retain these confounds and strict source/scene/env/presentation gates. A39 rejected
early-ready hook is NOT this test. Burnout two-buffer reference does not establish
its format/workload. See frame-pacing.md; do not default-promote Hybrid3.

A40 V1 now generated from bound A38 reverse (no A39): actual Hybrid3 ALL arms,
private EE/VU/EE modes, existing supportsPaintVu/basis/representation logic.
Cold actual paint-head witness and prescribed +3qw delta, original emitter
normalized against parent source, all original triangle/clip/stream/raster gates
remain. No source-gate loosening before evidence. Native/targets pending, no
price/default. [A40](tyrax2-paint-triple-2026-10-11.md).

A39 V5 completes BOTH targets/native493/ELFcc4eaae5...a5f5b, actual PS2 moving
190/340 protected publications, all held31,36 exact rasters each/17 negatives.
Physical day58.815/58.701/58.813Hz, night54.338/54.013/54.450: BOTH net FAIL,
stable60 FAIL. Mean work regresses0.050..0.084ms day/0.123..0.154 night, not pure
EE arithmetic. Keep private OFF/no reverse or poll/cache rescue.621-file copied
readers replay byte-identically. Devices ON, owned5168/26/15 stopped after full
export. [A39](tyrax2-ready-present-2026-10-11.md). This rejects the specific early
publication scheme, not EE efficiency or a whole-scene60 ceiling.

A40 next combination question, NOT a new paint discovery: existing A13 physical
integrated VU paint saves~1ms under older double-buffer ordinary LOD workload;
its reverse/default-off image-cost qualifications are still open. After A38's
both-order triple success and A39 rejection, test EE/VU/EE with THREE buffers
BOTH arms/current A31/A30 folded-light/native-strip ABI. Require accepted paint
basis/representation invalidation, current source/clip/output semantics, full
matched main/env rasters and actual offload activation, same-ELF PS2 presentation
net/tails, then reverse if positive. No adding unrelated timings, no automatic
default or60claim. Consult A13/A08/A11/A29 evidence first. Not implemented yet.

A39 V5 full hidden closes six-trip/36 exact-raster/17-controls, all held
protected30..31, moving day0/night1 only. Same ELF on PS2/editor reset/client5168;
first moving candidate190 protected/37106 checks, held31. Full hardware return/
night gate and net result pending. No default/performance claim yet.

A39 V5 native493/ELFcc4eaae5...a5f5b binds. Hidden first held candidate actually
presents30..31 protected prefixes; moving first candidate ZERO/42263 checks.
Allow inactive emulator MOVING only, not physical performance interpretation:
ALL hidden held candidates still positive with exact rasters, BOTH PS2 moving
candidates positive before price. No hidden full gate or PS2 yet. Do not freeze
or delay priced moving work just to fabricate candidate activation.

A39 V3 held pause fails native FrameProfile name; V4 direct CP0 fixes it, V5
explicit ready call after common held pause precedes reflection fences so they
cannot mask activation. V5 native build succeeds; source-extracted modeled4099
controls pass optimized/sanitized plus32 header-state cases. No target yet;
binding/full moving+positive-held/raster gates still required. All earlier
versions unpriced/undeployed, not target failures or performance evidence.

A39 V1 missing assert include fails native; V2 builds but held synchronization
would mask activation, not deployed/priced. V3 held normal overlap with common
40ms qualification-only pause after new clear, never moving-price delay. Require
positive protected-prefix activation in all six held candidates too. Actual
Core/queue extraction passes optimized/sanitized modeled4096 state/wrap cases;
not hardware readiness proof. V3 native/targets pending.

A39 V1 private implementation now generated: Hybrid3 BOTH arms, actual ready
old DMA+GS FINISH/free display guards, RAII current-prefix/HUD/bank protection,
writer/arena/bank invariance assertions. Poll recording-start and StaPip entry,
no worker/timer/forced wait. Off/On/Off moving plus held gates; require positive
protected-prefix activation, not just a callable helper. Native/targets pending;
no cost/result/promotion. Host guard matrix is NOT target readiness proof.

A38 reverse-v1 completes BOTH targets/native491/ELF6f6ea95f...daae3, full six
trips/36 exact rasters/14 controls. Actual3/2/3 day58.818/48.836/58.815Hz,
night55.104/40.832/54.889. Both triple endpoints beat double with retained p95;
both net gates PASS, stable60 FAIL (10/10 day,43/45 night late intervals).
603-file copied-reader replay exact. No production default: private common
A31/A30 qualification is not normal-production integration. Devices ON, owned
6428/26/15 stopped after completion. Reverse.

A39 next, source audit ONLY: nonblocking publication of an already-finished old
Hybrid3 job during next-frame EE preparation. Nearest H01 replay/C05 fusion/H05
lookup did not test that timing intervention with unchanged moving workload.
waitFor ALWAYS flushes current native prefix; old callback is safe only because
its caller cleared nativePendingSequence first. Preserve current reservation/
bank/HUD/raster state while publishing old DMA+exclusive FINISH, only with free
display slot; no forced wait, skipped workload or worker race. Require positive
activation, host lifecycle/wrap/prefix controls, native/full raster/source gates,
then same-ELF PS2 Off/On/Off with triple BOTH arms. No implementation or measured
deadline cause yet. [Source contract](tyrax2-hybrid-triple-2026-10-11.md#a39-ready-presentation-source-audit).

A38 reverse-v1 now tests moving3/2/3 day/night, held2/3/2 unchanged, same bound
A37 source/common workload and no sampler. Native491/ELF6f6ea95f...daae3 agrees;
only engine mode selection and generated checkpoint restoration differ from V1.
Hidden full run is in progress; no physical reverse price yet. Question: does
the actual presentation gain survive reversed order and restored triple return?
Reader must compare BOTH triple endpoints against middle double, require full
travel/source/36-raster/14-negative/event grants, and include real VRAM pressure.
No promotion based on the first-order bracket alone. Existing V1 remains intact.

A38 V1 completes BOTH targets: native491/ELF108375b7...df5cf, six drives,
actual2/3/2 grants,36 exact main/env rasters each,14 reader controls. Initial
FRONTORACLE hash equality rejects legitimate live TEX0 relocation; independent
repair pins original head writer unchanged and keeps all source/length/raster
contracts. No target-source/ELF change.603-file archive copied readers replay
byte-identically. Physical DISPFB day49.629/58.928/50.422Hz, night41.515/55.217/
41.401. Candidate late>25ms intervals9/524 day and42/491 night (references
84..91 and162..163). Both net gates PASS, both stable60 FAIL. Mean non-pacing
work barely changes; confirmed large pacing contribution, not EE memcpy fix.
Actual steady VRAM pressure/Hybrid FINISH included. No default promotion yet.
Next reverse3/2/3 BEFORE a new EE/presentation scheduling candidate. Evidence:
A38 V1.

A37 moving completes native490/ELF185560b0...e7b9a, closed hidden30604/PS230492
samples and36 exact OFF/ON/OFF rasters each,15 reader controls, no overflow/late
IRQ. Both997/1733us distributions agree: physical VBlank30.93..34.04%, largest
non-wait function copyQwords5.00..6.29%, Core/head/bounds distributed. These are
PC residency, NOT exclusive cycles. OFF return49.26 day/41.18 night, no60proof.
623-file archive replays qualifiers and physical symbolizer byte-identically:
A37 moving.

A38 next question (NOT implemented): does current Hybrid double-buffer present
sequencing waste usable throughput despite mean non-pacing work14.04/16.17ms?
Actual boot448x448/x2 and completePipelineFrame's VIF/GS wait then VBlank/copy
confirmed. Review: existing frame-pacing.md hybrid triple correctness and old
synthetic PAL load tests; ordered-pipeline/interleave attribution caveat; C05/
H05 queue trials. Existing feature is not a new optimization; no current-scene
moving physical double/triple net result found. Difference: preserved roads/
live reflections/day+night current baseline, same-ELF double/triple/double.
Discriminating result: entry cadence/critical-path tails and physical presentation
events, actual third-buffer grant, identical qualified image/geometry/quality,
no texture/resource-lifetime regression. Include VRAM pressure and extra Hybrid
FINISH/rebuild cost; don't force unavailable buffers, remove fences or compare
unlimited/synthetic frames as60real. If net fails, return to copied mutable REF
provenance; generic PC sample cannot identify the producer responsible.

A38 V1 now implements actual private settings/rebuild/drains and shadow refresh,
native491/ELF108375b7...df5cf passes. No sampler; common A31/A30/A35 OFF.
Initial hidden phase0 grants actual2/3/2 at held checkpoints; complete six-trip
travel/source/36-raster/presentation gate pending. Record actual DISPFB calls
with ISR flag, not flip-return alone. No physical launch or pricing yet. Reader
reuses A37 ordinary gate, NOT sampler or A35 activation; requires actual buffer
grant, no overflow, slots/ISR/ordered events and spatial/tail/net contrasts.

A37 moving continuation in preparation: same ordinary V7 common baseline, A35
OFF;997/1733us drive distributions in BOTH day/night plus sampler-off moving
returns. OFF/ON/OFF held main/env gates, bounded reused768KiB sample store,
released alarms before export. Distinguishes actual instruction residency from
A36 vector traffic; no clocks per bag, no cache change, no speculative fix.
Accept only complete travel/live captures/night lights, no overflow/late IRQ,
exact return rasters and exact native symbolization with unknown/kernel bins.
ISR masking/preemption bias remains; histogram is not cycles or60fps ceiling.
Pending native and targets, no result or default promotion.

Before running a test, add an ID, question, difference from the nearest previous
test, discriminating result and acceptance/rejection gate here. After it finishes,
record actual platform/workload, result, status, evidence link and next decision.
Mark abandoned, invalid and incomplete attempts explicitly. Update this register
in the same change as its tools/results; detailed archives remain immutable.
To reopen a closed row, state the new evidence or genuinely different workload.
