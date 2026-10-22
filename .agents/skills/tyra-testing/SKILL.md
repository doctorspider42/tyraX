---
name: tyra-testing
description: >
  How to build, run and VERIFY anything in this repo: compiling the editor
  (build.ps1 on Windows, build.sh on Linux), headless CLI project creation and
  game builds, checking code
  generation without a game build, full native or Docker-fallback e2e + PCSX2 (boot, emulog.txt,
  reliable screenshots, and DRIVING both the game's controller (`--pad`) and the
  EDITOR's own UI (`--ui-script`, clicking widgets BY NAME) unattended — neither
  needs window focus — plus synthetic keyboard/mouse via the bundled scripts, GDI
  on Windows, mutter's D-Bus APIs on Wayland), and audio verification.
  Use this skill EVERY time you need to test a change, run the editor, build a
  game, boot PCSX2, take a screenshot, PRESS A BUTTON / move the player / drive
  the emulator or the editor without a human, create a scratch project, or decide
  "how do I know this works?" — including before writing a commit message or
  claiming a change is verified. There is no unit-test suite in this repo; this
  skill is the testing story.
---

# Building, running and verifying

Critical-path census: docs/tyrax2-critical-path.md and
tyrax2-critical-path-fixture.py. No offline tables/quality changes; observer
Off/On controls are NOT optimization modes. Keep full image/pose/SDK/owner and
capture-work gates. Nested inclusive scopes are not additive; raw hardware
boundary states are not utilization. Correlate prior N-1 launch/presentation
with preparation N, price observer overhead and use actual present records.
V2 FULL emulator/PS2 PASS54 held images/3942 poses/six endpoints. Actual off
8/7/7 vs on7/7/7 late intervals; all seven recurring sites common to controls.
On-minus-Off contrast−0.005519/+0.002238ms. Pending N-1 linkage is checked
against actual preceding launch/sequence, one native launch per measured loop.
Seal with tyrax2-critical-path-report.py AFTER owned processes stop and both
verifier/analyzer pairs rerun. Four recurrent consumer cases vs three late
preparation cases; do not infer pure VU ALU time from VEW or nested scopes.

Offline-world trials: see docs/tyrax2-offline-world.md. The offline terrain
bootstrap enumerates all effective LOD/edge combinations in the native producer;
its host compiler verifies every active stream after factoring base LODs and
sparse edges. Emulator-produced floats are not automatically physical-PS2
producer bytes: retain the native oracle and failed physical evidence. Material
composition is an explicit quality experiment; verify-world-cell-price.py
requires --quality-trial and a material-compose manifest with qualityEquivalent
false. Protocol/timing success never grants visual or equivalent-output approval.
Always launch automated emulator checks in an invisible WSL/Xvfb session.

Final-VIF follow-up: docs/tyrax2-offline-vif.md. Bootstrap actual SDK geometry
for all effective variants, factor it with tyrax2-offline-vif-compile.py, and
use tyrax2-offline-vif-fixture.py for replay. Keep camera/material/VRAM binding
live, validate native program/layout metadata, and preserve full-body SDK plus
exact raster/pose/work/owner gates. No --quality-trial. The offline terrain
stager selects BOTH pinned PS2 assets. Report both raw and final table memory;
the On role includes both restore stages, not an isolated final-stage price.
Final VIF V1 now FULL emulator/PS2 PASS54 exact held rasters/3942 poses/six
endpoints and native SDK/owner gates. Net0.077975/0.043239ms BOTH orders;
compiler boundary0.578–0.587->0.451–0.453ms, On59.278Hz/6 late intervals,
NOT60. Tables2,818,768 bytes plus allocator/static-binding/ordinary-body costs.
No default promotion or substantial improvement over raw-only demonstrated.
Preserve binary header bytes when copying/hash-pinning; metadata JSON digests
must use actual canonical UTF-8/LF output, not pre-translation text on Windows.

Run tools/verify-world-input-compact.py OUT under WSL for frozen cold-format
component optimized/ASan/UBSan checks. Host V2 passes1120 cases/eight controls,
exact modeled VU memory at each kick and restored incoming ROW/MASK. Decoder
is bounded/synthetic, NOT actual SDK/native VIF or FPS. Actual SDK body inputs,
incoming hardware state, target decoded memory/images/leases and PS2 net follow.
world-input-sdk fixture extends the actual cold SDK gate to dump60 bodies, then
explicit INPUTSDKDONE/SleepThread. It is NOT a rendering/FPS candidate. Extended
host runner accepts the dump; bind it to actual ELF/log and check both state
snapshots. Observed MODE/ROW/MASK stability is not queue-wide lifetime proof.
SDK-dump V2 native504/hidden emulator cold gate passes; verify-world-input-sdk.py
freezes531 payloads and checks60 actual SDK bodies with optimized/sanitized
model decoder plus eight corrupt-file controls. Actual compact VIF execution,
moving exact images/lifetimes/physical net are still unqualified.
Private input-native component runs before world init/recording, drains the queue,
polls DMA/FIFO/VPS/VEW before reads, compares all4096 VU1 data words and restores
data/ROW/MASK/CYCLE. Shader kicks are removed deliberately. Verify completed
VIFINPUTDONE with verify-world-input-native.py and both log paths; source alone
is not target execution, nor is the component moving image/FPS qualification.
Input-native V1 now passes real emulator+PS2 same ELF540 comparisons/restoration.
Physical file logging is disabled under ps2link; require console assertion flush
and completion, inspect log.txt if present. Emulator log.txt is mandatory.
Archive both completed readers after owned clients stop; no moving/FPS claim.
input-compact price variant keeps direct/paint/hybrid wheels/event audio ON;
only cold encoding alternates. Require INPUTINIT once, positive cold format
witnesses only ON, full native SDK inverse audits, original image/pose/lifetime
gates and exact capture/env/paint sequences. Actual PS2 present records decide
stable60; source/model/component success is not moving FPS proof.
Input-price V2 FULL target gate passes but both-order physical net is negative
(-0.087910/-0.118910ms), ON actual58.21Hz/16 misses. Archive correctness must
not be presented as accepted optimization. Keep native format proof separate.

Run verify-terrain-lod-key.py QUALIFIED_FIXTURE OUT under WSL for source-bound
actual edgeMix/hAtE/shadeAtE host extraction, optimized+ASan/UBSan. V1 passes
36.67M points, but callbacks are synthetic stable height/shade; no full producer
or native/FPS claim. Budget-preserving raw-metadata no-op needs actual complete
stream/material/light guard proof before A/B; do not advance real LOD changes.
Private terrain-lod fixture adds an actual full native producer byte oracle
before complete owner preparation in every phase. Require base/shadow streams,
bounds/material/package exactness, preserved88 stale budgets plus five new
residents accounted as93 rebuilds plus metadata skips, and skips ONLY ON. Retain full
SDK/owner/image/pose/live capture gates and actual presentation CSVs. No default
or gain claim before same-ELF physical A/B. Pause after the requested next PS2
attempt, including a failed start; do not retry automatically.
V1 now completes both targets/full gates:20 resident chunks/16 shadows/allLOD
per native oracle,54 exact images/3942 poses. ON70 rebuilds+23 skips, OFF93.
Physical net+0.045194/+0.034259ms is minor; ON59.387Hz/5 misses equals settled
OFF cadence. Not60/default/night. User pause reached; keep devices powered on.

Run tools/verify-native-segments.py OUT --engine-inc QUALIFIED/tyra/engine/inc
for frozen actual FrameVifWriter storage/command checks, optimized plus ASan/UBSan.
3371 cases pass in host v3, including final sole packed GS FINISH. This does NOT
exercise actual queue/GS callbacks, bank leases or PS2 performance; do not deploy
or accept early segments from these host checks alone. Keep native/target gates.
Native-segments price variant keeps event audio/wheels/direct+paint common ON;
require positive gas publication only ON and held exact prior-window byte audits,
plus original image/pose/delivery/lifetime gates. Generation pins helpers before
copy and rejects edits during generation. Superseded unaudited V1 is not deployed.
V2 quiet assertion lives in game/bin/log.txt, not emulator stdout. Read BOTH
before calling idle CPU/DMA completion a renderer deadlock. Held reuse may skip
capture; V3 forces common final held capture only under audit, keeps moving
reuse unchanged and positive two-window gate. Never weaken that gate to pass.
Native-segments full gate also pins exact per-gas capture/env-eligible/env-drawn/
paintVu sequences in all six phases and ON publication for every capture. A
mean gain from fewer live captures is not an acceptable optimization result.
Early prefix moves presentation within the loop: entry late counts268..269 in
V3 differ from actual98..100 display misses. Require actual538 present records
for537 gas loops, slot1/ISR0 and wrap-safe intervals before an FPS conclusion.

Terrain-normal V1 physical-v3 completes all gates after human physical reset
and ConsoleAlreadyReset; physical-v2 stdout was only freepad DMA Busy, not EE
boot. Both-order gain0.050959/0.042518ms is minor, not stable60 qualification.
Sealed entry/present-tail reader accepts qualified event-audio or terrain-normal
archives; normal mode requires six native sources/rebuilt exact endpoint gates.
Existing pose/update/render fields are offline attribution, not pure EE cycles.

terrain-normals price variant keeps event audio/local hybrid wheels common ON
in every arm; only normal source mode alternates. Require NORMALSOURCE native
exact lattice and NORMALWITNESS moving build activation/held mode, plus original
full raster/pose/lifetime/net gates. No priced per-vertex counters or host FPS.
For terrain-normals also require SIX cross-phase reconstructed endpoint main/
env/display rasters exact: toggling mode in held views alone never rebuilds the
terrain and is insufficient to qualify a producer-attribute change.

Existing Hybrid2 drive entry periods refer to preceding whole loops, not just
non-pacing work. Deferred old-job presentation can move the late entry one row
beyond the initiating overrun. Use source-bound entry/present-tail reconciliation;
14.573ms work alone did NOT prove a pacing defect (prior work18.858ms).

PCM lifecycle V1 real PS2+emulator six actual rounds PASS,520-file component
archive/exact verifier replays. Seal report/log hashes AFTER owned emulator stop;
live emulator may append shutdown lines after component completion. Callback
feeding/kernel wake does not qualify audible quality/FPS; preserve low-ring warn.

Event V1 FULL real PS2 gate PASS54 images/3942 poses/kernel waiter/mode wakes,
net+0.572/+0.543ms BOTH orders,59.17..59.28Hz/6..7 late intervals NOT60.
No extra gain over idle16 V2 proved across ELFs. Actual PCM start/feed/stop/
resume/return-to-wait and audio quality remain separate, no default acceptance.

Audio event V1 FULL emulator gate PASS54 exact images/3942 poses, actual
waiting1/kernel waiter1 ON and ordinary polling OFF, both mode changes wake
correctly. Common renderer and PCM starts0 pass; actual active PCM/audio and
physical net remain separate. Do not substitute host mocks for kernel witness.

audio-event reader retains full common render gates but requires real
AUDIOEVENT waiting1/waiters1 ON, idleHalf0; OFF waiting0/waiters0/idleHalf>0,
PCM/start events0. Mode/reset epochs retain source correctness; witnesses OFF
in all sampled rows. Earlier audio-idle V2 must replay unchanged. Actual PCM
start/stop/resume/quality remains a separate gate, not implied by idle images.

Clean idle-audio V2 FULL PS2 gate PASS54 images/3942 poses/PCM starts0,
net+0.573/+0.492ms BOTH orders, ON59.0619Hz all trips but8 late intervals,
NOT60.1287-file/305-input archive exact both reader replays PASS. Keep private;
active PCM/start latency remains unqualified. Existing period rows refer to
previous entry interval, never pair late period with SAME row's work.

Clean audio V2 FULL emulator PASS54 exact images/3942 poses, idle16 only ON/
idleHalf only OFF outside price, zero PCM starts, common hybrid wheels all arms.
Archive911 hashes/305 inputs/exact reader replay PASS. Physical price pending;
empty PCM fixture does NOT qualify active music/audio latency/public behavior.

Audio idle V1 is native-only/UNPRICED (wake witnesses were hot). Clean V2
requires audioIdleNoPriceObserver, bound MotionProbe setObserved(!sample),
outside-price wake witnesses and global PCM play starts0. No cost attribution
from V1 or active PCM/audio-quality claim. Preserve original active polling.

audio-idle variant keeps accepted hybrid wheels ACTIVE in ALL arms, with
positive retained ordinary wheel batches, direct+paint/Hybrid2/ordinary roads.
AUDIOIDLE moving/held modes must match phase roles, actual idle16 ONLY ON,
idleHalf ONLY OFF, PCM ZERO; mode/reset epochs drop in-flight old-arm wakes.
Full image/pose/net gates remain. Active PCM runtime/audio/start latency are
separate unqualified surfaces, never claim them from this empty-worker test.

Hybrid wheel V3 FULL real PS2 gate PASS54 exact images/3942 poses, ON537
local car draws+3961 ordinary retained batches, net+0.512/+0.481ms BOTH orders.
57.68..57.79Hz/20..21 late intervals NOT60.1284-file/305-input archive exact
both reader replays PASS. Cross-ELF V2/V3 rates do NOT prove extra hybrid gain.

Hybrid wheel V3 FULL emulator gate PASS54 exact images/3942 poses, moving ON
1815 VU car submissions/7260 draws/2178000 vertices PLUS2683 ordinary retained
batches. Held1/4/1200. Source preservation is qualified for this fixture;
real PS2 net remains open. Same engine library as V2, fresh game ELF required.

wheelMovingOnly requires matching WHEELSTATIC witnesses, positive retained
ordinary parked batches during moving ON, and fewer candidate car draws than
all-car V2. Zero-speed cars still use current-state ordinary rebake logic,
never get omitted. Older all-car V2 manifests keep their original exact gate.

Wheel-local V2 FULL emulator+PS2 gate PASS54 exact images/3942 poses, net
+0.543/+0.495ms BOTH orders,57.58..57.68Hz/21..22 late intervals NOT60.
Input5397600 vertices is new-path volume, not removed EE transforms (old
still-rig skip remains). Added parked-car heads are included. Keep private;
moving-only hybrid is distinct next A/B, not already qualified/night/default.

wheels-vu price variant keeps direct+paint/Hybrid2/ordinary roads common,
requires six moving/eighteen held WHEELVU witnesses, ON four draws per car
and positive vertices, OFF zero candidate activity. Full inherited image/pose
gates still required before physical net. Cold local geometry preparation is
common BOTH roles; baseline old Motor District wheel costs do not price this.

V9 full real PS2 gate PASS54 images/3942 poses/ROADLIFE1/3/5 and early whole/
partial routing; net+0.132/+0.125ms BOTH orders,56.47..56.67Hz,31..33 late
intervals per537 gas. Small private gain, NOT60/default/night. Both roles have
common road owner preparation; never treat cross-ELF rate differences as net.

roadLifetime manifest requires exactly ROADLIFE phases1/3/5 with positive
ROAD old reader/replacement/old-new exact bytes/stale rejection; each must
occur before its LOGWINDOWBEGIN and after previous LOGWINDOWEND. Native alone
does not prove this gate, and earlier manifests must replay unchanged.

roadWholeReplay manifest requires one ROADWHOLE per ROADPREP witness, identical
phase/arm/activation and replayed+fallback==inside. Moving ON must have positive
early replay (>300); OFF must have zero replay/fallback. Older V6 manifests
omit this new gate and must continue to replay unchanged. Emulator equality
still does not qualify road old-bank-reader pressure or physical net/tails.
V8 FULL emulator PASS54 exact images/3942 poses, ON moving36307 replay/38
fallback each, held4/4; original V6 verifier still PASS. No physical gain yet.

Road-prepared price variant requires common direct+paint/Hybrid2, positive
moving/held PARTIAL emission only ON, original-package route audit outside
price at1100, zero producer failures/unready. Full54-image/3942-pose gate is
necessary before PS2 net/tails. Terrain old-bank reader witnesses do NOT prove
new road lifetime pressure; that gate remains separate. Generator V1 wrong
WorldMoving hook placement stopped before build; it lives in terrain_game.cpp.
V6 full actual partial GPU gate PASS54 images/3942 poses/47 owners/fail0/unready0,
first emitted ROADWIRE actual writer+transport equality outside price. No road
old-reader or physical gain yet. Archive runner conditionally retains road
helpers/snippets/generator; old paint report still replays byte-identically.

Road-package SDK fixture is cold EE writer qualification ONLY. New native
binding required; `verify-road-package-sdk.py FIXTURE` requires exact completed
48 bodies/4128 actual-writer comparisons/192 controls/48 owned witnesses,
all64 masks and both state/kick modes, then8 malformed reader controls.
ROADSDKDONE is this component's completion, NOT full moving raster/DRIVEDONE
or physical timing. New geometry is not GPU-executed; no lease/FPS claim.

`verify-road-package-plan.py OUT --engine FIXTURE/tyra/engine` freezes bound
actual packager/bbox source; only host ABI, trace/debug and VU0 leaf shimmed,
all probes OFF. Compares translated/rotated package routes, eight-plane guards,
ranges and program continuation; optimized+ASan/UBSan. This is cold-plan host
evidence, NOT actual SDK writer output/queue lifetime/GPU or PS2 performance.

World PC refresh reuses original calibrated A37 adapter/store on accepted
direct+paint/Hybrid2. OFF/997/OFF and1733/OFF/1733 moving, held OFF/ON/OFF,
sampler stopped before export/readback/reload. verify-world-cell-pc first replays
current54-image/pose gate, then raw12 exports/zero OFF/overflow/late IRQ/SDK and
negative controls. Original symbolizer can consume its pc-moving report after
copying matching debug ELF to fixture root. No emulator timing/exclusive-cycle
or scene60 ceiling claim; quantify observer cost and retain unknown/raw bins.
Current V1 full PS2 PASS22599 rows/54 images/9 controls; overhead0.115/0.116ms,
OFF~57Hz NOT60. Archive1319 exact price+PC and symbolizer replays. Function
mapping is qualified, many line locations unresolved: never invent line costs.

Paint-cells price variant requires positive existing moving paintVu only ON,
held actual paint3QW/head and whole-head delta,54 exact main/env/Hybrid2 images.
Both modes retain directCold=true/skip-A31 cells/ordinary roads. No hot observer
or triple-buffer import; inherited A13/A40 results are not current net proof.
Paint-cell V1 full current emulator/PS2 PASS3942 poses/54 images,599 moving
paintVu ON/0 OFF, held actual+3QW/head. Physical both-order0.887/0.872ms gain,
57.07..57.17Hz NOT60. Archive1275 hashes/305 inputs/exact both reader replays;
default/night/normal variable-dt remain open.

`verify-world-cell-direct.py OUT` freezes direct-cold component/independent
Plan+flatten oracles, tests840 range cases/12 source/14 fragment-budget controls
optimized and ASan/UBSan. Host mode does NOT compile the SDK adapter or prove
GPU/physical gain. Integrate with common skip-A31 terrain/ordinary roads; require
actual cold body-byte equality before same-ELF real PS2 net/tail qualification.
Direct-cold fixture uses offset1100 complete opposite-body oracle ONLY outside
price; phase/endpoints must show actual selected compiler calls,93 replacements
and positive old readers. Both roles keep cells/skip-A31 and ordinary roads.
Direct V2 full emulator/PS2 PASS120 body oracles/54 exact images/3942 poses,
113 selected calls/other0,93 replacements/21 old readers. Both-order net gain
0.465/0.399ms,55.0..55.2Hz, NOT60. Archive1274 hashes/305 inputs/exact both
reader replays; local boundary covers pre-price work too, not pure EE cycles.

Private `tyrax2-world-road-cell-fixture.py` derives qualified skip-A31 evidence;
price reader variant road-cells requires candidate road activation/zero fallback,
ordinary road zero candidate work and common terrain cleanup in all six roles.
Endpoint terrain cells stay enabled in every arm; road role alone alternates.
No default/performance claim from native compilation or emulator timing.
Road V1 full correctness PASS but PS2 NET FAIL-2.94/-2.99ms. Archive1269 exact
replays has testValid=true for correctness, NOT net success. Keep roads ordinary.

`tools/tyrax2-terrain-pass-presence.py FIXTURE OUT.json` audits qualified A44
skip-A31 source/logs, not target timing. All500 hashes/ELF/accepted PS2 report
must bind. Driving scenes0/1 have no terrain layer/AO/emission companions;
ground shadows are separate lifted subsets. Do not invent fusion activation
or generalize this absence to future maps. Log decoding preserves raw hash.

A44 four-view V1 fails direction3/main-display although candidate==return.
Ordinary-only control reproduces all36 exact transition signatures with ZERO
new submissions;99/108 full captures match, not universal candidate innocence.
Both rejected archives937 hashes/303 inputs/3 failing replays are preserved.
Additional --settle-frames32 fails all3 phases: direction3 candidate differs
from BOTH matching controls. Do not treat warmup as a qualified correction.
Compare matched ordinary-only/32-settle control; retain exact main/env/display
acceptance. A failed image gate is never a physical price.
Matched ordinary-only/32-settle V2 reproduces ALL108 full captured rasters with
ZERO new submissions. This is exact sequence equivalence, not stationary held
acceptance or a physical price. Diagnose ordinary test-state/source drift next.
World-source audit requires9 complete bounded raw captures, both solo/batch
coverage, stream-length checks and108 exact images versus uninstrumented matched
control. Exclude source-*.bin diagnostic output from launch staging. Seven
malformed controls are host/parser evidence only, not target execution.
Source V2 actual9 captures each31 records/404296 payload bytes; all fields
equal across arms. Observer gate FAILS105/108 exact (phase2 main33/34/35 each
2-byte delta). Keep rejected; isolate SAME-ELF OFF/ON before broad source claims.
Source-switch OFF/ON uses runtime source-control.txt and equal2MiB reservation.
Clone completed OFF --reuse-built; do not rebuild ON. Reader requires same ELF,
mode/control-hash witnesses and108 exact rasters. Stage has304 inputs with switch;
archive must retain switch manifests/config. No observer/FPS result until tested.
Write runtime control as exactly one byte (write_bytes), not platform text:
Windows CRLF rejects on target. V3 invalid input preserved; V4 reuses SAME ELF
with byte0. Host9 cases include CRLF rejection; not target image acceptance.
Cross-boot scene poses are not automatically identical: real CP0 dt drives world
AND vehicle physics despite frame-scripted gas. Source-switch V5 fixed-scene-step
overrides global dt/scale from first loop in BOTH modes, leaves real timers and
original clock update intact. Diagnostic only; qualify exact same-ELF images.
V5 observer qualifies108 exact OFF/ON images/9 raw35-record captures. Actual
command V1 qualifies9 one-prefix/548-call owned dumps and108 parent images.
Stationary gate still fails: Auto interleave1 probes order during held test,
an A18-known conditioning trap. Pin held order separately, restore on exit;
do not reuse FastJoin checkpoint (also changes shared env-basis invalidation).

Held-order control completes all3 strict view gates/108 exact images. Its
successful archive freezes1028 hashes/304 inputs and3 exact reader replays;
raw source/command dump inventory is not additional observer qualification.
Conditioned actual terrain-cell V1 passes all3 four-view gates/108 exact
main/env/display images with3/4/13/13 admitted cells and no fallback. Preserve
fixed-step/held-only order conditioning; moving activation, lifetime/pressure
and same-ELF real PS2 price remain separate required gates.

A44 moving correctness tools use world-moving-control.txt exactly one byte.
Build OFF once; ON --reuse-built copies completed same ELF/bound sources.
Both modes use fixed simulation/order2 and common slots, with gas-frame GS
readbacks. verify-world-cell-moving requires exact poses/schedules/rasters and
positive zero-fallback delivery. Deferred dirty pre-record preparation costs
remain visible. This is NOT a production Auto-order or FPS experiment.

Motion staging excludes moving-*.bin readbacks as generated diagnostic output,
alongside cell/source/command/tape captures; world-moving-control.txt remains
a real launch input. Do not deploy stale captured frames as authored assets.

Moving V1 failed display-count assertion because inherited A38 trip hook still
toggles3/2/3; held checkpoints alone force2. V2 --recover-hybrid-control accepts
ONLY this explicit failed OFF source, preserves it, pins actual2 each phase,
then rebuilds. Reader requires6 A44BUFFER witnesses. No timeout restart or
Hybrid3 display waiver. Exclude stale log/drive/presentation output on clone.

Moving V2 full same-ELF observer36 images/1971 pose rows EXACT, but25 fallback
per phase rejects delivery. Keep saved valid=false separate from108 passing
held images. Archive --moving-audit-off requires a hash-verified frozen OFF
archive and exact fresh moving reader replay, including expected rejection.
Launch count is303+sourceSwitch+movingSwitch=305, excluding diagnostic bins.

Incremental-cell fixture derives from V2 rejected-delivery/36 exact-image
evidence. Build OFF; ON --reuse-built must use same completed ELF. Reader
requires incremental config, one cold preparation,49+ local rebuilds, zero
deferred dirty/fallback and positive held old readers on ON, plus unchanged
pose/image gates. Do not weaken witnesses to promote a nonactivated route.

Incremental V1 subsequently PASS all36 moving rasters/1971 poses, zero fallback
and21 held old readers each phase. This common-work observer is not a net price:
production-role preflight must leave OFF ordinary with0 cell preparations,
ON pays own1 cold/93 local work. Require exact roles config/poses/images before
clean Auto-order physical A/B/reverse; never charge unused candidate compilation
to baseline and call that production saving.

Clean world-cell price V1 rejects unconditioned cross-phase camera shake;
vehicle endpoints alone do not prove identical camera. V2 resets shake phase
at gasStart without removing its math/amplitude, disables old ClipTrial packet
reference observer, compares3 endpoint roles at SAME pose after price rows.
probeCapture is LIVE reflection capture; require it positive, not zero. Stage
excludes world-price-*.bin. Emulator never supplies a net price/60fps claim.

Price V2 full real PS2 net/54 image/3942 pose/zero-fallback gates PASS both
orders,1.17ms saving/~47.5->53Hz, NOT60. price archive --physical-attempt replays
both emulator and PS2 reports exactly;1267 hashes verified. ICMP/ARP preflight
can fail while editor-equivalent ps2link reset/execee succeeds: not availability
proof. Preserve owned file server until actual completion/error; never restart
on observation timeout. Physical V2 server5288 stopped only after DRIVEDONE/read.

Price reader variant skip-a31 keeps both moving roles cell-active; parity means
old/new producer, not cells OFF/ON. Endpoints stay cell/ordinary/cell all phases.
Require positive eligible old A31 calls/successes, skipped0 and one cold/local
replacement each phase. Count no unready startup as supposedly skipped work.
Legacy Price V2 replay stays unchanged; do not subtract different ELFs.

Skip-A31 full emulator+PS2 gates subsequently PASS, old372 calls/93 successful
bodies versus new0, net0.21..0.25ms both orders, still~53.8Hz/not60. Physical
archive1268 hashes/exact paired reader replays verifies. File server35140 stopped
only after DRIVEDONE/read; no shutdown. Small gain does not complete goal.

A44 terrain GPU V2 qualifies three SAME-pose daytime repetitions:25 cells/43
passes/19 textures/39104 GS words, actual3/3 cells/no fallback,18 exact main/env
images. NOT diverse-view/display/moving/physical/FPS evidence. Reader8 negative
witnesses and844-file archive/303 separate inputs/3 exact replays PASS.
Stage excludes cell-*.bin diagnostic outputs. Native libtyra.a stays in WSL
mirror; hash-check it against native-binding before copying into an archive.

A44 complete-cell-fixture V2 native495-source/ELF6b7a1ab3...935d289 binds all
three backend methods, but has NO runtime terrain caller or GPU/FPS result.
V1 alias/integer-template compilation failure is preserved. Require complete
producer/pass delivery and main/env/display gates before hardware pricing;
old per-bag routing counters cannot establish whole-cell source equivalence.

A44 tyrax2-world-texture-fixture.py creates an actual bounded texture-manager reservation
backend; cold one-texture A44SLOTS gate is not whole-scene VRAM/GPU/FPS proof.
Require both victim tiers, real core/CLUT addresses, six controls and fenced release.
align3D inside recording is only a recorded barrier: release outside recording
after actual GS drain. V1 generator int-return span failure remains preserved.

A44 slots V2 native494/ELF3235af3a...474b29 and emulator2112-word slot/6 controls/
both tiers/epoch+GS release PASS. verify-world-texture.py adds8 malformed-witness
controls; archive --texture-reservation requires exact replay of both cold gates.
This does not prove full-world budget, sustained pressure or new GPU/FPS behavior.

A44 tyrax2-world-cell-sdk-fixture.py OUT FROZEN_PARENT inserts a one-time actual
EE SDK compiler check:12 five-pass C/TC list/strip cells and54 rejection controls.
Require native-binding and actual A44SDK completion log. It does NOT run the new
bodies on VU/GS or price a world-cell path; inherited drive FPS is not its result.
Use hidden emulator first. All held/moving presentation arms use original Hybrid2.

A44 SDK execution V1 native494/ELF8d5a5b5d...325f50 and emulator12 cells/60 passes/
270 parts/54 rejection controls PASS. Reader verifies source/launch/ELF and seven
malformed witnesses. Archive freezes cold gate and replays exact report; it must
not label these bodies GPU-executed or use inherited timing as a performance fix.

A44 verify-world-cell-sdk.py OUT --engine FROZEN_ENGINE --toolchain TOOLCHAIN
instantiates the actual cold SDK cell compiler into a real EE object and freezes
288 dependency headers. This is compile-only, not linked emitter execution,
decoded geometry, raster validation or FPS. List parts require sixfold reserve;
do not give them the native-strip72 capacity. No runtime/VRAM/GS lease binding yet.

A44 verify-world-cell-plan.py OUT --ee-cxx PATH freezes whole-cell SOURCE model,
840 optimized+sanitized cases/1680 failed replacements/16 controls plus EE object.
18-file cell-source-v1 hashes verify. This does NOT qualify actual VIF emission,
texture residency, PS2 execution or FPS; do not price or promote source tests.

A43 first physical run rejected at unchanged completion-window gate in a night
CONTROL (outside1); do not price or waive. Candidate retained waits1/day519,
1/night469 are not substantial activation. Archive --rejected-physical requires
actual strict rejection, emits valid=false and preserves full replay evidence;
998-file rejected archive reproduces physical rejection/emulator PASS exactly.

A43 --retain-ready varies only main withdrawal policy; copy/notifications ON
all arms. Component131/644 optimized+sanitized/five EE entries PASS. Full reader
requires20 negative controls,54 exact rasters, positive moving retained waits
on PS2 plus old native-ready/window/restore gates; component is not FPS proof.

A42 physical full54-raster/18-control/restoration gates PASS, moving late-ready
copies day2/night17. Notifications day no net gain; night~0.44-0.54Hz gain,
stable60 FAIL both. native-ready-game-v1 archive998 hashes/both exact replays
PASS. No default/reverse claim; next inspect early main withdrawal policy.

A42 fixture --late-ready keeps A41 copy ON all arms, varies only VIF/GS late-ready
notification. HELD START deferral qualifies actual native IRQ kind3; moving no
extra delay. Component runner now91 guards/five EE entries. Full A42 reader,
native/closure/raster/activation/PS2 gates required; implementation is not proof.

A41 late-ready V7 component90/644 optimized+ASan/UBSan and EE four-entry gates
PASS; v7 inventory hashes verified. Slow live linker I/O is a verified wait,
not a failed attempt. Full-game native-ready ISR activation/IRQ ownership still
required; keep old queue ISR OFF and never call queue/cache work from observer.

A41 late-ready component runner now expects90 adapter controls (32 delayed-ready
masks plus12 START/VIF/GS order/budget cases),644 packet cases and four EE IRQ
entries. Component success does not reserve DMAC/GS or prove game/FPS benefit.

A41 reverse physical passes54 rasters/16 controls and positive ISR each candidate;
day57.559/47.656/57.450, night51.247/40.074/51.230, stable60 still FAIL.
Both998-file archives hash-verified; exact readers replay both reports identically.
Do not call slight night p95 increase no-regression or default promotion.

A41 fixture --reverse uses ON/OFF/ON for moving/held, all Hybrid2. Checker source-
binds exact order, prices BOTH candidates vs middle OFF, mutates active candidate
for negatives. First-order game-v9 archive998 hashes/readers BOTH replay PASS.

A41 hybrid-copy-archive.py preserves both full54-raster reports and exact readers,
all bound sources/ELFs/IRQ audit and every immutable launch input for strict replay.
Select attempt/report filenames explicitly; no omitted-input hash waiver.

A41 checker --physical --physical-dir PATH selects a separate retry directory
(bin/log/launch pins). Preserve failed startup attempts; a user-confirmed reset
uses deploy -ConsoleAlreadyReset, unchanged ELF, no extra network reset.
Use motion-run.py stage FIXTURE --stage-dir PATH from original game/bin, NEVER
copy a prior deployed bin: it includes mutable exports and invalidates input pins.

A41 V9 emulator complete PASS:496 native files/54 rasters/1262 copy completions/
eight held ISR/16 negatives/linked IRQ closure. Require elapsed completion<288*512
Count ticks as well as edge ages (missed whole fields can alias ages). Physical
moving ISR must be positive before pricing. Stage excludes hybrid-copy CSVs;
never include emulator outputs in mutable physical launch input hash set.

A41 V8 HELD-only frame1 delay40ms is identical all arms, outside moving prices,
and preserves next unsubmitted clear prefix across old-job START. Require held
ISR AND fallback positive; physical moving ISR positive, no forced emulator
moving slowdown. V9 checker adds CT16 display equality:54 rasters, not only36
main/env. IRQ audit inspects six linked app routines/two constructor-bound
callbacks; source pins are required, not BIOS/WCET/physical safety proof.

A41 reader V4 complete36 rasters/baseline PASS then installed0 activation FAIL.
Common pre-capture retirement before begin gives first-gas(offset1320) flips0;
checker binds source and requires exactly0 there/1 elsewhere, never ignores all
flip counts. V5 finds write-only IMR/CSR mirror, V6 main GsGetIMR/frozen ISR mask
pending. Do not accept a complete emulator run with zero candidate activation.

A41 full-game V4 native496/ELF builds, emulator REFUSES activation (installed0),
so no PS2 price. V5 reports reservation state once outside steady-state. Native
symbol witness should use HybridCopyTrial::rows: adapter has an additional C++
guard-variable suffix match. New hybrid-copy-check.py inherits complete baseline
36-raster/travel/owner contracts and requires positive ISR/fallback,81qw packet,
owned paired job/FINISH and physical blank ages. Python compile is not reader
qualification; negative controls/full replay remain required.

A41 queue V4 runner now varies transient nativeInFlight independently of stable
nativePresentationSequence:65536 states/240 admissions; source guards bind final
publication and enqueue revoke order, next begin must NOT clear presentation.
Adapter V6 passes46/644 controls and direct completion-ISR relocation inspection.
Full-game V4 build pending; V2 shadowed inherited queueProof callback and V3
ambiguous generator anchor remain rejected private fixtures, never priced.

A41 hybrid-copy-fixture.py BOUND_A38_REVERSE OUT snapshots full game/engine,
actual queue predicate and immutable original copy with fixed Hybrid2 OFF/ON/OFF.
Need native binding and held full main/env output before PS2 pricing. Adapter
runner now expects46 guards and audits copyFinishISR as well as S/E direct
relocations. Trial witness hardware ages are NOT emulator price or universal
WCET; every physical copy and old/native ownership must qualify. No full-game
test result yet; preserve failed fixture attempts rather than overwrite/restart.

A41 verify-hybrid-copy-queue.py snapshots actual queue method and extracts it
unchanged:16384 states/60 admissions, no state writes, optimized/sanitized pass.
EE mock setup must remain mutable so pipeline=false cannot fold wrapper away.
This is not full engine linking, actual ISR/foreign-DMA/Core safety or FPS proof.
See adapter queue-v3 (EE loads/no-stores/calls gate). New API has no runtime caller yet.

A41 copy-budget.py BOUND_A40 OUT --toolchain PS2DEV_ROOT extracts actual GS methods
with pinned source; check.py --platform physical/emulator verifies packet columns,
complete120-copy trace, all200704 pixels and six negatives. Use aligned row raw
ps2_screenshot(address/64); rejected V1/V2 file-readback fixtures are not production
bugs. Physical median0.179562ms is isolated cost, not game/IRQ WCET. Replay uses
--output OUTSIDE archive. See adapter copy-budget-v3.

A41 window-preflight.py builds standalone signal/timer-only ELF; check.py validates
120 S/E edges, modes, restored state, source/ELF pins and six rejecting controls.
Both targets pass; physical blank1.4288..1.4306ms differs from PCSX2. Archive replay
uses --output OUTSIDE archive. No copy WCET/game/FPS qualification. Negative ICMP
alone is not ps2link failure: editor-equivalent reset succeeded in this run.

A41 verify-hybrid-copy-window.py OUT --ee-cxx EE_GXX snapshots actual predicate/
driver/runner before optimized/ASan+UBSan/EE builds;280800 positions/20000 safe
admissions/137 rejection guards and24/32-byte ABI pass. Bounds/epoch are synthetic:
not MMIO, timer reservation/polarity, copy budget or physical FPS proof. Archive
adapter window-v1; qualify real sampling and original packet before admission.

A41 runner --adapter V5:42 same-adapter mocked-hardware controls and644 extracted
actual EE packet-tail/bounds cases pass optimized/sanitized; real backend explicitly
instantiates in EE object with `_EE`/SDK headers and60-byte adapter ABI. Direct IRQ
relocation inspection is NOT full indirect closure. Preserve failed V2 host driver
and earlier component reports. Delayed START/END handlers cannot prove physical
blank budget; explicit window callback is mandatory and unbound backend refuses
copy. Core/queue/cache/lifetime/physical-window/native/raster/PS2 gates still pending.
See docs/tyrax2-hybrid-copy-adapter-2026-10-11/README.md; no runtime speed claim.

A41 verify-hybrid-copy-protocol.py snapshots header/driver/runner and uses structured
compiler arguments (quote sanitizer commas in PowerShell). V1 optimized and ASan/
UBSan each pass144 guards/161280 schedules; EE relocatable compilation verifies
36-byte protocol/12-byte readiness ABI. Models require atomic external transitions
and immediate VBlank-start ISR claims. Fresh-field fallback and completion evidence
retained until blank end are mandatory. No actual Core/MMIO/cache/INTC timing or
zero-sequence qualification yet. Preserve full inventory; source and model success
cannot substitute for adapter extraction/native/raster/physical blank-end gates.
See docs/tyrax2-hybrid-copy-protocol-2026-10-11/README.md.

A41 tools/tyrax2-vblank-sdk-audit.py binds492 current source files and exact
stripped/symbol ELF text before checking14 linked SDK instructions.32 CSR latch
model cases/four rejecting instruction controls are host/source evidence, not
measured deadline losses. VSINT cannot establish active blank. Next independent
Hybrid2 copy protocol requires atomic ISR/fallback and exact old-job FINISH/GIF/
source ownership, positive activation AND copy completion within blank. Main/env
readbacks alone cannot qualify scanout tearing. No target candidate yet; see
docs/tyrax2-vblank-sdk-2026-10-11/README.md. Do not repeat rejected A39 as this fix.

A40 paint-triple-fixture.py BOUND_A38_REVERSE OUT tests known A13 paint with
Hybrid3 ALL arms/current ABI, NOT A39. paint-triple-check keeps complete travel/
source/clip/36 exact-raster gates, permits ONLY prescribed+3qw per actual cold
paint head, and normalizes original SDK emitter against pinned parent source
after removing two cold-only observer edits. Require moving and ALL held paint
activation; unsupported fallback/basis invalidation stay live. V1 BOTH-target
36-image/17-control gates pass/native492; physical59.378 day/56.679 night, still
late5/29 and stable60 FAIL.617-file archive copied readers replay exact. Fresh
user-reset launch has no reset log; allow missing reset artifacts ONLY with
user-confirmed-reset provenance, preserving initial no-output deployment separately.
Reverse/macro0 image/production qualifications remain open, no default promotion.

Burnout buffer audit: tools/tyrax2-burnout-buffers.py reads existing six slots
and two dumps without launching/mutating the user's emulator. Use pinned ZIP CRC
reader plus existing freeze-v9/GIF parser; Counter target formats are strings,
fill scalar, draws separate. Saved FRAME and decoded DISPFB metadata corroborate
fixed CT32-to-CT16S conversion like Hybrid2, not alternating CT32. Snapshots drain
consumers and GS dump order has no EE wait durations: no physical pricing or
complete scheduling claim. See docs/tyrax2-burnout-buffers-2026-10-11/README.md.

A39 ready-fixture.py BOUND_A38_REVERSE OUT is private Hybrid3 BOTH arms,
Off/On/Off publication trial. verify-ready-guard.cpp tests the actual RAII header
on host; not target readiness/latency proof. ready-check.py adapts only all-three
buffer guards in inherited A38 full gates, then requires positive actual protected
prefix presentation in BOTH moving candidate phases. Do not price inactive or
partial runs. Native binding/full targets pending; no promotion.
V1 missing assertion include fails native. V2 builds but held per-frame drains
would mask candidate activation; not deployed/priced. V3 normal held overlap
and common40ms qualification-only post-clear pause in ALL arms require positive
protected-prefix activation all six held candidates too, outside moving price.
verify-ready-runtime.py extracts actual Core/queue methods; modeled4096 state/
sequence/wrap controls pass optimized/sanitized, not hardware completion proof.
V3 checkpoint FrameProfile name fails native; V5 direct CP0 pause plus explicit
try-ready before reflection fences builds. Host now4099 actual-method modeled
cases plus32 header states. Keep native source binding and positive ALL held
activation/full rasters mandatory. ready-archive requires BOTH accepted targets
and actual host source pins/logs; no archive/price from an incomplete run.
Hidden V5 then passes full six-trip/36 exact-raster/17 controls; all held30..31
protected, moving0/1 day/night. Reader allows inactive emulator MOVING only,
not physical or held. PS2 candidate must actually present protected prefixes in
BOTH moving phases. First PS2 day190/37106 checks/held31, full/net pending.
V5 subsequently closes BOTH full gates, PS2 moving190/340/held31,36 exact
rasters each/17 negatives;621-file copied readers replay byte-identically.
Net FAIL day58.815/58.701/58.813Hz, night54.338/54.013/54.450. Keep private
OFF/no reverse. Owned5168/26/15 stopped after export, devices ON. Next known
A13 combination in Hybrid3 must qualify current ABI/output/activation/net,
not sum old gains or treat source-safe publication as a performance success.

A37 PC sampling is NOT SDK gprof: prof.c samples last __mcount function entry,
not callback pc_value. tools/verify-pc-sample-store.cpp tests the bounded raw
store/delay policy on host only. Pin installed timer implementation and require
two-leaf target PC/EPC calibration, stopped-callback lifecycle and multiple
intervals BEFORE renderer interpretation. Never claim host tests qualify IRQ
safety or samples are exclusive EE cycles. Contract: docs/tyrax2-pc-sampling-2026-10-11.md.
tyrax2-pc-calibration.py BOUND_V7 OUT now implements shared SDK two-leaf alarms.
Actual native490/ELFa87be09e...af2c3, hidden375/physical372 rows PASS;97..100%
callback leaf hits, ZERO CP0 EPC leaf hits. Always use callback PC, never CP0
read inside callback as interrupted game PC. Pin installed/mirror libkernel and
forwarding disassembly; no -pg symbol. Five reader controls;519-file archive
replays both reports byte-identically. Staging excludes pc-calibration.csv.
No renderer distribution/observer bias/FPS claim until moving/raster gates.

A37 moving fixture: tyrax2-pc-moving.py BOUND_V7 OUT keeps A35 OFF/common A31/A30.
Each day/night997/1733us then OFF driving return; held OFF/ON/OFF exact main/env
gates.24576 reusable rows, release before export, no ISR allocation/IO. New
independent pc-moving-check.py requires full trips/owner/source/header/raster
and sample lifecycle; do NOT relax/reuse A35 activation reader. Symbolizer
checks native debug/deployed .text equality and raw unknown/kernel bins.
Coarse main-loop region is NOT thread ownership. Staging omits pc-drive/held CSVs.
Native/hidden/physical moving qualification pending; no new performance claim.

Moving V1 then passes native490/ELF185560b0...e7b9a, closed hidden30604/PS230492
samples,36 exact rasters each/15 controls.623-file archive copied qualifiers and
physical symbolizer replay byte-identically. Windows sample-pin separators were
fixed to POSIX before WSL symbolization. OFF return49.26/41.18Hz, observer is
not free; instruction shares aren't cycles. Actual Hybrid448x448/x2, VBlank
31..34% versus copyQwords5..6%. Next current-scene double/triple boundary A38,
not unlimiting/removing fences/claiming existing feature as new packet fix.

A38 tyrax2-triple-fixture.py BOUND_A37 OUT removes PC sampling, implements real
Hybrid2/3/2 settings/rebuild/drain, forces actual grant. Held arm rebuilds refresh
shadow owners outside price. triple-check.py reuses A37 ordinary travel/owner/
header/36-raster gate, independent buffer/event/net controls. Record actual
DISPFB calls with ISR flag; never confuse flip return/entryHz with photons.
No promotion until full targets/net/reverse. Staging excludes triple-present CSV.

A38 V1 completes native491/ELF108375b7...df5cf, BOTH six-trip/36 exact-raster/
14-control gates. Raw FRONTORACLE hashes change with relocated TEX0: reader pins
unchanged original head writer and retains counts/lengths/source/full rasters,
not impossible same-layout byte hashes.603-file copied readers replay exact.
Physical DISPFB58.928/55.217 day/night, baseline49.629..50.422/41.401..41.515;
still9/42 late intervals, no60proof/default. Next reverse3/2/3. Devices ON;
owned15024/emulator428/Xvfb417 stopped only after completion/export.
Reverse A38 uses triple-fixture.py --reverse: moving3/2/3, held2/3/2 unchanged.
Reader takes actual mode/ISR from manifest and prices BOTH triple endpoints
against middle double, not the old middle-candidate formula. Use separate
reverse fixture/archive; old V1 immutable. Full target gates still required.
Reverse V1 then passes BOTH/native491/ELF6f6ea95f...daae3,36 exact rasters each/
14 controls/603-file exact copied-reader replay. Display day58.818/48.836/58.815,
night55.104/40.832/54.889, both endpoint net gates PASS; stable60 FAIL10/10 and
43/45 late intervals. Owned6428/26/15 stopped after export, devices ON. No
production default; normal production boot is separate from private A31/A30.

A35 world-front V4 uses world-front-preflight.py: complete parent gates plus
positive early inside/partial activation and30 actual whole-original-head
oracles. Native488 and closed hidden60 rasters pass; physical preflight pending
client34280/editor reset. NO pricing/performanceCandidate/firstOrderGate.
verify-world-front.py OUT --fixture BOUND extracts actual shape/scalar checks
and tests2496 mutations/64 registry rounds optimized+sanitized, plus actual bbox
foundation. V1 TEST exceeded1024 cap; V2 releases a slot before populated move.
Pipeline/VU0/ABI shims are component-only evidence. verify-world-front-reader.py
passes1 positive/22 mutations on actual generated parsers. Full producer/net-
cost/stale/lifetime qualification remains; never price this preflight reader.
V4 physical then FAILS phase2 reload/freepad DMA Busy/no DRIVEDONE; full reader
rejects10 heads/20 rasters. Client34280 stopped, raw failed evidence preserved.
No accepted archive or partial pricing; hardware retry needs human power-cycle.
world-front-archive.py requires BOTH full accepted preflight reports, not price.
Extended verify-world-front.py also extracts actual producer method; explicit
SDK/compiler/cacher and M4 assignment ABI shims. Original V4 with injected bbox
bad_alloc terminates uncaught (producer-host V2), V1 host compile lacks ABI
bridge. V5 moves lookup inside try, native and expanded host rerun pending.
Do not infer target hang cause, actual SDK compiler fault recovery or speed.
V5 native488/ELFd38e2039...f86e4 and producer-host V3/V4 pass optimized/sanitized;
V4 records original method/core/owner pins.36-file host archive recompiles/runs
optimized read-only unchanged; never rerun creation INTO it. Hidden V5 PID46/
Xvfb35 live, full preflight pending; no physical staging/deploy or price yet.
V5 then completes hidden60 rasters/30 whole heads/47 controls, CLOSED full
preflight valid, no price. PID46/Xvfb35 stopped after exports.303 physical
inputs staged ONLY; no V5 launch. Human power-cycle required, no blind reset.
V7 adds world-front-admission.inc actual early renderWorldTicket stale/shape/
missing-owner rejection probes after sync/program reset, OFF-PRICE. Require
fresh supported ticket and current packet qw unchanged, restore before frames.
V6 native accessor objectDataPacket nonexistent; V7 packets[context], pending.
Opt-in preflight requires6 >=39-check records +5 rejecting mutations; generated
parsers1 positive/27 controls pass. Updated reader replays V5 report unchanged.
V7 native488/ELF9193dd24...a237e passes; hidden PID26/Xvfb15 running, first two
actual admission44-check records pass, full gate pending. No physical V7 launch.
V7 then passes CLOSED hidden60 rasters/30 heads/52 reader controls/6 actual
44-check admission probes (264 calls), positive moving routes/zero baseline
owners. PID26/Xvfb15 stopped after exports;303 physical inputs staged only.
Use V7 for next full physical preflight after human-confirmed power-cycle.
V7 then passes full physical six trips/60 rasters/30 heads/52 controls/264
actual admission rejections. Closed log requalified after owned37164 stop.
First archive rejects historical emulator idle-tail hash drift; full unchanged
closed emulator reanalysis passes. No preflight pricing or default promotion.
verify-world-front-switch.py extracts actual warm helper/Core wrapper/ticket
guard, with explicit storage/compiler/capacity shims:512 frames/4 controls.
tyrax2-world-front-net.py reruns full physical preflight, tests12 report controls
and computes net adjacent/spatial/tail contrasts without modifying preflight.
V7 first order FAILS: day0.008ms mixed signs, night0.086ms regression/worse p95.
Keep OFF/no reverse launch; initial load excluded, moving rebuilds included,
strip-only and common A31/A30 both arms. Evidence world-front-net-v1.
tyrax2-command-lineage.py ARCHIVE --output NEW_JSON decodes accepted A18
physical owned closures offline. Compile verify-command-lineage.cpp against
bound actual Vif1ChainCheck::Stream; pin used archive files and native nm
export for stripped ELF, exact resident-image matching, dense REF cursor/bounds.
Day759/night805 calls map, four corruptions reject. V1 missing symbol-table
assumption rejected; use pinned nm only. MVP runs are NOT confirmed owners;
UNPACK bytes are NOT copies/timing, VIF FLUSH counts are NOT DMA launches.
A36 is historical, not A35/current attribution. Next matched current capture
must retain synchronized freeze/ordinary-return main/env raster qualification.
`tyrax2-bag-tape.py OUT BOUND_V7 --current-v7` builds that private ordinary-only
capture bridge: A35 OFF, current common baseline, outer motion suspended,
per-frame presentation fence, forced live capture, one bounded A18 dense
closure. Descriptor-owner lookup is unpriced diagnostics, not command ownership
or an EE cost measurement. V1/V2 source-anchor mismatches reject generation;
V3 first native classifier used RuntimeObject.parts incorrectly; repaired to
the actual objectGeometry table. Native491 and BOTH targets later pass eight
ordinary-return rasters each, zero descriptor mismatches, one submission,
stable programs and4 report/raster plus4 actual-parser corruptions each.
Current physical657 calls; descriptor roads46.48% of main source vectors,
not EE time or emitted work.602-file archive copied readers replay unchanged;
physical decoder byte-identical. --current emulator/physical selects explicitly
qualified current reports, never historical pricing. Pose differs by real dt
across platforms. Staging now excludes inert old bag-identity.csv outputs.

A34 uses head-hot-fixture/analyze with common A31 hot bodies, positive head
moving witness, exact independently prepared cold heads and all parent gates.
V1 build wrong GeoPart field; V2 native490 completes emulator but FAILS retained
75 baseline words. V3 fixes silhouette/wheel/portal and baseline cold release;
native490 binding passes, fresh emulator pending. Do not waive zero baseline.
verify-head-hot.py OUT --fixture BOUND --sdk-host SDK_HOST copies bound owner,
extracts actual fixed-facts helper and tests128 ownership/budget rounds plus
16384 mutations optimized/sanitized. Pipeline host shims are not target proof.
verify-head-hot-reader.py exercises generated parsers1 positive/18 negatives;
decode nested A33 escapes before extracting its function. Hardware pricing
requires full closed-log/60-raster/source/lifecycle gates; no emulator price.
V3 finishes60 exports but final night baseline retains13 words; reject, no PS2.
V4 switches to intrusive actual-owner complete transition release. Re-run
bound host ownership tests (populated move/self-move/registry release), fresh
native binding and full emulator before hardware.22/30-file SDK/V3-host archives
recompile read-only unchanged, but do not qualify V4 registry or moving speed.
V4 native490/ELF2692b91c...369dc1c and bound128 owner/16384 facts host controls
pass optimized/sanitized, including registry/populated-move/self-move. Next
fresh hidden emulator full gates, then physical; neither V4 target started yet.
V4 subsequently passes closed hidden60 rasters/30 semantic heads/49 controls,
zero baseline ownership.303 physical inputs/same ELF deployed via editor reset;
client28308 running. Use head-hot-analyze on full closed console log before
pricing and head-hot-archive after both targets; no emulator speed evidence.
V4 physical passes60 rasters/30 heads/49 controls but regresses0.510/0.671ms
and tails: firstOrderGate=false, no promotion.684-file archive replays both
full reports exactly, hashes unchanged. Do not tune head keys; next A35 needs
full early-Core ticket activation/source/dispatch proof with original heads.

A33 verify-object-head.py OUT --sdk ACTUAL_SDK copies14 native SDK headers.
Host V3 checks24576 changed writes/61440 stale/command/alias controls optimized
and ASan/UBSan; manual UNPACK close only, host64-bit pointers/-fpermissive target
casts, NOT native auto-close/raster/FPS proof. V1 fails unavailable auto-close
link, V2 switches explicitly to actual manual helpers. Cold-only fixture uses
tyrax2-object-head-fixture/analyze, derives qualified A31 V8 and retains old hot
head serializer. Cold original bytes supply spot/fog/TEX0 values, so the oracle
qualifies serialization/execution, not independent numerical/producer admission.
The reader must forbid performanceCandidate, contrasts and firstOrderGate.
V1 include-anchor generation fails, V2 native489 binding passes; hidden emulator
pending. Full producer facts/lifecycle/direct-live-input/net-cost hot bypass is
required before any head speed attribution or production promotion.
A33 V2 then completes closed emulator60 exact rasters/30 head contracts/44
controls. Generated head parser1 positive/10 controls and22-file host archive
read-only recompilation pass. Cold source reset assertion includes HeadTrial.
Use object-head-archive after both targets, not a pricing archive/gate.
A33 V2 physical then passes60 rasters/30 heads/44 controls;678-file archive
replays both reports exactly. Remove BOTH inherited late physical/reverse pricing
blocks or physical analysis reads removed contrasts. Final reports MUST omit
contrasts/firstOrderGate and retain hotOriginalHead=true, performanceCandidate=false.
Current verifier checks no late assignments. Actual hot producer bypass remains.

A32 first runtime uses tyrax2-prepared-runtime-fixture.py and the full
tyrax2-prepared-runtime-analyze.py, not the foundation host reader. V1 native489
files, closed hidden emulator six trips/60 exact rasters/47 controls qualify
owned validation/capacity/bounds only; live heads/dispatch remain original.
Contrast combines A31 owner + A32 stage, not isolated frontend price. The global
8192-part budget includes replacement old+new. verify-prepared-runtime.py OUT
--fixture BOUND_RUNTIME copies bound headers/actual bbox sources and passes64
budget/replacement/copy/move rounds optimized/sanitized plus512 foundation cases.
Host V1 renamed-main compilation failed; fresh V2 includes explicit return.
Reader repairs inherited World-only release/no-witness Core assertions without
allowing mode-dependent Core routes, and creates report before adding controls.
Hardware net cost/raster gates remain required; never price emulator cadence.
A32 V1 later passes physical six trips/60 rasters/47 controls, but combined
0.141/0.235ms mean gain and0.898/0.651ms worse p95 FAIL first-order promotion.
Use prepared-runtime-archive;680-file archive replays both reports exactly.
No isolated A32 price,60fps/default or capacity ceiling. Close the combined
bounds-only policy; broader frontend work is not thereby disproven.

A32 verify-prepared-front.py OUT --engine BOUND_FIXTURE/tyra/engine pins11
actual bbox files and runs optimized/sanitized ownership/admission tests.
Host V4 passes512 cases/12288 changed views/15360 stale controls;23-file archive
recompiles read-only with unchanged inventory. VU0 min/max and platform ABI are
host shims on finite inputs, not target packet/raster/FPS proof. This is only
owned frontend facts: legal-bag adapter/producer hooks/live Core/global RAM
gates remain. Never rerun the fixture-creation driver INTO its archive.

A31 producer-owner runtime uses tyrax2-world-runtime-fixture.py, not the cold
body/mask fixture. V4 hits an inherited native-call assertion because owner
replay bypasses ordinary native calls. Preserve/reject that apparatus stage.
V5 scopes admitted full-owner sources separately from ordinary residual/header
observation; keep Core flushBuffers inside the scope, coarse in every cold arm.
world-runtime-preflight passes15 mutations but FORBIDS pricing: complete owner
travel/lifecycle/SDK/body proof remains required. WorldPayload decoder tests560
cases/5040 controls optimized/sanitized; actual-writer runtime pending. Source
digest must read decoded compiled payload, not just the pre-emission qbuffer.
V6 now binds487 source files and completes closed hidden six-trip/60-raster/
15-control preflight. It remains performanceCandidate=false; complete moving
activation/travel/lifecycle acceptance before editor-protocol hardware A/B.
A31 V7 moving witness activates, but both arms compile owner bodies on rebuild:
reject pricing because this hides candidate compilation cost. V8 must guard hot
prepare by candidate/cold, free baseline owners at warm boundaries and sweep on
epoch OR role transitions only. Use world-runtime-analyze, not preflight, with
worldNetProducerCost flag. Actual generated reader tests2 positives/21 controls.
verify-world-runtime-switch.py compiles the actual helper/producer guard with
host storage shims:512 steady frames, role transitions, baseline owner release,
candidate rebuild cost and3 rejecting controls pass. V8 native487 binding passes;
full emulator/physical acceptance remains required before price.
V8 subsequently completes both targets, six trips/60 rasters/38 controls,
net producer cost and positive moving owners.0.303/0.393ms net saving is NOT60fps
or pure EE cycles.670-file archive replays both full reports byte-identically,
inventory unchanged. Use world-runtime-archive; preflight remains nonpricing.

A30's cold coarse-strip oracle is tools/verify-coarse-strip-input.cpp with
tyrax2-coarse-strip-input.hpp and the A29 input header. Compare pre-route
ordered source to actual candidate delivery, independently expanded lists and
residual non-source jobs; retain baseline post-route equality and exact60
rasters. Do not simply permit changed candidate input hashes. Host execution
passes17920 cases/seven controls optimized and ASan/UBSan. The actual generated
reader test verify-coarse-strip-reader.py passes one positive/14 controls.
Runtime native/emulator/physical qualification remains pending. Deriving a reader
must strip only the final exec: A29's exec spelling also appears in its quoted
parent finish marker, so whole-string count/replace is invalid at this layer.
Audit final report assignment too: inherited A28 code overwrote A30's earlier
consumerInputEqual=false flag. Override the final assignment and test generated
flags; source/delivery/residual equality is NOT whole post-route input equality.
A30 V1 full emulator60 rasters/29 negatives PASS; physical qualification pending.
A30 V1 performance is subsequently rejected before pricing: its main-loop
false/true setter drains every frame. Preserve proof/raw output but do not price.
V2 must use conditional mode-change drains; verify-coarse-strip-switch.py compiles
the actual main-loop snippet, checks256 frames and rejects per-frame drains
and missing transition resets. Keep the real renderer's unchanged early return.
A30 V2 full emulator/physical60 rasters/29 negatives PASS; reverse pending.
After stopping a run, requalify its closed log before archiving: unpriced output
after initial analysis changes logSha256. Never weaken the archive hash guard.
654-file V2 archive reanalyzes exactly, inventory unchanged. --reverse keeps
coarse mode7; inherited A28 reversed semantic labels are not applicable.
Reverse A30 also requires quiet negative injection to use plan['modes'][1],
not hard-coded mode7 (phase1 is mode0 in reverse). Actual generated focus
reader tests both orders/two positives/eight mutations. V3 full closed-log
emulator60-raster/29-negative gates PASS; reverse physical price remains pending.
V3 then passes complete reverse hardware qualification,0.740/0.689ms savings
and improved tails, still46.4..46.8/39.7..39.8Hz.654-file archive reproduces both
reports exactly. Order is qualified, production lifecycle/default remains private.
A31 verify-world-strip-plan.cpp tests private owned geometry, not VIF commands:
360 cases/six corruptions/13 invalid inputs and failed replacement preservation
pass optimized and ASan/UBSan. Host leases are not actual DMA lease integration.
Require SDK/program emitted-word and main/env raster gates before physical price.
A31 cold actual-writer gate is tyrax2-world-body-fixture.py: original and owned
source bodies must flatten exactly, then compiled REF executes under live heads.
Storage resets after cold-arm drains only; this is NOT production lease wiring.
V1 missing ClipTrial include fails before link; fresh V2 includes clip_capacity.hpp,
native484-file binding PASS, full emulator pending. Host body controls=10;
actual-reader controls=6 and no-performance flag. Preserve full layered-reader
dependencies (host-v3 has15 files); host-v2's reader copy alone is incomplete.
A31 V2 completes emulator60 exact rasters/35 negatives and six positive compiled
cold execution records. Stop/requalify closed logs as usual. Its reader/archiver
must keep performanceCandidate=false and omit contrasts/firstOrderGate: ordinary
hot routes are unchanged. Same-ELF physical correctness now passes60 rasters/
35 controls too.664-file archive replays both reports byte-identically with
unchanged inventory. This is NOT a speedup or production owner/lease proof.
A31 all-plane cold follow-up uses world-mask fixture/reader, NOT world-body
reader (which rightly requires byte-identical masks). Require actual SDK body
equality except one packed mask word, changed-mask positive activation, exact60
rasters and unchanged performanceCandidate=false. Host controls15, reader4.
World-mask V1 host signature missed SDK unsigned bit14 and asserts in emulator;
preserve rejected V1. V2 requires exact6c02c000 UNPACK and STCYCL01000101,
plus a rejecting unsigned-flag control; no raster-failure inference from V1.
V2 all-plane native485-file binding and emulator60 rasters/39 controls pass.
This qualifies selected native cold buffers only, not full owner input/lifetime
or speed. Same-ELF physical correctness is running; preserve no-performance flag.
Physical all-plane gate now passes too;671-file archive reproduces both reports
exactly. Compiled-owner tests use verify-world-owner.py --engine-inc against
preserved actual engine headers. Only typedef/address shims are synthetic;
bank table is not copied logic. Host-v3 ten-file source/log inventory excludes
binaries; optimized/sanitizers/raw-fence/12 admission controls pass, no runtime
callback or speed proof. Recompile copied C++ with -DTYRA_HARDWARE_TRACE=0 -Iinc.

A29 private clip-ring host proof: tools/tyrax2-clip-ring-host.py copies/pins
the ACTUAL generator, derives list/native-strip bounded output and snapshots
RAM at EACH XGKICK. Final RAM alone hides overwritten ring outputs.
Host V6 checks 1280 cases/29392 triangles, all64 masks, both strip parities,
single/many colour, shared lighting/env/spot, immutable uploads and 3 rejection
controls. C positive lighting requires a normal stream even with a single
colour setting; V4's mistaken harness layout is rejected, not renderer failure.
Use --toolchain to require actual VCL/DVP assembly and scheduled NOP/XGKICK.
Image words C376/TC404 for strips are not whole-set residency or PS2 FPS proof.
Production code/defaults are untouched. See docs/tyrax2-clip-ring-2026-10-11.
A29 V8 adds hybrid legacy/native input, fold/paint:176352 host triangles PASS.
Runtime V1 native481-file binding and six hidden-emulator trips/60 exact
rasters complete. It is PRELIMINARY: old packet-shaped hashes differ and lack
a distinct native activation gate. Do not price or deploy V1 as accepted.
Use the canonical cold header in tools/tyrax2-clip-ring-input.hpp;4480 host
cases/four controls pass. V2 must retain SDK byte/state checks per header,
require identical ordered canonical triangles and positive candidate native
activation (zero in controls), but not identical removed header COUNTS.
Use tools/tyrax2-clip-ring-analyze.py, not A28's reader. It requires equal
canonical ordered triangles, positive native activation only in the candidate,
fewer positive SDK-verified headers, linked2034-word residency, bound source
and shader hashes, and all60 exact raster exports. Its rejection controls
exercise missing/inactive native contracts, native controls, zero triangles
and changed canonical input. V1 remains excluded; qualify a fresh V2 first.
The --native-fallback extension retains original package masks/order but
borrows unrefined/cache-unavailable partial strips too. Require positive
RINGFALLBACK only in candidates plus four rejecting fallback controls; the
V2 refined-only admission does not qualify ordinary unrefined road offload.
V3 completes these gates on emulator/PS2:26 negatives,60 exact rasters and
positive fallback; first-order gain0.649/0.667ms, NOT60fps or default promotion.
State-once host output uses one full GS state then geometry-only kicks.
Compare cumulative effective state plus each primitive descriptor; require
first state even when its polygon is empty. V10 passes264528 triangles/five
controls and selected394/466 images fit2038/2042. V9 oversize and partial
runtime V4/V5 symbol-guard failures are rejected, not deployment evidence.

A27 gas-only ordinary-log gate uses the A26 travel/raster harness but SDK
automatic header close in every arm. Window markers occur outside priced
travel; require positive baseline vehicle/terrain output and ZERO ordinary
LOG output in quiet windows, all six trips and all-arm exact raster/input
checks. No hot counters/clocks. A23..A26 logs are active despite profiler-off.
A27 V2 passes both platforms and first-order physical gate (1.060/1.163 ms),
but only 38.02/34.55 Hz, not goal completion/default promotion. Archive 639
files read-only reanalyzes byte-identically. Terrain triangle census and raw
periodic reports remain even with ordinary logs quiet. Next causal A/B uses
quiet BOTH arms. A corrected terrain LOD focus may change transitional
rasters: require held-focus return controls and neighbor edge invariants,
not false exact-candidate equality or destructive no-LOD geometry omission.
A28 private terrain-focus generator derives from bound A27 V2; pre-shake
camera target selects terrain only in mode7, both gas arms quiet. Cold all
arms share held stable focus, settle32 frames, verify own/four-neighbor LOD
contracts and exact rasters/input/header. Moving LOD changes are expected.
Use terrain-focus-analyze, not the old logging reader. V1 passes all-six
emulator/physical contracts and 17 rejection controls; savings1.735/1.500 ms,
NOT60. V2 --reverse keeps numeric modes0/7/0 but swaps intervention roles;
reader phaseRoles/reverse recomputes candidate/baseline/candidate contrasts.
Require both outer means/spatial means faster and max p95<=control+.15.
Never price reverse data with the forward formula or compare different ELFs
as a causal delta. Reverse native/emulator/physical qualification PASS, with
1.799/1.603 ms savings. Both archives reanalyze byte-identically. Production
1.173.5 host snapshots/streaming tests, Release editor, fresh FPP/orbit target
syntax and normal example native build PASS. Compilation is not hardware FPS.

A26 fixed-header uses A25's held-input harness but restores NORMAL clip dispatch.
All five checkpoint arms, including candidate, must have exact main/env pixels.
Require positive actual-SDK auto/manual byte/state comparisons in each arm and
sparse moving checks, native binding, hidden emulator, then editor-protocol PS2.
No new timer scopes. Do not price sparse-oracle or emulator frames.
A26 completes both platforms with six trips/60 images each, exact SDK bytes
and packet state, and 13 reader rejection controls. Physical first-order gate
FAILS (day/night mean saving -0.008/-0.050 ms): no default or reverse-order
qualification. Archive and read-only reanalyze outside the archive.

A25 clip-consumer derives from bound A23 via the shared A24 fixture harness,
then restores original Core/renderer and changes final consumer selection only.
Native-bind, hidden emulator, clip-consumer-analyze, then editor-protocol PS2.
Require all six moving trips, five cold input-hash arms per pose and exact
baseline/return rasters; candidate clipping-loss differences are expected.
Archive with clip-consumer-archive and reanalyze read-only outside the archive.
Both targets complete A25 with 60 images each and 13 reader rejection controls;
56 actual-header hash controls pass sanitizers. Physical consumer bypass has no
useful gain. The legacy groupRasterEqual report field means RETURN controls
only here; consumerInputEqual/controlRasterEqual name the actual gates.

A24 derives from bound A23 but restores original qbuffer gathers. Run the
capacity/range sanitizer harness, native binding and hidden emulator before
hardware. Ten trips use 0/7/0/8/0 for day/night: 7 groups true Clip ranges,
8 omits the full clip route. Sparse witnesses in all arms exclude offset%128
64 and 65 from price. Require complete travel/live captures, positive candidate
activation, zero clip submissions for 8, and exact 0/7/0/0 checkpoint images;
report arm8 raster differences as destructive attribution. Five checkpoint
arms produce 100 readbacks/platform. Do not promote no-clip or price PCSX2.
See docs/tyrax2-clip-capacity-2026-10-11/README.md.
V2 refined-only grouping has no physical activation; retain it for full
NoClip attribution only. V3 widens ordinary strips too and runs six 0/7/0
trips day/night, retaining five cold raster arms (60 images/platform). Native
binding, sanitizers (131072 capacity +730 winding cases), emulator and PS2 pass;
group gain is mixed/small and no default follows. All source revisions are
bound separately; never subtract V2/V3 timing or price their emulator cadence.

A23 uses list-join-drive from bound lean A16, EE paint and rejected target gate
off in every arm. Twelve full trips cover both orders/day-night; preserve real
dt, roads, three cars, live captures and camera sweep. Sparse moving byte
oracles and next frames are excluded from pricing. Post-trip Off/On/Off GS
checks settle eight frames with held scene/current capture, outside pricing.
Reader requires all trips, 72 readbacks/platform and eleven negative controls.
Gate requires positive mean/spatial savings and p95 <= slower Off +0.15 ms in
all four triples. A23 completes both platforms but gate FAILS in reversed day;
no default promotion. Settle console logs by stopping only the owned client
AFTER completion, then reanalyze/archive. Keep PS2/computer on. Read-only archive
reanalysis output must be outside the archive. See the A19 report's A23 result.

A22 uses tyrax2-list-join-fast.py from bound A18 V5: ordinary producers in all
arms, actual copy bypass in stages 2/4/6/8, gather byte oracle only on frame 222,
actual bypass rasters on frame 223, samples 64..191. No split scopes. Native
bind and use command-price-analyze plus its rejection controls. Never reuse
A19 oracle-retained timings as a speed result. See the A19 report's A22 section.
A22 completes emulator and PS2 night, 304 host checks, 18 reader rejection
controls and a 587-file archive with read-only reanalysis. Physical net saving
is 0.276..0.287 ms; frozen timing is not moving-game FPS.

A21 uses preparation-split --geometry from bound A18 V5, not from the already
instrumented A20 fixture. Native-bind the new build. The bound geometry define
selects 72-byte rows; the reader also retains old 56-byte A20 support. Run host
tests for both layouts and parser controls before hardware. Deferred flushes
belong to Geometry, including callbacks from Setup/Queue. See A20 report's A21
section; these scopes include cache policy and waits, not pure instruction costs.
A21 completes emulator/PS2 night with exact rasters and 28 negative reader
controls. Native binding/586-file archive reanalysis pass. Timed frames make
696 extra reads; net Timed-minus-Off is ~0.80 ms, not a uniform correction.

A20 preparation-split derives a fresh all-ordinary fixture from bound A18 V5.
Run verify-preparation-split.py under WSL sanitizers, native binding, hidden
emulator and command-price-analyze before editor-protocol hardware. The reader
detects the bound split header and requires all split rows/activation/balances;
verify-command-price-analyze adds split rejection controls. Finish is raw
elapsed including pacing, not additive to non-pacing whole work. Off/Count/Timed
controls price net observer perturbation, not a uniform correction. See
docs/tyrax2-preparation-split-2026-10-10/README.md.
A20 night now passes emulator and PS2 with stable within-run counts, equal
images, 24 reader rejection controls and immutable archive reanalysis. Counts
include same-kind nested entries; timed frames have 454 new reads. About
0.46 ms Timed-minus-Off is net perturbation, not a per-bucket correction.

A19 list-join is qualification only: the original gather remains and an actual
byte oracle precedes pointer submission in A18 descriptor-list stages 2/8.
Use tyrax2-list-join-analyze.py, which reports activation/rasters but no timings;
zero activation is an observation, not qualification of a working candidate.
The clone/archive tools preserve its optional manifest and reader. Source-owned
producer storage, non-recording lifetime and physical speed remain separate
gates. See docs/tyrax2-list-join-2026-10-09/README.md; do not deploy while the user
cannot reset PS2. Host helper tests use ASan/UBSan plus a corrupt-copy rejection.

A18 `tyrax2-bag-tape.py` is a private descriptor-list qualification prototype,
not a timing harness. Bind with symbol `BagTape::iteration` (the vector symbol
has an ABI suffix). Compare all main/env GS readbacks using bag-tape-analyze;
ordinary return must match too. `bag-tape-control.cfg` selects all-ordinary in
V3; bag-tape-clone creates a fresh replay run of that SAME ELF without the marker.
Never regenerate or alter source-bound fixtures to switch arms. Old V7
23.33/10.73 ms compares validation policies WITHIN replay, not normal vs replay.
V5 pins pre-freeze interleave order: otherwise the producer-free gap resets
automatic order probing and changes ordinary-return inputs. The shared stage
helper excludes tape readbacks from physical launch inputs; the analyzer checks
those inputs and any available descriptor comparisons as well as actual pixels.
The command continuation uses command-tape/pricing generators and the dedicated
command-price-analyze reader (never derive PS2 prices from emulator samples).
Hybrid presentation can leave GIF DMA live after synchronizeFrame; a common
post-presentation align2D fence is required INSIDE all measured arms. V2's
physical capture is rejected; its emulator raster pass does not qualify this
fence. Preserve resident-program and startup-state checks, complete mutable REF
ownership, ordinary returns, both intervention orders and off-clock GS readbacks.
The reader has 11 negative controls in verify-command-price-analyze.py.
V4 must suspend the inherited Engine drive sampler after freezing; merely
clearing sample flags once is insufficient. Final physical pricing rejects
unsuspended sources (an explicit legacy flag permits historical comparison).
Archive reanalysis uses --archive --output OUTSIDE the immutable archive and
verifies all archived hashes; omitted assets are checked against the preserved
launch inventory. A deploy status of console-output-received can be only stale
freepad output: require a fresh boot and completed test markers, not that status.
V4 physical day now passes, but night fails with std::bad_alloc BEFORE freepad;
the older V3 night log has the same precursor. Do not diagnose those runs from
the last error alone. V5 sizes the owned closure with command_size.hpp rather
than reserving 4 MiB; verify-command-size.py compares against actual arena copies
under host sanitizers. Preserve the hard cap and full target payload oracle.
PRICEHEAP is off-clock allocator bookkeeping, not total free EE RAM. Require
all stages and COMMANDMEM bounds; neither partial night prices nor emulator
success qualify the repair on hardware. Existing bound V4 files stay immutable.
V5 now passes four emulator states / 80 GS images and complete physical night.
The reader has 14 rejection controls for V5 (12 for older allocation formats).
Archive only settled evidence and reanalyze outside the archive. Hardware
later-view/parked and live-game performance remain separate pending gates.

A17 `tools/verify-env-normal-churn.py` runs under WSL with g++ and sanitizers.
It extracts the actual EE normal-preparation function and checks stable
colliding/noncolliding arrays plus legitimate mutations. It proves cache-slot
churn, not that the game experiences it, nor any PS2 timing or raster result.
Use the investigation register's census gate before changing normalization.
A17 `tyrax2-normal-census.py <new> <completed-A16-drive>` verifies the parent
binding, disables the rejected target gate and adds warmup/parked/driving
normalization counters without changing its policy. Native-bind before another
build replaces the mirror; run hidden emulator then editor-equivalent PS2.
`tyrax2-normal-analyze.py [--physical]` requires all six trips and count identities;
it intentionally reports no timings. `verify-normal-census.cpp` tests observer
accounting under host sanitizers. V1 failed linking unused TargetAudit counters;
V2 defines them in the common probe header, not only in discarded engine code.
A17 completes six emulator and six physical routes: 3,450 physical post-warmup
normal-preparation calls, all hits, zero roots. Do not reopen this as the current
drive slowdown without new workload evidence. Archive and reanalyze snapshots;
the live ps2client log may gain an idle tail after completion.

A16 `tyrax2-packet-audit.py` stages actual emitted TEX1 checks with a legacy
negative control; qualify copied packet bytes, not only desired filter fields.
`tyrax2-target-audit.py` stages the private OUTSIDE-only reflection gate and
paired env/main GS readbacks; `--drive` starts from completed A13 forward with
EE paint fixed in every arm. Packet/target and drive analyzers are separate;
only physical drive analysis reports prices. See the A16 evidence report for
coverage and unqualified cases. Never infer full-scene raster equality from
the isolated synthetic raster test or overwrite completed fixture evidence.

A13 `tyrax2-paint-qualification.py <new-fixture> <completed-A12-V2> [--reverse]`
removes heavy paint diagnostics/forced fallback/LOD and installs same-ELF
EE/VU/EE day then night controls (reverse selects VU/EE/VU). Explicitly compile
hardware trace AND frame profile out: A13 V1 inherited trace=1 and was never
launched. Native-bind, run the hidden emulator, then qualification-analyze.
The analyzer checks exact production-loop identity apart from declared hooks,
no linked diagnostic state, actual VU counts, live capture and complete travel.
Emulator analysis intentionally emits no prices. --target physical requires
editor-deploy.json plus pinned inputs; partial runs must fail. Both control
arms link PAINT images, so this does NOT price the whole-image tax versus flag0.
A13 V2 also completes six physical controls after recovery: about 1.0-1.2 ms
mean work savings, not sustained 60 Hz; see docs/tyrax2-paint-physical-2026-10-09.
Reverse is only partly emulator-tested, not physically deployed. For a physical
host that must stay alive, archive a read-shared log snapshot and reanalyze that
snapshot: post-completion log tails can change its hash without changing CSV
results. Keep the earlier report and require all other fields to agree.
Stop the owned emulator BEFORE
final analysis: A12's final log gained a harmless post-completion tail after
its original hash was recorded. An immutable-archive reanalysis under A13 host/
matches all results except that hash. Archive creation now rejects mismatched
final-log hashes. paint-drive-analyze --output supports read-only reanalysis.

A12 `tyrax2-paint-drive.py <new-fixture> <completed-A11>` verifies the entire
source binding before cloning it, restores throttle and ends only at z>=108
(or fails at the 1,200-row capacity). Authored moods alternate day/night/day;
camera sweeps, fallback modes, LOD and the accepted-basis oracle remain active.
Run paint-drive-analyze after native binding and hidden emulator completion:
it requires actual CSV travel/gas/camera coverage and moving EE/VU paint visits,
not just warmup checks. No timing result from these intrusive tests.
Use ONE PINE screenshot client at a time: a second socket may connect but time
out while the first is active. Do not restart a live emulator for this timeout.
Count dynamic lights only within [0,dynLightCount). clearDynLights resets the
count, not enabled bits outside that prefix. A12 V1's return-to-day witness
counts stale slots; V2 corrects the observer and requires day=0/night>0.
A12 V2 completes all trips: 8,981 paint visits, 1,138 at speed >5, zero errors,
both authored LODs/all modes. Ten lifecycle and five travel mutations are
rejected. Both attempts are preserved in a verified 1,039-file archive.

A11 `tyrax2-paint-game.py <new-fixture> <completed-A08>` uses rebuilt Dev
--refresh-gen on isolated FPP/orbit copies, then transplants the exact current
paint block into the preserved A08 driver with explicit diagnostic hooks.
Current engine/public capability and normal whole images are used, not C16
injections. Night is selected through the authored mood script; require roads,
active lights, all three reloads and zero PAINTLIFE errors. The existing
lifecycle analyzer recognizes paint-game-manifest.json and requires midnight.
Run verify-paint-game.py with the before template snapshot under Linux sanitizers
(6,000 transitions, three rejected mutations, default-OFF token equality).
verify-paint-generated.py uses the native compiler on BOTH generated physics
TUs/headers with flag 0/1. Native bind BEFORE another build replaces the mirror.
The PINE sky-capture helper must run in WSL (AF_UNIX), not Windows Python.
These intrusive tests do not measure speed or establish whole-scene pixel equality.
A11 completes 12,124 night-road visits and 62 held refreshes with zero errors;
both authored tiers/all modes and three scene loads pass. The 673-file archive
verifies. Driving remains a distinct next case; do not call the parked sweep a
full-throttle test. Use analyzer --output to recheck old evidence without changes.

A10 `tyrax2-paint-engine.py <new-fixture> <A09-V3> [--enabled]` runs the public
engine API on the isolated raster harness. Build both flags, bind each native
build BEFORE replacing the shared mirror with another: `tyrax2-motion-run.py
verify <fixture> --symbol paintRasterCull`. The ordinary verifier still requires
MotionProbe::rows; a missing requested symbol must fail. Keep the old private
motion header for unused generated TUs, without importing old renderer hooks.
Require PAINTCAP 17 checks plus null rejection and all 54 fog-off/on readbacks;
disabled means EVERY group actually uses EE. Run `verify-paint-engine.py` with
Linux g++/ASan/UBSan for actual capability/guard fragments and all 32 wrapper
flag combinations; its NDEBUG-return mutation must fail. Rebuild and run
--vu-check, four actual paint-image scalar/GIF oracles, and derived-image --check.
This does not qualify full-game lifecycle, night or physical performance.
A10 completed both builds: 471 bound native sources each, 54 exact readbacks
each, and exact pixels against A09. Disabled ELF links no PAINT images. The
default texture-bag declarations compare identical after preprocessing against
the before snapshot (`verify-paint-engine.py --baseline <engine-snapshot>`).
All 1,173 archived files verify. Historical C16/A08 injectors require their
pre-A10 source anchors; use a fresh public-API harness for game integration.

A09 `verify-paint-generator.cpp <new-output> <C16-engine>` links vugen, vuir,
vusim and vuasm. It snapshots ordinary output, checks 8,400 generated/private
draws including shared TC/TD, rejects unsupported/custom descriptions and three
real instruction mutations. Keep a pre-change snapshot: 45 ordinary files must
remain byte-identical. Run the actual-source `verify-vu-paint.cpp` oracle too.
`tyrax2-paint-generator.py <new-fixture> <authored-project> <generated-output>`
requires WINDOWS Python for the Windows editor staging chain. It selects
separate whole images with generated paint prepasses, preserving FOLD/ENVN
bodies and shared TC/TD/TCE ownership. Native bind/build and hidden emulator,
then the raster analyzer require 54 completed fog-off/on GS readbacks. Fog
must change every matching raster, and all EE/VU/EE groups must be exact and
nontrivial. `--output <dir>` writes analyzer reports outside an old fixture.
V2's 12-near fog range reduces two view-edge bases to 110/115 RGB colours and
fails the unchanged >128 nontriviality gate; V3 uses 24-near, not a weaker gate.
No performance, full-game night or physical qualification follows from this.
V3 passes all 54 cases: exact EE/VU/EE RGBA, observable fog, plus exact pixels
against A07 for the first 27. Both 473-file native bindings and all 1,336
archived files verify. The rejected V2 remains preserved, not overwritten.

A08 `tyrax2-paint-lifecycle.py <new-fixture> <authored-project>` adds private
accepted-basis/EE-readiness state, real clipping/override fallback toggles and
forced authored LOD tiers. Compile/run `verify-paint-lifecycle.cpp` with
ASan/UBSan, native build/binding, hidden emulator, then
`tyrax2-paint-lifecycle-analyze.py <fixture>`. The screenshot helper accepts
`--phases 3` for this run (its existing default stays six). Require every-vertex
EE oracle, zero basis/state errors, held-key refreshes, all six modes, authored LODs
and all three phase/reload completions. These heavy checks are NOT timing data.
V1 generation failed before build on a wrong include anchor; use V2 or newer.
The three authored cars have one far tier each (mask 3), not the two far tiers
of unused car assets left in bin. The analyzer reads TMDL layouts and binds the
expected LOD mask to authored vehicle IDs; do not infer coverage from filenames
or count a clamped request as a third actual tier.

A07 `tyrax2-paint-raster.py <new-fixture> <authored-project>` generates an
isolated C16 target-assembled shader harness: 27 EE/VU/EE cases, three bases,
inside/near-crossing/view-edge geometry, fixed texture and raw CT32 readback.
Run native build/binding, then the hidden emulator and
`tyrax2-paint-raster-analyze.py <fixture>` with Windows Python (numpy/Pillow).
Require all cases, actual cull/clip witnesses, nontrivial images, exact off/off
controls and exact EE/VU RGBA. Candidate uploaded colours are intentionally
wrong, so bypassing the prepass cannot silently pass. This does not qualify
game lifecycle, legacy/custom fallback or physical performance. Status is in
the investigation register; do not deploy while A06's console remains wedged.
V2 passes all 27 cases with exact RGBA. V1 is rejected: omitted TerrainGame's
clipMargin setup made every clip image blank. Set it BEFORE setRenderer, and
do not weaken nonblank checks. Include the SDK screenshot.h, not debug.h.

A06 `tyrax2-road-reduction.py <new-fixture> <authored-project>` derives a
234 -> 72 vertex draw-only plan from the union of BOTH archived A05 targets.
Run `verify-road-reduction.py <fixture>` under WSL/Linux, native source binding,
then `tyrax2-road-reduction-analyze.py <fixture> <csv-dir> <target-log>`.
Require runtime exact original-stream matches, all four plans in every phase,
candidate-only reduced counts, actual qbuffer consumption and sky refinement
OFF. The A03 validator's optional `sky_refine=False` is only for this distinct
policy; its default remains unchanged. A06 passes all six emulator phases, but
physical PS2 stalls with freepad DMA Busy during phase-2 reload after two valid
phases. Missing DRIVEDONE must fail validation; never price that partial A/B.
No reset loop or promotion. Preserve the owned file server on the stalled run.
Stationary image differences exceed off/off noise; no raster equivalence.
The stage helper excludes road-reduction-analysis.json as an output, not an asset.

A05 `tyrax2-road-geometry.py <new-fixture> <authored-project>` captures unchanged
road streams once at offset 1050, outside timing. Bind native sources, run the
hidden emulator, require ROADGEOMETRY valid=1 and DRIVEDONE valid=1, then run
`tyrax2-road-geometry-analyze.py <bin>/road-geometry.bin <target-log>`. This is one phase,
not an A/B performance run; do not use the six-phase sky analyzer on it.
RDA1 records little-endian chunk/run identity plus XYZ/W, STQ/W and RGBA floats.
The initial audit only accepts single-run, nonblended, nonedge, constant-colour
monotone UV rails with <=1e-5 world-position affine residual. That is a geometry
opportunity, not raster/depth qualification. Never rewrite collision receivers
or relax the error budget merely to make more chunks eligible.
A05 now passes emulator/PS2: four chunks, physical 234 -> 66 versus emulator
234 -> 39 vertices. Source float differences matter: never reuse emulator-only
selection indices without target validation. The offline full UV triangle
intersection oracle bounds world-surface error, not GS depth/pixel identity.
Evidence: `docs/tyrax2-road-geometry-2026-10-09/`. The stage helper now excludes
sky analysis and road geometry output files when staging after an emulator run;
they are results, not launch assets. Editor-equivalent deployment is unchanged.

A04 overnight follow-up: `tyrax2-sky-prepared.py <new-fixture> <authored-project>`
extends A03 without changing its routing policy. Run `verify-sky-prepared.py
<fixture>` under WSL/Linux (191,828 ranges plus A03 replay/fuzz), native build
and source binding, then emulator and physical `tyrax2-sky-prepared-analyze.py
<csv-dir> <log>`. Require geometry-build SKYPREP before timing, zero misses,
every measured candidate eligibility matched by a prepared hit, and cold
equality with A03's per-input classifier. V1 failed compilation before launch:
Tyra Vec4 assignment returns void; never chain its assignments. The host mock
now preserves that signature. See the investigation register for results.
A04 V2 passes host/emulator/PS2 but regresses turn work 0.195-0.242 ms;
not promoted. `tyrax2-sky-pixels.py <fixture>` records stationary ROI differences
and input hashes, not a moving/depth oracle. Its sky ROI is identical; world
differences remain, and drive A/B exceeds the off/off temporal control.
Raw evidence is in `docs/tyrax2-sky-prepared-2026-10-09/`.

## Physical PS2: use the editor deployment protocol

The source of truth is `src/runner.cpp::claimPs2Channel`,
`resetPs2Link`, `deployToPs2` and `stageReplayChannel` (checked 2026-10-09).
For normal projects use the editor's Run on PS2. For a frozen diagnostic
fixture, do NOT use `--build --run-ps2`: it regenerates instrumentation away.
Use `tools/tyrax2-deploy-ps2.ps1 -BinDir <fresh-staged-bin>` on Windows;
the helper mirrors the protocol below without building. It refuses other live
ps2client sessions; `-OwnedClientId` is only for an explicitly verified client
owned by this test. Never infer ownership from the executable name alone.

1. Stop only the owned file server BEFORE resetting (release tty and host polls);
   refuse an unrelated live session, never kill all ps2client processes.
2. Start a captured `ps2client -h IP listen` witness. Send exactly one
   `ps2client -h IP -t 10 reset`. On nonzero exit stop. Otherwise allow up to
   eight 250 ms intervals for witness output, then stop that owned listener.
   A successful but silent reset is SENT, not proof of a wedge.
3. In the selected bin clear `log.txt`, `livedbg.bin/cmd`, `frame.tga`,
   `livelogic.bin`, `livetime.bin/rst`, `livepad.bin`, plus
   `replay.in/arm/out/stop/st` when no replay was requested. Write
   `ps2link.run` containing `deployed by TyraX\n`; the marker is necessary
   because crt0 may not deliver the `-ps2link` argument.
4. Wait 40 x 250 ms (10 seconds). Start a captured, hidden, long-lived
   `ps2client -h IP execee host:<elf-name> -ps2link` with cwd EXACTLY bin.
5. Wait up to 15 seconds for console output. A dead client is failure.
   A live client with no log is UNCONFIRMED: KEEP THE FILE SERVER ALIVE.
   Do not automatically kill/re-send execee based only on silence. Initial tty
   output acknowledges a launch, not successful gameplay; check target markers.
6. Retain the file server while the game needs assets. Preserve launch inputs,
   ELF hash, PID and logs; release only owned processes after evidence export.

This supersedes older ad-hoc 8-second/reset/re-send recipes below. The protocol
does not prove the historical freepad DMA Busy root cause or guarantee recovery
from a real IOP wedge. A confirmed physical restart already at ps2link permits
the documented bare-execee recovery, but it is an explicit recovery exception,
not the normal editor launch sequence.
`tools/tyrax2-deploy-ps2.ps1 -ConsoleAlreadyReset` encodes this exception after
the user confirms recovery. It skips only reset/listen, retaining ownership
checks, marker/channel preparation, input pins, 10-second settle and keep-live
startup handling. The launch record names `resetMode=user-confirmed-reset`.
Never infer this permission from a silent or wedged previous deployment.

A02b `tyrax2-scene-capture.py <new-fixture> <authored-project>` extends P06
with nine bounded full-StaPip frames, owner/view/pass identity, actual EE
cull/clip calls (separate from logical replay), and raw clip input/plane bits.
Run `verify-scene-capture.py <fixture>` under WSL, then
`tyrax2-scene-capture-analyze.py <fixture> <csv-dir> <log>`.
Require zero overflow/errors and frame/bag/clip reconciliation. The classifier
is conservative: safe against eight planes, definitely outside a common one
of six VU planes, or boundary/uncertain; it is NOT a raster visibility oracle.
DynPip is outside this recorder. No production optimization is implied.

A03 `tyrax2-sky-bounds.py <fixture> <original-project>` is a private sky-only
tight-list-bounds turn/drive A/B/A. Run `verify-sky-bounds.py <fixture>` under
WSL, bind native sources, then `tyrax2-sky-bounds-analyze.py <csv-dir> <log>`.
The host replays A02b's 221 actual sky inputs and fuzzes 50,000 safety cases.
Require six positive cold SKYORACLE records with zero failures, candidate-only
applied decisions and exact eligible = applied + actual clip reconciliation.
`tyrax2-sky-capture.py <fixture>` reads PINE on the owned hidden Xvfb session
and captures stationary offsets 1240..1289 in each phase; start it immediately
after the emulator, before phase 0 passes the window. A still sky image is
not a full moving/depth oracle. Existing cull-first partition can change
dispatch ordering, so geometric safety alone does not justify promotion.
A03 V2 completed host/emulator/PS2 on the first editor-protocol deployment.
It is not promoted: turn work regresses 0.226-0.242 ms despite fewer clips.
Driving uses real dt and an endpoint; unequal frame counts are not paired
operation costs. Preserve the negative result in the scene work audit and
`docs/tyrax2-sky-bounds-2026-10-09/`. PINE may initially reject requests before
the guest is mapped; the capture helper retries that condition within its
bounded deadline. Do not restart the emulator or steal another PINE session.

A01 offline reconstruction: run `python tools/tyrax2-scene-ledger.py --output
docs/tyrax2-scene-ledger-2026-10-08 --selftest`. It verifies frozen P06 input
hashes, reconciles 72 frames / 2,220 road bags and rejects six corruptions.
This is not a new target run. See `docs/tyrax2-scene-work-audit.md` for missing
current-scene pass/plane/build-event coverage; the historical producer census
predates terrain refinement and must remain separate.

A02a `tyrax2-first-build.py <new-fixture> <authored-project>` extends unchanged
P06 with bounded per-road bake/finalize scopes, payload/arena sizes and existing
prefix deltas. Run `verify-first-build.py <fixture>` under WSL, then
`tyrax2-first-build-analyze.py <fixture> <csv-dir> <log>` (includes P06 gates).
Growth blocks are not allocation counts; build/finalize overlap parent timing.
This does not implement A02's full owner/pass/plane census. Check the audit's
actual hardware result before drawing conclusions or changing cache policy.

For this Vehicle Playground performance campaign, consult and update
`docs/tyrax2-performance-investigation.md` before a test. Add its question,
non-duplication rationale and decision gate; after execution record platform,
result, acceptance status and evidence. The register is the shared entry point,
not another raw log. Closed leads need an explicit reason to reopen.

P06 `tyrax2-road-trace.py <new-fixture> <full-source>` extends the P02 baseline
with whole-frame non-pacing wait leaves and native-queue counters, plus original
road ID/view records in three eight-frame windows per turn cycle. No grouping.
Run `verify-road-trace.py <fixture>` under WSL; analyze with
`tyrax2-road-trace-analyze.py <fixture> <bin-directory> <log>` (it also runs P02
validation). RAM records export only after each phase. Explicit wait time is
not all EE stalls; road-triggered copies may include mixed pending owners.
Require all per-window road Core calls to have records, off-arm zero metrics,
positive native submissions, no overflow/nesting errors and observer pricing.
P06 V2 had columns without optional expansion/partial-replay source hooks:
retain it as superseded partial evidence, never interpret those zeros. V3
requires installed hooks and positive cache activity. Partial baked replay
itself may legitimately be zero for expanded clipping (a cull-only cache).

P07 `tyrax2-road-pair-omit.py` uses original road IDs 24/34 and omits only
their unrefined partial-strip dispatch loop before qbuffer allocation. Run
`verify-road-pair-omit.py` under WSL and the turn analyzer with
`--vu-paint --roads-day --road-pair-omit --selftest`. Require both exact
66-vertex IDs and equal positive cold eligibility across off/on/off; only
mode 6 may skip. This deliberately removes triangles and is attribution,
NEVER a production optimization or raster-equivalent result.

P08 `tyrax2-clip-command-memo.py <new-fixture> <full-source> [--reverse]`
memoizes clip-TC stream commands for IDs 24/34, not geometry or the dynamic
header/kick. Run `verify-clip-command-memo.py <fixture>` under WSL. Analyze
with `--vu-paint --roads-day --road-command-memo --selftest`, plus
`--reverse` for the reverse fixture. Require positive cold actual-writer
byte checks/hits, zero mismatch, candidate-only applied witness and equal
eligible calls. Cold qualification executes in every arm; scoped clocks
remain off. Normal snapshots retain ownership of the current REF sources.
Host memo tests do not replace the actual target packet-writer oracle.
P08 V1 failed on untouched DMA PAD bits; V2 preserves destination PAD bits
and passes target equality, but is slower on PS2 and stays rejected.

P09 `tyrax2-strip-lookup-once.py <new-fixture> <full-source> [--reverse]`
acquires one expansion per selected strip loop. No new cache or cross-frame
pointer lifetime. Run `verify-strip-lookup-once.py <fixture>` under WSL and
analyze with `--vu-paint --roads-day --road-strip-once --selftest` (plus
`--reverse` for reverse order). Cold controls acquire 16 times per loop;
candidate acquires once and verifies 15 derived slices against the original
lookup. Require zero mismatch/fallback, same original IDs and positive reuse.
P09 was stopped after cold emulator checks on user redirection, with no
completed timing or physical run. A01 scene/packet audit now takes priority;
do not treat those checks as a measured improvement or resume automatically.

`tyrax2-road-prep.py <new-fixture> <full-source>` selects road-bag Core calls
through deferred execution, with normal road rendering and constant VU paint.
Clocks run only in the middle arm. Analyze with `--vu-paint --roads-day
--road-prep-scopes --selftest`; `partialTicks` means ALL selected dispatch
routes in this apparatus. Nested create/classify/fill/packet/submit/copy times
are exclusive, but submission/copy can contain mixed pending owners. They are
elapsed scopes without explicit-wait subtraction, not pure EE time.
Run `verify-road-prep.py <fixture>` under WSL for the actual header ledger,
clock-off/non-road controls, wrap, nesting and deferred-tag source checks.
See P01 in the register for qualification status; no production fix implied.
Add `--prefix` to the generator and `--road-prefix` to the analyzer for P02's
five disjoint pre-dispatch regions; do not add them to inclusive Core times.
`tyrax2-road-clip-memo.py` is P03's counter-off exact-MVP/near/far road-plane
reuse trial, with interleave pinned to 2 in all arms. Analyze with `--vu-paint
--roads-day --road-clip-memo --selftest`; require positive cold target hit/check
witnesses. `verify-road-clip-memo.py` compiles the actual candidate and original
bodies on the host with mutation and corrupted-output controls. No production
promotion is implied; use the register for the actual result.
P04 `tyrax2-road-groups.py` creates private draw-only groups of up to four
compatible opaque road strips. Original chunks remain the physics/receiver and
visibility authority; use a group only when all its members are accepted.
Analyze with `--vu-paint --roads-day --road-groups --selftest`.
Use `--reverse` on generator and analyzer for the on/off/on confirmation.
Generator `--pairs` selects P05's two-member coverage experiment; analysis flags
stay the same. Read its separate acceptance gate in the register.
Require equal original input counts, explicit padding accounting and a real
reduction in submissions. The host
`verify-road-groups.py` exhausts actual dispatch logic over visibility masks;
target load-time assertions compare all concatenated streams byte-for-byte.
This is not a raster/lifecycle or lit/night qualification; do not promote it
without the P04 gates and follow-ups recorded in the investigation register.

`tyrax2-road-submit.py <new-fixture> <full-source>` retains physical road
geometry/receivers but omits accepted road submissions in the middle arm.
VU paint stays on. Default is day; `--night-roads` keeps the midnight mood.
Analyze with `--vu-paint --roads-day --road-submit-off --selftest` (replace
the context flag for night). Require equal positive cold accepted/input counts,
complete middle-arm omission and unchanged poses/pins. This removes drawing,
including road reflection-view submissions, not a production optimization or
an EE-only timer. Do not equate it with deleting authored roads.
Its `--no-kick` mode keeps submissions but changes road GIF XYZF2 to XYZF3;
both retained and baked keys include the private bag flag. Analyze with
`--road-no-kick` instead of `--road-submit-off`; ROADRASTER's historical
`skipped` field means raster-suppressed bags, not skipped submissions.
`verify-road-nokick.py <fixture>` checks the actual substitution and both key
sites on the host. This preserves VU programs and packet lengths, not rendered
pixels; the contrast includes primitive setup/raster and downstream overlap,
and cannot by itself distinguish EE from VU execution.
`tyrax2-road-vu.py <new-fixture> <full-source>` holds road no-kick ON in
every arm and bypasses VU vertex work/output only in the middle. It retains
EE geometry submission and emits material state plus a zero-loop primitive
tag. Analyze with `--vu-paint --roads-day --road-vu-bypass --selftest`;
require ROADVU positive equal uniform counts and candidate-only mode 3.
`verify-road-vu.cpp`, linked with vuasm/vuir/vusim, checks the actual three
patched shaders: control memory/output equality and bounded bypass writes,
with a corrupted EOP negative control. Check native micro-memory residency
and runtime assertions before hardware pricing. This is VU plus output-path
attribution, not a pure VU arithmetic timer or a production optimization.

Private VU paint: `tyrax2-vu-paint.py <new-fixture> <source-project>` creates
0/6/0 (EE/VU/EE); `--reverse` creates 6/0/6. Analyze with the turn analyzer's
`--vu-paint --selftest` and matching `--reverse`. `verify-vu-paint.cpp`, linked
with vuasm/vuir/vusim, checks actual prepass and full-shader GIF output,
including nonempty clipped geometry and a specular-zero negative control.
Host math and moving screenshots do not qualify target GS raster equality.
No production VU paint path is enabled. See the reflection-turn report.
`tyrax2-road-single.py` compares ordinary road/junction many-colour versus
single-colour binding with constant VU paint and unchanged geometry. Use
`--vu-paint --roads-day --road-single --selftest` and matching `--reverse` in
the turn analyzer. Require ROADCOLORS eligibility, selection and cold draws;
spill/edge alpha streams must remain many-colour. This is a private fixture.
Its `--repack` follow-up retains chunk boundaries and compares every real
triangle's position/ST bytes before replacing storage; invalidate road indices.
Add `--road-pack` to analysis. `verify-road-repack.py <fixture>` sanitizes the
actual repacker and checks a corruption negative control. No arbitrary clip
capacity changes: obtain the real single-colour program capacity from Core.
`tyrax2-road-refine.py` holds VU paint on and compares only road refinement
off/on/off in the full daytime scene. Analyze with `--vu-paint --roads-day
--road-refine --selftest`; require positive cold road-partial/cache witnesses.
`--verify` enables range witnesses separately; do not price that build.
Matching `--reverse` selects on/off/on. Paint remains VU in both controls,
unlike the original VU-paint experiment; require all body submissions enabled.
`--night-roads` on the VU generator and analyzer selects the full seven-road
main scene at the project's ordinary midnight mood. Require NIGHTROADS
witnesses (hour 0, paused time, road chunks, active lights) in every phase;
this is still a parked sweep, not full-throttle driving.
Factorial follow-up flags `--roads-day`, `--night-no-roads`, `--day-no-roads`
use the SAME full seven-road source, skipping main-scene runtime buildRoads
after cleanup for the latter two. Removing authored references before generation
invalidates shadow bakes (zero pins); reject rather than weakening the gate.
Do not use the user's separate No roads copy as an exact
control: vehicle pieces/envLimits and Strix glass metadata differ. Require
the matching context flag in the analyzer; the NIGHTROADS label is historical.
Day witnesses require zero active lights. Run `tyrax2-factorial-analyze.py`
with day/no-roads, day/roads, night/no-roads, night/roads roots and an output
JSON path; it checks authored objects, staged assets, shadow/paint sources
and sampled poses before reporting contrasts. Price the complete night mood,
not individual lights, and do not confuse these parked sweeps with driving.
Strict cross-road pose equality fails because road omission lowers the car
and camera .12 units. Explicit `--allow-road-height-shift` validates that rigid
shift only (residual .00002); label the result a natural road intervention,
not pure drawing cost. Day/night comparisons retain identical world poses.

`tyrax2-reflection-turn.py` builds a five-phase stationary-car camera sweep:
normal / frozen paint / body pass off / frozen capture / normal. Use its
dedicated `tyrax2-reflection-turn-analyze.py ... --selftest`; CSV gas means
sweep, not throttle. Require positive normal paint counts and matching camera
paths before pricing. See `docs/tyrax2-reflection-turn.md`.

Ground-mask pin tests: `wsl --exec python3 tools/verify-ground-shadow-pins.py`
(use the absolute Linux repo path) compiles actual template reset/preload bodies
with ASan/UBSan ownership stubs. `tyrax2-hitch-probe.py ... --production-pins`
compares no scene pins / production pins / no pins on the road-free route;
require GROUNDPINS records and completed phases, not just successful startup.
Use `--all-pins` for the unchanged production call and three full-length drives;
analyze those with `tyrax2-hitch-analyze.py ... --full --selftest`.

`tyrax2-hitch-probe.py <new-fixture> <source-project>` narrows the road-free
drive to control/local-scopes/control around z=-53.593. Do not enable the
existing synchronous render-cost profiler: its fences change the workload.
See `docs/tyrax2-road-free-hitch.md`; inclusive nested intervals are not additive.

Road-free reflection attribution: `tyrax2-reflection-isolation.py <new-fixture>
<source-project>` preserves current geometry-cache defaults and uses one shiny
vehicle bake across normal/frozen capture and body-pass on/off arms. Nine drives
use 0/1/2/3/0/3/2/1/0; the dedicated analyzer validates actual gates and sparse
counters. Inspect saved source settings first: a road-free/matte copy may still
enable Live Debugger and Remote Pad. Never assume that means the preceding user
run had those settings. See `docs/tyrax2-road-free-reflections.md`.

For physical deployment, follow the complete editor protocol at the start of
this skill (`Runner::resetPs2Link/deployToPs2`), including channel cleanup,
marker, ten-second settle and preserving a live but unconfirmed file server.
A bare execee after repeated manual resets is not equivalent. Do not ask for a
manual reset before trying this normal path.

Terrain strip refinement: rebuild the editor, generate a scratch fixture with
`tyrax2-strip-subranges.py <new-directory> [--verify]`, native-build and bind
sources before another native build. Twelve phases compare A/B/A counted and
B/A/B counters-off for rotation and full-throttle driving; both geometry caches
stay enabled. Analyze with `tyrax2-strip-subranges-analyze.py <csv-dir> <log>`
(add `--oracle` for the witness build). Host ASan/UBSan:
`verify-strip-subranges.py --production`. Oracle/emulator timings are not PS2
prices. Preserve parked captures and compare world pixels excluding HUD;
this is not a moving full-frame GS oracle. See `docs/tyrax2-strip-subranges.md`.

`tyrax2-producer-probe.py` creates a private twenty-phase attribution fixture.
Use `tyrax2-producer-analyze.py <csv-directory> <runtime-log>` and
`verify-producer-probe.py` (host g++). Require complete phase logs, matching
row/bin ledgers, no deferred owner imbalance or scoped-clock leakage, and valid
parked/rotation/drive poses. Rotation deactivates vehicle instances and suppresses
captures caused by their still-resident materials; drive retains normal capture.
Do not use emulator timings as physical PS2 prices. Protocol:
`docs/tyrax2-producer-attribution.md`.

`tyrax2-static-plan.py` creates a private 36-phase fixed-view static-batch trial.
Use its dedicated analyzer and runtime log, never the old motion parser. Require
all6480 rows, exact semantic counts, cold physical oracle activation and zero
warm misses. Vehicle-free selected batches do not cover roads/terrain or shared
reflections. All36 oracle/emulator/PS2 phases pass; B is mixed and C only saves
hundredths of a millisecond with a mixed center contrast. No promotion or drive
follow-up. See `docs/tyrax2-static-plan.md` and its frozen evidence.

`--prepared-static` keeps both promoted geometry caches on in all six driving
arms, preparing/pinning static batches only in phases1/3/5 at offset1000.
Analyze with the same flag and `--activation-log=<matching-runtime-log>`;
require positive preparation readiness, both normal reuse paths, restored
parked controls and both actual-writer oracles in a separate correctness ELF.
Preparation clocks/logging are outside sampled windows. Six oracle, emulator
and PS2 routes pass correctness but show no useful gain; do not promote this
default-off policy. Analyzer controls reject16 malformed inputs, including
missing/empty/reordered preparation and control-arm preparation leakage.
The cache lifecycle host harness now runs62 checks including scene pins.

`--geometry-reuse` combines partial baked-body replay and strip expansion in
six driving phases (Off/On/Off with counters, On/Off/On without). Use the
matching drive-analyzer flag. Both actual-writer oracles can be enabled together
with `--verify-partial --verify-expansion` in a separate non-priced build.
Normal captures and TTL250 remain unchanged. Six oracle/emulator/PS2 routes
passed; combined default-on promotion is recorded in the full-throttle report.
`--geometry-sweep` keeps both candidates on for the vehicle-free camera protocol,
then visits dense/procedural/main at frames7200/7800/8400. Require scene witnesses
and GEOMETRYSCENEDONE at9000, in addition to MOTIONDONE and both byte oracles.
`verify-stapip-cache-lifecycle.py` runs 59 actual-cache ASan/UBSan host checks
with explicitly mocked DMA readers; it is not a target concurrency proof.

`--clip-followup` uses eight full-throttle phases (0,1,2,3,3,2,1,0), where mask1
is immutable strip expansion and mask2 is env-map barrier coalescing. Counters
are always on, no new clocks, normal captures/TTL250. The matching analyzer
requires complete routes, recording-mode env calls with no fallback, actual
expansion reuse and omitted barriers only in their arms, plus parked controls.
`--verify-expansion` is a separate actual-expander byte oracle, never a timing
build. These trials leave the earlier partial baked-body candidate off.
Corrected oracle/performance builds completed all eight emulator/PS2 passes.
The rejected per-chunk prototype failed parked snapshot controls despite zero
geometry mismatches; do not loosen the controls to accept residency pollution.
EnvFallback also counts GIF recording failures and frame splits in v3. Zero
excludes those and synchronous env calls, not every wait elsewhere. Host `all`
now includes the existing native frame-writer test (43 cases).

`--partial-baked` runs six normal-capture TTL250 full-throttle passes with
partial geometry reuse Off/On/Off then On/Off/On. Use the same analyzer flag.
`--verify-partial` is a separate correctness build comparing actual ordinary
packet bytes to cached bodies; require nonzero PARTIALCHECK and zero failures.
Never price this per-package verifier. `verify-tyrax2-host.py --only partial-baked`
tests its comparison oracle. Six oracle and six same-performance-ELF PS2/PCSX2
passes completed; the oracle reported at least 131,072 checks with zero failures.
The later combined geometry trial supersedes this experiment's default-off status.

`--submit-fix` in the motion fixture/analyzer runs 18 full-throttle passes:
fused snapshot/native emission, single immutable lookup, then both, each in
opposing Off/On/Off and On/Off/On triplets. All counters stay on, clocks off,
captures normal, TTL250. Require all endpoints/parked controls and successful
`fusedSubmit` counts only in the appropriate arms. Do not mix scope/freeze modes.
The generator applies archived candidate patches only to its private engine.
`verify-tyrax2-host.py --only count --submission-candidate` tests temporary
patched sources. PS2 completed only phases0-8 before a reload/SIF stall;
`--completed-prefix=9` explicitly salvages these triplets, never the missing
arms. Fused lost both orders; single lookup was mixed; neither ships.

Full-throttle follow-up: `docs/tyrax2-full-throttle.md` keeps authored vehicles,
real dt and chase camera. `--drive` uses scene reloads and spatial bins; never
compare its runs as identical frame-indexed trajectories. The endpoint and
lane/driver/speed checks must pass before interpreting performance.
Scene reload does not clear all baked occupancy: the 180-loop pilot reproduced
driving slowdown but contaminated return controls. Warm up for 1200 loops,
longer than the tested 1000-frame lifetime, and compare parked cache states.
Qualified PS2 drive controls average about 49 Hz counters-off, with 31-33 Hz
in the central segment despite longer TTL. Capture-bearing loops are a lead,
not an isolated capture price; entry periods describe the preceding loop.
`--freeze-capture` fixes TTL at 250 and freezes only valid shared captures
while driving in phases 1/4. Analyze with the matching flag; require actual
freeze flags, positive blocked eligibility and zero candidate captures.
`--packet-scopes` retains normal captures/TTL 250 and clocks Core plus its
Partial dispatch subset, with original wait leaves unioned separately.
Analyze with the matching flag. Clocks use Off/On/Off then On/Off/On; counters
remain on only for the first triplet. Never add nested scope totals together
or call elapsed-minus-known-waits pure EE instruction time. Pin wait sites and
require balanced scopes and route-count agreement before interpreting results.
Physical follow-up: Partial dispatch is about 4 ms averaged over the drive,
4.7-5.3 ms in central bins, with no entered measured wait leaves inside Core.
The 45-51 ms hitch near z=-53.38 is mostly outside Core (which stays near
12 ms there). Do not conflate those two costs. The scope ledger's deterministic
host test is `tyrax2-motion-run.py scope-selftest <fixture>`; it requires g++.
`--packet-breakdown` adds exclusive Create/Classify/Fill/Packet/Submit/Payload
buckets only inside Partial, with nested child time deducted from parents.
Use the matching analyzer flag; require bucket sum <= Partial and expose the
unassigned remainder. Coarse classification stays in Create, arena copies in
Payload, not Submit. Reprice the more frequent clock observer in both orders.
Qualified PS2 breakdown: Submit excluding copies ~1.15 ms plus Payload ~0.86 ms,
versus Create+Classify ~0.72 ms, inside Partial ~4.3 ms. Observer activation
adds ~0.19-0.25 ms to whole-drive means; no uniform per-bucket subtraction.
First-16 REF memo/counting already ships. Next inspect conversion/ownership,
not another proposal to add that memo or blindly borrow mutable references.

Vehicle-free camera motion census: `docs/tyrax2-motion-census.md` describes
the private generated fixture, six parked/sweep phases and count-only render
hooks. Keep FrameProfile/HardwareTrace off, export outside sample windows,
and distinguish view-dependent workload from temporal camera invalidation.
Repeated sweeps use a degree per loop, not fixed angular speed per second.
Cache-cause columns distinguish new entries and first-mismatch categories;
expiry/eviction counts are window totals, not per-entry retirement history.
Occupancy excludes graveyards and retained-command memory.
The `--lifetime` fixture uses 250/1000/250-frame refresh policies, then repeats
with counters disabled; analyze with `analyze-lifetime`. Each angular window
starts after 1200 warmup loops. Budget and DMA retirement stay unchanged.
Physical A/B/A removes the isolated rotation spike (192/0/192 misses), with
about 0.31 ms mean savings even counters-off. This is not a production policy
or evidence of stable 60 FPS in the original vehicle/night workload.

Strip-packing evidence: `docs/tyrax2-strip-batch-2026-10-07/README.md` records
existing model coverage and a private batch-run compactor. Preserve source-run
order, package overlaps and the GS provoking vertex; copy position/color/ST
together and rebind versioned arrays after swaps. Host topology equality does
not prove physical raster equality. Resident batch counts are not visible
submitted counts. Clear startup HUD transitions before every compared window,
keep the same sampler in each arm, and use 294,912,000 EE Count ticks/second as
the physical analyzers do. The 0.016 ms stationary daylight contrast does not
qualify night/driving performance or a production optimization.

Loop-balance diagnostics: `docs/tyrax2-loop-balance-2026-10-06/README.md`
separates pre-render update, render phase, leaf VIF/GIF/GS waits and existing
pacing. Reuse enclosing/pacing stamps, end GS scope before clear callbacks,
exclude queue flush/reset from wait spans, and record the actual pending job's
EE owner. Algebraic closure needs a separate source-coverage review. Render
includes its own logic/HUD/effects; elapsed work includes preemption. Account
dynamic extra Count reads separately and reject mixed/nested or crossing waits.
Opposing same-ELF observer contrasts do not justify a scalar tax subtraction.

Claude-trial boundaries: `docs/tyrax2-claude-trials.md` records twelve original
Core assertions (the inherited comment says thirteen), private bypass prices
with no production safety claim, and sparse nonempty-entered companion census.
Capture `clipPackageSize()` only after the original max-vertex setter; compare
semantic plane fields, not Vec4 padding. Native proof/source/ELF/symbol identities
must agree with current provenance. PMU overflow is an unmaskable exception;
endpoint rejection alone is insufficient. Refuse unknown active owners, qualify
enabled lifetime before runtime, and label D-side events as including uncached
loads. A no-log/no-loadelf console start is not a renderer runtime failure.
Owned TEX1 prototypes must preserve sendObjectData-before-setInfo filter
semantics and generic mutable LOD fallback; Core ownership does not make a
public opt-in compiler-enforced. Target class growth and common selector cost
need separate qualification. Eleven authored night-only boxes remain a geometry
difference even when lighting effects are cut.

Hardware follow-up: both TEX1 orders favor the private candidate but do not
price common class/selector footprint. Hardware census bbox/rebuild counts
differ from PCSX2; never transfer emulator counts into an EE cost claim.
Night main-only batching must preserve solo reflection carriers and reconcile
dirty selected members before secondary views clear dirty. Forcing batchStatic
alone is unsafe. PMU bits4/14 select USER; actual operation-mode activation
is an additional runtime gate, independent of overflow/lifetime.

Object-data attribution: `docs/tyrax2-night-shift.md` records five disjoint
active scopes and both physical orders. An absolute sum of packet cursor QWs
is not incremental write traffic; reset may decrease the cursor. Cold zero
clip-capture waits are not a timed-window census. Validate target comparisons
only over initialized semantic outputs, retain rejected runs, and keep net
observer tax separate from the individual scope costs.

Private night producer observer: `docs/tyrax2-night-producers.md` records five
disjoint preparation scopes, sparse workload counters outside 800..1119, and
both physical On/Off orders. Keep additional scoped Count reads separate from
the inherited ordinary sampler countReads field. Timed calls qualify priced
activation; sparse bytes/change flags do not qualify all-window writes.
Enabled observer contrasts can be confounded by drift; do not uniformly
subtract them from scopes or describe common disabled code as zero overhead.

> **A note on `PROGRESS 123` citations.** They point at numbered entries of
> `PROGRESS.md`, retired at ~15 800 lines. They remain exact pointers — the file
> is in git history, and `docs/backlog.md` has the recipe. New work records
> itself in its commit message and PR body instead.

Generated-game split verification: build both FPP and orbit fixtures with the
native backend, boot them in a private PCSX2 instance, and inspect scene/menu/
vehicle state. Syntax-check historical example subsystems with the PS2 compiler.
Check that mutable helper symbols/cache guards have one final linked instance;
check marker-owned upgrade, owned legacy preservation and owned split refresh.
Time active ambience keys and a real project preference, separately from
ordinary object moves/additions and no-op builds. Jobs use host-visible logical
CPU counts; never describe the measured `-j24` as a fixed project setting.

There is **no committed test suite** (no CTest, no test/ dir). Verification is
layered: compile → codegen inspection → PCSX2 boot → visual/log/audio checks.
Use the cheapest layer that actually exercises your change, and be honest in
your commit message and PR body about which layer you reached — the established
wording distinguishes "verified in PCSX2" from "compiles, needs a pad test by a
human". (That record used to live in `PROGRESS.md`, retired at ~15 800 lines;
the honesty convention outlived the file.)

**If the thing you need for a clean test does not exist, ADD IT - now, in the
same change, not in a backlog entry.** "The start pitch cannot be authored, so
I aimed the torch with a pad script" cost an afternoon of guessed `stick r`
pushes before the one-line fix (the Player's rotation X is the pitch, 1.66.0,
docs/player-start.md) made the fixture a JSON edit. The pattern recurs: a
constant that should be a setting, a state only reachable by a human at the
pad, a value the game computes but never logs. Each one is a small, honest
feature (or a `--flag`, or a `TYRA_LOG` line) that makes the test repeatable
for everyone after you - and an A/B that depends on a hand-driven input is not
an A/B. Ask "what would make this a file edit + one command?" and build that
first; the user has asked for exactly this.

**Frozen-camera fixture** (every PCSX2 A/B of a rendering change): on the
Player object set `walkSpeed: 0`, `lookSpeed: 0`, the `position` that frames
the shot and `rotation: [pitchDownDeg, headingDeg, 0]` (rotation X is the
pitch, positive = down - docs/player-start.md), plus `"keyboardMouse": false`
in the `.tyra`. Then `--build <dir> --run`, wait ~12 s, `-PrintWindow`
screenshot. Same frame every boot, no pad needed.

For observer calibration, an identical ELF does not guarantee identical
workload or memory placement. Archive configuration bytes before launch; use
matched camera/state/dt and address controls, compare both arm orders, and
keep rejected runs. A continuous-scene/common-buffer fixture must export and
finish the whole-loop sample before buffer reuse. Interpret the result as net
routing performance, including existing waits; it is not a compiler-pruned
equivalent or an automatic timing correction. See `docs/hardware-profiler.md`.
Repository JSON acceptance records under `docs/` are pinned to LF so
cross-record SHA-256 references survive Windows/Linux checkouts. Preserve
original bytes for private device logs and exported artifacts instead.

## Layer 0 — build the editor

The editor builds and runs on **Windows and Linux**. Pick the script for the
box you are on; they take the same flags and do the same things.

```powershell
./build.ps1          # → build/tyrax-editor.exe (fetches missing vendor deps itself)
./build.ps1 -Run     # build + launch the GUI
./build.ps1 -Clean   # full rebuild
./build.ps1 -Dev     # -O1 iteration build → build-dev/tyrax-editor.exe
```

```bash
./setup.sh --deps    # one-time on a bare box: EDITOR toolchain + dev headers,
                     # AND the game half (curl/tar/rsync) - one command, both
./build.sh           # → build/tyrax-editor
./build.sh --run     # build + launch the GUI
./build.sh --clean   # full rebuild
./build.sh --dev     # -O1 iteration build → build-dev/tyrax-editor
```

A clean Release build is ~1-1.5 min on 16 cores; a one-file edit to the UI is a
few seconds. **`-Dev`/`--dev` is for the edit-compile-look loop** — `-O1`
instead of `-O3` (which is about two thirds of the compile time), in its own
`build-dev/` so alternating with Release costs nothing. Two rules: never
benchmark or ship a Dev build, and **never use one to verify anything that
bakes** — gibake/matbake/aobake/pngquant are raytracers and quantizers, and at
-O1 a bake that takes seconds takes minutes, which reads as a hang rather than
as the flag you chose. Verify bakes with the Release build. If ccache/sccache
is on `PATH` CMake uses it automatically, which is what makes switching
worktrees cheap.

**Timing a build honestly:** back-to-back clean builds on a laptop drift ~20%
from thermals alone, so a single before/after pair proves nothing. Alternate
the two variants (A, B, A, B, …) and compare like rounds — and read
`build/.ninja_log` (`start_ms end_ms mtime output`) rather than guessing where
the time went: it tells you the per-target durations, and summing
`end-start` against the wall clock gives the real parallelism, which is how the
26k-line-app.cpp critical path and the libimgui.a ordering stall were found.

`build.cmd` / `setup.cmd` exist for plain `cmd.exe` and double-clicks. They are
**wrappers only** — they map `run`/`clean`/`dev` onto `-Run`/`-Clean`/`-Dev` and forward to
`build.ps1`/`setup.ps1` with `-ExecutionPolicy Bypass`. Never give them logic of
their own: they used to carry a hand-copied dependency list, it drifted behind
`deps.ps1`, and a fresh clone got "vendor\tyra is not an empty directory" from
setup plus "Cannot find source file: vendor/ufbx/ufbx.c" from cmake.

Needs `scoop install mingw cmake ninja` (Windows) or `./setup.sh --deps`
(Linux — apt/dnf/pacman/zypper, the lists live in deps.sh). **The Windows
compiler is MinGW-w64 GCC; MSVC is not a supported target and no flag makes it
one** — `src/templates.cpp` holds the PS2 templates as raw string literals far
past MSVC's hard 16380-byte cap per literal, so Visual Studio's default
`x64-Debug` CMake preset dies with a wall of *C2026 string too big* (plus
C2589/C2660 in `wire.cpp`, where `windows.h`'s `min` macro eats `std::min`).
Report of that error means "you configured with the wrong kit", not a code bug.
build.sh checks
the tools and the pkg-config headers up front, names the exact install command
for the distro it is on, and refuses to configure rather than failing later
inside cmake. This is also the compile check for everything under `src/` —
warnings matter, the build is expected to be clean.

**Only one platform's compiler runs at a time, so a cross-platform change is
only half-checked until the other side builds too.** Anything touching
`src/platform.*`, `wire.cpp`, the Runner, CMakeLists **or any of the paired
build scripts** needs a build on both, or say so in the commit. The pairs that
must move together — `deps.ps1`/`deps.sh`, `setup.ps1`/`setup.sh`,
`build.ps1`/`build.sh`, the `if(WIN32)`/`else()` halves of CMakeLists, the
`#ifdef _WIN32`/`#else` halves of `platform.cpp` — are listed in
tyra-editor-dev ("Platform parity"). Editing one side only is the single most
repeated way a change lands broken on the platform its author doesn't use.

**On Windows, `build` is `build.cmd`.** PATHEXT resolves `.CMD` before `.PS1`,
so the bare command runs the wrapper, which just calls `build.ps1`. That is a
deliberate one-line delegation now — it used to be a full cmd translation with
its OWN four-entry dependency list, which is how a tree that built fine on
Linux died on Windows with `fatal error: miniaudio.h: No such file or
directory`: the guard that fetches missing dependencies was in `build.ps1`, and
`build.ps1` was not what ran (PROGRESS 214). When a Windows build fails on a
missing `vendor/` header, check WHICH script ran before suspecting the code.

**Third-party dependencies live in exactly one list per platform: `deps.ps1`
and `deps.sh`.** `setup.ps1`/`setup.sh` fetch from them and `build.ps1`/
`build.sh` probe them before configuring, so adding a dependency **to both** is
all it takes — the build guard picks it up for free and fetches it on the
next build. Add one anywhere else, or to only one of the two, and you recreate
the bug this arrangement exists to prevent: the lists used to drift, and a
worktree that predated a new dependency reached cmake with the sources missing.

Each entry is fetched at a **pinned commit**, not a branch (`git init` + `git
fetch --depth 1 <url> <sha>`, since `git clone --branch` refuses a SHA), with a
fallback to our mirror fork. This is what makes a build reproducible, so when
you are chasing "it worked yesterday", `git -C vendor/<dep> rev-parse HEAD`
should always equal the SHA in `deps.sh` — if it does not, that checkout
predates the pinning and is stale. Fix it by deleting the directory and
re-running setup, not by pulling in it.

So when a build dies with **`Cannot find source file: vendor/<something>`**
(usually followed by `No SOURCES given to target: tyrax-editor`), it is not a
corrupt checkout — that path simply isn't on disk yet. The build script
normally fixes it by itself; run `./setup.ps1` / `./setup.sh` directly if you
want the fetch without a build. The same applies after merging a branch that
added a dependency: the merge brings the CMake reference, not the clone. Probes
are real source files, so a half-finished clone reports as missing rather than
sneaking through — delete the directory and re-run setup when the guard says a
probe is still absent after a fetch.

## Native incremental build regression

On Windows compare TYRAX_NATIVE_DIRECT=1 with the default WSL filesystem
cache, warming each separately. Verify bin/ returns to the authored project,
a Windows-written runtime channel survives, and DWARF paths map to authored
sources. Use a private PCSX2 capture slot to boot the resulting ELF.


Use a scratch copy of `examples/vehicle-playground`. Warm the native build, then
repeat `--build`: no compile/link/archive commands and unchanged ELF/archive
mtimes. Change one script source, then one scene value, and inspect which objects
rebuilt. Touch the engine archive to verify another project's engine rebuild
relinks the game without recompiling it. Change a Makefile to verify flags
invalidate objects. A failed engine compile must fail again on retry until fixed.
For IRX ABI changes compare objcopy binary payloads and check the link has no
abicalls mismatch warnings. Toolchain identities must match for byte-identical
source trees at different absolute paths. Exclude setup/cold builds from timings.

## Layer 1 — headless CLI (no GUI needed)

(`build\tyrax-editor.exe` on Windows, `build/tyrax-editor` on Linux — written
`TYRAX` below.)

```
TYRAX --new <name> <parentDir> [width] [depth] [empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]
TYRAX --build <projectDir> [--run] [--rebuild]   # exit code 0 = success
TYRAX --resave <projectDir>          # load + save, no Docker
TYRAX --migrate <projectDir>         # backup + apply format migrations
TYRAX --refresh-gen <projectDir>     # regen sources, no Docker
TYRAX --bake-gi <projectDir>         # bake global illumination, no Docker
TYRAX --bake-status <projectDir>     # read-only: is every bake cache fresh? exit 3 = stale
TYRAX --bake-model-ao <projectDir> [--texbake]   # per-model self-AO, no Docker
TYRAX --bake-prelit <projectDir> [sceneName]     # re-bake STALE pre-lit objects
TYRAX --dump <projectDir>            # JSON project summary
TYRAX --atlas-report <projectDir>    # what texture atlasing packed, refused (with reasons), and costs
TYRAX --batch-report <projectDir> [scene]  # how static objects batch, and WHY each one that does not (docs/static-batching.md)
TYRAX --chat-prompt [projectDir]     # what the AI Assistant is told (docs/ai-chat.md)
TYRAX --list-nodes <projectDir>      # what the graph generator is told
TYRAX --dump-graph <projectDir> <object> [scene]
TYRAX --apply-graph <projectDir> <object> <g.json> [scene] [--append]
TYRAX --vehicle-check                # drive-model property tests, exit 0 = pass
TYRAX --pad <projectDir> "<script>"  # drive the RUNNING game's pad, no focus
TYRAX --ui-script [projectDir] "<script>"  # drive the EDITOR's own UI, no focus
TYRAX <projectDir|project.tyra>      # open GUI on a project
```

The neural upscaler trains and measures headlessly too
(`docs/neural-upscaler.md`), which is the whole test layer that feature has:

```
TYRAX --blss-train [<projectDir>] [--all-shots] [--threads N] [-o out.net]
TYRAX --blss-eval  [<projectDir>] [-i net] [--cv] [--features] [--dump <dir>]
TYRAX --blss-emit  [-o inc/blss_net.gen.hpp]
TYRAX --blss-coverage <projectDir> [--frames N] [--raster N] [--threads N]
                                   [--out WxH] [--verbose]
```

**`--blss-coverage` is the SPEED half of "should this project have BLSS on"**,
and the headless twin of the window's *Will the frame get faster?* button — same
`blss::measureCoverage`, same verdict arithmetic, in-process, about a second. It
prints per-shot and overall mean/p95 coverages with the **geometry/emitter
split**, the derived verdict, and machine-readable `[blss] coverage …` lines.
Use the project's own raster (the default) or the verb and the button answer
slightly different questions. It also reads the project's **reconstruction mode**
(`blssNetwork`) and prices the verdict for it — plain mode's EE bill is a
seventh of the neural one, so the same coverage count lands against a **2.6**
break-even instead of a 13.1 one — and prints the OTHER mode's line beside it,
because the two verdicts routinely disagree and the switch is one setting away. It exists because the round that *measured* the
speed model could not re-derive the estimator's own figure — it was a button in
a GUI — and **a number nobody can re-run is a number nobody can check.** Its
first run disagreed with the hardware anchor (72.63 against 58.7 blended-pass
equivalents on `examples/upscaler-lab`), and that gap has since been **located**
rather than closed: it is a constant scale error in the emitter term, not the
unit and not the camera. Walked under the fixture's own parked gameplay camera -
authored as a training vantage, which is what makes the check possible - it reads
**78.99**, i.e. HIGHER than the six-move mean, and the counted-to-measured ratio
holds at 1.35 / 1.26 / 1.27 / 1.36 with the haze stepped 6 / 4 / 2 / 0 banks. So
read its output as an overdraw **index** that over-states its scale by about a
third, not as milliseconds: docs/neural-upscaler.md, "The overdraw count is an
INDEX", and docs/profiling.md, "Calibrating the speed model against hardware".

**Eight flags measure a configuration no project can currently ask for, and each
prints a line saying so** — `--tile N`, `--scale WxH` (the raster scale; the
ENGINE is generic, it is `blssScale` that can only name 2x2 and 1x2),
`--act-table N`, `--no-anim`, `--still` (freeze each shot at one camera and one
pose so only the jitter phase advances — the period-2 metric's fixture, refused
by `--blss-train` and by `--cv`), `--proxy-budget` (the fifth twin-contract
rule, off on both sides), **`--emitter-proxy`** (the SIXTH twin-contract rule —
give each enabled particle emitter a bag proxy; also off on both sides, the
engine's half being `TYRA_BLSS_EMITTER_PROXY`. Mind what it does and does not
change: it makes the six channels DESCRIBE the particles, and the corpus
renderer still DRAWS none, so a PSNR from an `--emitter-proxy` run prices the
description against a particle-free truth. Read `--features` and a console
`BLSSFEAT` line through `--probe`, not the dB) and `--ignore-shot-plan` (do not
read the project's
training-shot plan — six automatic moves, takes on, an equal frame share, which
is how a table taken before the plan existed stays runnable). A table of decibels
whose configuration is not written down is a table nobody can reproduce, which is
how this feature published five wrong numbers — and a **sixth**: the row that set
the "fit the project you ship" rule had its ceiling measured at jitter ON and its
margins at jitter OFF. The sampler is announced in both directions now.

**Several positionals is a UNION corpus, and `bestiary` is a member.**
`--blss-eval <a> <b> bestiary --cv --cv-groups` is **leave-one-PROJECT-out**: the
held-out shot's whole project is removed from the training set, which is the only
form of "can I ship one net" that means anything. Plain `--cv` holds out one shot
and trains on eleven other moves of the same scene. See docs/neural-upscaler.md,
"Can one net ship for every project?".

**Verify a change to any parallel phase with `--threads`, not by reading the
code.** `--threads N` (0 = every core, clamped to 32) bounds the corpus render,
the oracle and `--blss-eval`'s per-method loop, and it is a wall-clock knob and
nothing else: the same `--seed` must write a byte-identical `blss.net` **and a
character-identical eval table** at any thread count. So the check is two
runs and `md5sum` —

```bash
build/tyrax-editor --blss-train examples/procedural --frames 156 --epochs 400 \
    --all-shots --threads 1 -o /tmp/t1.net
build/tyrax-editor --blss-train examples/procedural --frames 156 --epochs 400 \
    --all-shots            -o /tmp/auto.net
md5sum /tmp/t1.net /tmp/auto.net    # must be identical
```

— and the stronger form, which is what actually protects the published numbers,
is to build the commit *before* the change into its own worktree and check that
its `blss.net` matches too. Every measured table in `docs/neural-upscaler.md`
came off a seeded run, so a thread-dependent result would silently unmake all of
them. On 6 cores that run is ~68 s at `--threads 1` and ~18 s at auto; use
`--frames 78 --epochs 200` for a faster smoke check (still deterministic, just a
different net). **All three verbs end with a `blss: timing` phase line** —
`corpus X, oracle Y, fit Z` for `--blss-train`, `corpus X, eval Y` for
`--blss-eval`, `corpus X, oracle Y, folds Z` for `--cv` — and the per-shot corpus
lines report **cpu ms, not wall ms**: summing them will not give you the wall
clock of a threaded run.

`--blss-eval <projectDir>` is also the "should this project have the feature on
at all" check: the **oracle** row is the scene's ceiling, and on some scenes it
is +0.00 dB. **It needs no trained net for that** — with no `-i` and no
`blss.net` to find it runs net-free, drops the `BLSS (trained)` row and still
prints the verdict, exit 0. (So `-i` IS required when you are checking twin
parity, which is a claim about that row.) Two lines are printed for a caller
rather than a reader: `[blss] verdict headroom=… passes=… bilinear=… oracle=…
native=…` from every `--blss-eval`, and `[blss] fold k of n` from `--cv` as each
fold lands. Never quote a plain `--blss-eval`'s held-out column — use `--cv`.

VU1 microprogram work has its own layer, faster than everything below
(`docs/vu-framework.md`):

```
TYRAX --vu-check [engineDir]         # exit 0 = every program parsed AND every
                                     #   generated one matches its handwritten
                                     #   twin bit for bit, in the host simulator
TYRAX --vu-list <file.vclpp>         # expand + disassemble one microprogram
TYRAX --vu-emit <outDir>             # generate .vclpp + the EE program classes
TYRAX --vu-replay <projectDir>       # re-run a console VU1 capture on the host
```

`--vu-replay` is the only layer that checks the simulator against REAL hardware:
it reconstructs the input from `bin/vucap.bin` and diffs its own output against
the console's. `examples/vu-lab` is the fixture built for it. Two things to know
before trying it on some other project: only the LAST mesh of a chain can be
replayed (use the Debugger's flush picker to capture a flush carrying ONE mesh -
a terrain flush with fourteen chunks is not resolvable), and **a project with no
flow-graph node has no devkit layer at all** - `live_debug.gen.cpp` compiles to an
empty TU and the capture button does nothing, which looks like a broken feature
and is the zero-cost rule working.

**Run `--vu-check` after ANY change under `vendor/tyra/.../*.vclpp` or to
`src/vugen.cpp`.** It is the closest thing this repo has to a unit test: it
parses all 25 shipped microprograms, executes the described ones against their
handwritten originals on randomized input, and diffs every quadword of the GIF
packet the GS would receive. Milliseconds, no Docker, no PCSX2. It does not
replace the e2e pass - it models no cycle timing and no MAC/STATUS flags, and no
generated microcode has been built for hardware yet - but a program that fails
here will not work on the console either.

**AND A GREEN `--vu-check` ONLY MEANS SOMETHING IF YOU CAN MAKE IT RED.** It
stages VU1 memory itself, so a term whose staged inputs make it evaluate to zero
is compared by comparing nothing - which is exactly what happened to the spot
light for its whole life (every `w` lane of its three quadwords was left at 0,
and all three of `invRange2`/`cosCut2`/`invSoft` are `w` lanes). The check
passed against a program that ran 21 operations a vertex and would have passed
against one that omitted them. So when you add, remove or GATE a term in a
microprogram, falsify the check first: invert the branch or zero the term in the
HANDWRITTEN program - which needs no editor rebuild, that side is read off disk -
and confirm it reports `DIFFERENT`. See docs/vu-framework.md, "The check that
makes this more than a plausible story".

**Rebuild before believing a `--vu-check` failure, and never attribute one by
swapping the engine alone.** Both sides of every comparison must come from ONE
commit: the generated side is compiled into the binary, the handwritten side is
read off disk. So `git archive <rev> vendor/tyra/engine` + `--vu-check <thatDir>`
- the attribution trick that works for engine-only symptoms - swaps exactly one
half here and MANUFACTURES failures against a stale exe (measured: 7 DIFFERENT
programs plus the matcap identity-at-zero, all of which pass when each half runs
against its own peer). The check now says so itself - `note: FOREIGN engine` /
`note: ... is NEWER than this executable`, and a paragraph under FAIL - but the
habit to keep is the 2x2: old and new binary against old and new engine. Both
diagonals passing means skew, not a bug. `D:\tyra-editor` is usually parked on
another branch with a days-old `build/`, so check `git log -1` and the exe's
mtime before trusting a run from there.

- `--new` scaffolds a complete game project (all generated sources, Makefile,
  docker-compose.yml) **without Docker** — instant way to get a fixture. `fpp` seeds a
  single Player entity in walk mode and `thirdperson` the same entity in
  third-person mode (both generate the SAME game sources — the mode is a
  per-object property); `empty` is an orbit-camera scene with no objects. The
  preset is the project's permanent `template` field, so it is also the way to
  fixture either game template — the editor deliberately offers no way to
  switch afterwards.
  Defaults match the *New Project* dialog: 100x100 terrain, 1 unit = 1 m, the
  **debug** profile with Live Link on, USB keyboard & mouse off — except the
  preset, which stays `empty` here while the dialog starts on `fpp`. It echoes the
  terrain size and world scale, so `--new` + a grep over the `.tyra` is the
  cheapest check that a new-project default landed (a fixture that needs the
  old release-profile behavior has to set it explicitly).
  **`--no-terrain`** (accepted anywhere among the optional arguments) scaffolds a
  scene with no ground at all (docs/terrain.md) — the fixture for the void-floor
  behavior, and the fast way to check the terrain-less codegen: the `.tyra` gets
  `"enabled": false` and `inc/scene_data.hpp` reads `TERRAIN_ENABLEDS = {false}`
  with `TERRAIN_TEXTURES = {-1}`. Verifying it in PCSX2 needs a floor object in
  the scene, or the player falls at boot — which is the feature, not a bug.
- `--build` streams the whole native (or explicit `--docker` fallback) build
  log to stdout and returns a real exit code — the backbone of scripted e2e runs.
- `--bake-gi` runs the whole global-illumination bake for every scene
  (docs/global-illumination.md) into `.res-baked/gi/` and then refreshes the
  generated files, so the probe table and the lightmap flags follow - **no
  Docker, no GUI**. It is the headless twin of the *Global illumination*
  tab in *Tools > Ambience Editor* and the only practical way to verify GI in a script: bake,
  grep `inc/ao_data.gen.hpp` for `SCENE_AO_ATLAS_GIS`/`SCENE_AO_MAP_GIS` (1 =
  the scene shipped GI), check `inc/probe_data.gen.hpp` has a
  `SCENE_PROBE_GRIDS` entry, then `--build --run` and A/B the screenshot
  against the same project with `"giEnabled": false` in its `.tyra`. Two
  traps worth knowing: the bake is NOT part of a build by default (a build only
  READS the cache), so a change to the scene silently falls it back to the
  pre-GI lighting until you re-bake - `"giAutoBake": true` in the settings
  makes `--build` (and the GUI build) re-bake exactly the STALE scenes first,
  printing `gi: baked GI ...` / `gi: fresh ...` per scene, and the check is a
  second build that says `fresh` for all of them; and it prints per scene how long it took plus
  the atlas/terrain/probe dimensions, which is the fastest sanity check that
  it saw any geometry at all (`atlas 0` means no eligible receivers).
- `--bake-status <dir>` answers "is the cache fresh?" WITHOUT baking or
  writing anything: one line per scene for GI and baked shadows (`fresh` /
  `STALE` with the cache and live signatures / `absent (or an older cache
  version)` / `off`), one per stamped pre-lit object and one per model-AO map,
  then `bake status: fresh|STALE`; exit 0 or 3. Use it after any change to a
  bake signature or cache version, on every example that checks a cache in
  (`git ls-files examples | grep res-baked`). `absent` on a checked-in cache
  means the format version moved without a re-bake. The line-ending recipe
  (1.164.2): copy the example out of the tree, rewrite its `.obj`/`.mtl` to
  CRLF, and it must still read `fresh`.
- `--bake-model-ao` bakes every eligible `.obj` model's OWN ambient occlusion
  (docs/ambient-occlusion.md, "Model AO") into `.res-baked/modelao/` and prints
  one line per (model, texture) pair - `baked` / `fresh` / `skipped ... :
  reason`. It exists because the shipping path for that feature is `texbake`,
  which runs only inside a real Docker build, so **`--bake-model-ao
  <dir> --texbake` is how the whole chain is checkable with no container**:
  the second flag runs the texture bake too, i.e. the actual multiply into
  `.res-baked`, which logs `model AO: multiplied into <texture>`. The honest
  check afterwards is a pixel one - decode `res/models/x.png` and
  `.res-baked/models/x.png` and compare their RGB means (baked must be darker)
  and their alpha channels (must be IDENTICAL - the GS cutout rule). Re-run to
  confirm idempotence (`fresh`, byte-identical map). The verb refuses when the
  project has the feature off, so a fixture needs `"modelAo": true` in its
  `.tyra` settings (new projects get it; older ones do not).
- `--bake-prelit <dir> [scene]` re-bakes every object marked to ship pre-lit
  (`prelitWanted`) whose baked texture no longer matches the scene
  (docs/prelit-models.md, "Managing pre-lit objects"), then saves and
  regenerates. It prints `baked` or `fresh` per object plus a summary and exits
  non-zero on a bake failure. **Two runs are the test**: the second must report
  everything `fresh` and bake nothing, which is the only check that the
  signature is stable rather than merely present. To see a stale one, move the
  object (edit its `objects/<id>.json` position) and run again - it re-bakes,
  and the reported `mean light` moves with it. The object's stamp lands in
  `objects/<id>.json` as `prelitSig` (hex string), `prelitWanted` and - only
  when the object had a material override before its first bake -
  `prelitSource`. It refuses a project needing a format migration, like every
  headless verb that writes the project. With `"prelitAutoBake": true` in the
  project's settings, `--build` (and the GUI's build) runs the SAME loop first
  and prints `pre-lit: ...` lines - the check is a build log that says
  `0 baked, N already fresh` on the second build.
- `--resave` loads a project and writes the `.tyra` (+ heights) straight back
  out — **no Docker**. Because `project::load` runs every format migration,
  this is the clean way to test/round-trip a `.tyra`-format change headlessly:
  strip/alter a field, `--resave`, and inspect the rewritten file. Also the
  one-shot batch-migration tool for existing projects.
- `--refresh-gen` runs `project::refreshGenerated` directly — the clean way to
  check codegen without Docker (supersedes the "run --build and let it fail"
  trick below, which still works). **It does NOT run `texbake`**, so nothing
  under `.res-baked/` is rebuilt: quantized textures, atlas pages, the terrain
  AO map and the scene lightmap atlas all stay as the last `--build` left them.
  Comparing a baked image after a `--refresh-gen` compares a stale file against
  itself and silently "proves" that your change did nothing — use `--build`
  whenever the thing you are measuring is an asset. It also runs the **asset bakes that live
  inside refreshGenerated**: animated models into `res/models/*.tskl` and
  static ones into `res/models/*.tmdl` (docs/model-pipeline.md), each printing
  its problems as `[anim bake]` / `[model bake]` lines on stdout, plus the
  **credits page strips** (docs/credits.md) into `res/credits/<roll>-<k>.png` -
  so a roll's typography and page count are checkable with no Docker and no GUI:
  refresh, stitch the pages back into one image and look at it, and read
  `CREDITS_PAGE_TOTAL` / `contentH` out of `inc/credits_data.gen.hpp`. So a model
  format / LOD change is verifiable headlessly: refresh, then read the file's
  bytes (a few lines of Python on the layout in `src/tmdl.hpp` /
  `glbparser.cpp` tell you the tier vertex counts). Note the texture bake
  (`.res-baked`, which decides what actually ships) runs in `--build`, not
  here. `--dump` / `--dump-graph` / `--apply-graph`
  are machine-readable project I/O (apply validates node types + link pin
  rules and saves) — handy for scripted graph fixtures; `--ai-graph` runs the
  whole AI generation (docs/ai-tools.md). To e2e-test the AI pipeline without
  a real backend, put a stub `claude.cmd` on PATH that swallows stdin
  (`findstr /r ".*" > nul`) and echoes a graph JSON — the Generator, parser,
  append-merge and save all exercise for real (see PROGRESS 65).
- **`--refresh-gen` DOES run the vehicle bake** (`vehbake::bakeProject`, the
  `[vehicle]` lines), unlike texbake: the bake hands measurements BACK to the
  definition - the emissive `lamps` part index and its rear corner count,
  `vehbake::adoptMeasured` - and codegen bakes those into `VEHICLE_DEFS`, so a
  refresh without the bake emits `-1` for a part the next build writes. The
  Runner runs it before `refreshGenerated` for the same reason. The check for
  a lamp-material model is one grep: `grep -o '{[^}]*}, {[^}]*}, [0-9-]*,
  [0-9]*, [0-9-]*, [0-9.]*F}' inc/scene_data.hpp` on the `VEHICLE_DEFS` row
  reads `..., 2, 60, ...` on the CC96 (part 2, 60 rear corners); the lamp
  colours on a GS capture then read EXACTLY the runtime's constants
  ((175,32,24) lit / (78,14,12) off / (255,45,35) braking), which is how the
  chain was verified.
- **The far tier, without eyes** (1.134.0, docs/vehicles.md "An authored far
  model"). The bake logs `far model X N tris (wheels in), S submit(s) past D
  units (T for cars nobody drives)` or `no far tier carries the wheels`, plus a
  line per body part: `part K name L list verts -> S strip (x); tier ...;
  hidden far` - the strip ratio is the packing acceptance number. The game logs
  `VEHLOD car N tier T swap at D` on every swap. To SEE a tier, set
  `farDistance` 3 (the driven car) or `trafficDistance` 3 (every other car) on
  a scratch copy and `--capture-frame`: the car is then on its far tier at
  chase-camera range. Compare against the full car with the shine off
  (`settings.vehicleShineBudget` 1 leaves only the driven car shiny) - a far
  car gives up its shine, so a shiny full car reads brighter for that reason
  alone. The chase camera in vehicle-playground does not orbit under
  `--pad "stick r ..."` at boot; move the parked car into the view in the
  copy's object JSON instead (forward is +z there).
- Both `--build` and `--refresh-gen` also run the **procedural bake** first
  (`procbake::bakeAll` - docs/procedural-generation.md): stale Procedural
  volumes are baked into their chunk meshes and the project is saved, printing
  `procedural: baked N volume(s) -> ...`. So a headless build of a project with
  a procedural volume MUTATES the `.tyra` (the chunk objects are real scene
  objects) - expect that diff, and use it: the fastest way to test a graph
  change is `--refresh-gen` + grep `inc/scene_data.hpp` for the
  `<volume>#<asset>-x<i>z<j>` chunk objects. The graph model itself is
  harness-testable (procgraph/procgen/procbake link without GUI, GL or
  templates.cpp - see PROGRESS 171 for the property list that caught three real
  bugs).
- **Format versioning** (see `docs/format-versioning.md`): `--build`,
  `--resave`, `--refresh-gen`, `--apply-graph` and `--ai-graph` refuse a project
  whose `formatVersion` has pending REGISTERED migration steps (exit 1) —
  `--migrate` is the explicit tool (backs up `.tyra` + `objects/` + heights +
  splat + flow-nodes/screen-effects into `_backup/`, applies the steps, resaves
  the same file set as `--resave`; degrades to a plain resave when current). To
  test a version gate, hand-edit `"formatVersion"` in the manifest: a value
  above `version::kFormatVersion` must be refused by every path, a lower one
  opens silently unless a step is registered in `migrations.cpp`. With no step
  registered the prompt/backup path is unreachable, so exercising it means
  registering a temporary throwaway step. Two things a throwaway step is the only
  way to reach, and both are worth re-checking whenever the persisted file set
  grows: that the `_backup/` copy holds **every** file the post-migration save
  rewrites (drop a sentinel `terrain-<scene>.splat` in first — the save deletes it
  when the scene has no layers, so the backup is the only copy), and that a step
  returning `false` leaves every file **md5-identical**. `migrations::validate()`
  catches a mis-registered step (out of order, duplicate, out of range): `all()`
  prints `BROKEN MIGRATION REGISTRY` on stderr at the first command that consults
  it, and `run` aborts with disk untouched. **A step whose `kFormatVersion` bump
  you forgot produces NO steps to run** — that stderr line is the only thing
  between you and an afternoon of "my migration does nothing", so read it.
- Create scratch projects in a **short** path outside the repo — the
  convention is `%TEMP%\tyra-editor-test\<name>`. Do NOT use the session
  scratchpad for anything that will boot in PCSX2: its path is ~180+ chars
  and PS2 `loadelf` truncates long `host:` ELF paths (emulog shows a mangled
  `secname ...`), crashing to a null PC before the Tyra banner — which looks
  exactly like an engine bug and is not one.

## Layer 2 — codegen checks without Docker

Most features live or die in the generated code, and you can inspect it
without building:

- `--new` writes every generated file; grep them for your new constants/logic.
- For an **existing** project, use `--refresh-gen <projectDir>` directly. It
  refreshes `inc/scene_data.hpp`, `src/gen/flow_graph.gen.cpp`, etc. without
  provisioning the native toolchain or contacting Docker.
- When inspecting, remember the ownership split (see tyra-editor-dev): `.gen.*`
  files and `scene_data.hpp` are always rewritten — trust them after a refresh;
  `terrain_game.cpp` / `controls.hpp` / `script.hpp` regenerate only while their
  marker line is intact, so a stale-looking file there may be user-owned by
  design, not a codegen bug.
- To test a specific graph/scene shape, edit the project's `<name>.tyra` file
  directly (it is the source of truth; the editor tolerates external edits and
  discards the stale undo history in `<name>.history`), then refresh and inspect.
- `examples/script-demo/` is a checked-in generated project — a useful diff
  baseline, but only as fresh as its last regeneration. If codegen changed
  since, regenerate the sample first (its files drift silently); don't treat a
  stale copy as ground truth.

Past features were verified with throwaway "codegen harnesses" — a scratch
`main()` that builds a `Project` in code, calls `templates::generate()` and
asserts on the emitted strings. This works because `project.cpp`,
`templates.cpp` and `json.cpp` have no ImGui/GLFW dependency — they link into a
tiny host harness without the GUI. Fine pattern; keep such harnesses in the
scratchpad, not the repo.

**Menu stylesheets have two cheap gates, and both found real bugs** (see
docs/menu-styles.md):

- **A harness over `menustyle.cpp` + `menulayout.cpp` alone** - neither needs GL,
  ImGui, `project.cpp` or a font, so it links in seconds:

  ```bash
  g++ -std=gnu++20 -I src -I vendor/stb -o h harness.cpp \
      src/menustyle.cpp src/menulayout.cpp && ./h
  ```

  Assert the cascade (element < class < state, menu scope wins), that
  `write(parse(t))` is STABLE (the Style tab saves through it, so an unstable
  writer rewrites people's files behind their backs), that a broken declaration
  does not eat the good one next to it, and the layout numbers. It caught a
  `content: "{{cross}} OK"` truncated at the first `}` (which also swallowed the
  next declaration) and a `row:disabled` cell baked for every row instead of only
  the gateable ones - 71% of the texture heap for one 6-row menu instead of 13%.
- **The pixel-identity gate for the Classic look.** A menu with no stylesheet
  must bake byte-identically to the pre-stylesheet baker. Build the previous
  editor in a worktree, `--refresh-gen` the same example copies with both, and
  diff `res/menus/*.png`:

  ```bash
  git worktree add /tmp/base HEAD && (cd /tmp/base && ./build.sh --dev)
  /tmp/base/build-dev/tyrax-editor --refresh-gen A/showcase
  ./build-dev/tyrax-editor        --refresh-gen B/showcase
  # compare A/*/res/menus/*.png with B/*/res/menus/*.png (PIL, 3 lines)
  ```

  It found a double-composited background (every translucent panel came out
  (7,10,21) instead of (8,12,24)) and a save menu that lost its "X SAVE O LOAD"
  hint line - neither visible without a diff.
- **Checking that something SCROLLS: never use a pattern aligned with the
  scroll.** A menu's animated background was measured as frozen through three
  rebuilds - correlating two frames found a shift of exactly 0 - because the test
  tile was diagonal stripes and the scroll vector ran along them. Vertical bars
  plus a pure horizontal scroll showed 53 px in one second, matching the declared
  speed. The emulator has a second trap in the same area: **PCSX2 unfocused does
  not take injected keys**, so click into its window before driving it, and read
  its status bar (FPS/VPS/Speed) to know the machine is actually running before
  concluding anything about motion.

**`App`'s own private methods are reachable from such a harness too**, which is
the difference between testing a copy of the logic and testing the shipped
function. Link every `build/CMakeFiles/tyrax-editor.dir/**/*.o` except
`main.cpp.o` (plus `libimgui.a`, glfw and `-ldl -lrt -lm -lGLX -lOpenGL
-lpthread`, `-no-pie`), and reach the members with `#define private public`
before `#include "app.hpp"`. Two rules make it work: include **every header
app.hpp includes first, normally**, so the macro only ever reaches app.hpp's own
body (libstdc++'s `<sstream>` fails to compile otherwise — *redeclared with
different access*), and neutralise whatever pulls in GL. For the Material
Editor that is `matEdPaintW_ = 0`, which makes `matEdRegenLayer` /
`matEdComposite` / `matEdSavePaintTarget` early-return, so a real
`matEdSavePreset` → file → `matEdApplyPreset` round trip runs **with no GL
context at all** (PROGRESS 223). What it cannot cover is the panel around the
call — say so rather than implying a click-through happened.

**Collaboration sessions are headless-testable the same way**: `session.cpp` +
`wire.cpp` have no GUI dependency, so a harness can run a host `Session` and a
client `Session` **in one process over 127.0.0.1 with real sockets** (drain
`drainEvents()` in a sleep loop) to test join/transfer/cache/kick/refresh, and
drive `session::diffModel`/`applyEdit` directly on two `Project` replicas for
convergence property tests (random concurrent edits + the host relay rule →
assert byte-identical serializations; that test caught two real divergence
bugs). Link the same .obj set as the model harness plus `session`, `wire`, `platform`,
and `-lws2_32` on Windows (`-pthread` elsewhere; `wire.cpp` needs no extra
library on Linux). For the interactive layer, run **two editor instances on one
machine** (the second without a project), host from A, join from B at
`127.0.0.1` — loopback is not blocked by Windows Firewall even when the LAN
prompt was declined.

## Layer 3 — full e2e: native build + PCSX2 boot

### Keep automated emulator runs off the user's desktop

Default to an invisible emulator session for automated checks. Open a visible
window only when the user explicitly requests one or needs to assess the game
visually. Agent-side screenshots and input checks do not require a visible window.

Verified on this Windows host: run Linux PCSX2 inside WSL on a private Xvfb
display, not WSLg. Start Xvfb on an unused display (the reflection trial used
`Xvfb :97 -screen 0 1280x800x24`), set `DISPLAY=:97` and
`QT_QPA_PLATFORM=xcb`, and unset `WAYLAND_DISPLAY` for PCSX2. Use a private
`-datapath` profile and unique PINESlot, then `-batch -nogui -elf <absolute ELF>`
with the ELF's bin directory as cwd. `-nogui` alone does not hide the game window:
the private virtual display does. The trial also used
`LIBGL_ALWAYS_SOFTWARE=1`; emulator speed is not physical PS2 performance.

See `tools/tyrax2-motion-run.py`'s emulator mode for the verified launch and
`docs/tyrax2-full-throttle-2026-10-07/reflection-freeze/emulator/launch.json`
for its identity. Its display/profile paths are fixture-specific, not universal
defaults: check availability before reuse. Record exact emulator/Xvfb PIDs,
capture from that display or the game, and stop only owned processes after the
test. Do not touch the user's emulator/profile. On Windows, start background
WSL/helper processes with `Start-Process -WindowStyle Hidden`. If an equivalent
invisible setup is unavailable, report that before opening a distracting window.

For editor Play, select a scene other than the saved startup scene and launch
from the toolbar or F5. Check the Runner log's `Play scene:` line and the
game's loaded scene, then check that `bin/launch.scene` was consumed. Build-only
and CLI launches still use the saved startup scene; the marker must never enter
an exported ISO.

Prerequisites: WSL on Windows or the packages named by
`tools/toolchain/setup.sh` on Linux, and PCSX2 with a BIOS configured — auto-detected in
`Program Files\PCSX2`, or on Linux from PATH / flatpak / an AppImage under
`~/Applications` or `~/Downloads`. Anything else: set the path in
*Edit > Preferences*.

**A relative project path is fine now, and was not before.** `--build
examples/<name> --run` used to hand the native build script and PCSX2 a
RELATIVE path: the script's `cd` failed outright, and PCSX2 derived the `host:`
asset root from the relative ELF, so the game booted to an untextured scene
with no baked shadows and nothing in any log saying why. Both are absolutised
at the source now (`Project::elfPath`, and the Runner's native-build
arguments); the ~145-character ELF limit is therefore measured on the path
PCSX2 really sees.

Host preparation is deliberately explicit. Check it with
`bash tools/toolchain/prepare-host.sh`; install missing Debian/Ubuntu packages
with `--install`. **On Linux `./setup.sh --deps` already covers this** - its
four distro lists carry `curl`, `tar` and `rsync` alongside the editor's own
dependencies, so a box set up for the editor can build games too; the split
used to bite exactly once, at the first Build & Run. And a manual
`tools/toolchain/setup.sh` now installs into the SAME directory the editor
uses (`configDir()/toolchain/ps2dev`), so it is a pre-install rather than a
second 943 MB copy. On Windows, `prepare-host.ps1 -Install` reaches the same script
inside the default WSL distribution. A normal build never runs `apt`; the
Windows installer offers this as an unchecked, non-inherited task.

```
TYRAX --build <projectDir> --run
```

What happens (see `src/runner.cpp`): generated files refresh → verified PS2DEV
and the vendored VU tools are provisioned if needed → engine sources checksum-
synced into the native cache, `libtyra` rebuilt if changed
(VU1 microprograms only when a VU source changed) → project rsynced → `make -j`
→ WAV sfx converted with `adpenc` → `bin/` synced back → existing PCSX2
processes killed → `HostFs = true` forced in PCSX2.ini → PCSX2 launched on the
ELF.

For every `*-loop.wav`, verify byte 6 of the generated `.adpcm` is `1` (for
example with `od -An -tu1 -j6 -N1`). This is part of native/Docker parity: a
newer output with byte `0` must be re-encoded with `adpenc -L`, not skipped.

Notes:
- First-ever build downloads PS2DEV v2.0.0, tests OpenVCL and compiles the
  engine (minutes). Subsequent builds take seconds unless the
  engine changed.
- **Testing a change to the toolchain image itself**: build it with
  `docker\build.ps1` / `docker/build.sh` (add `-FromSource` / `--from-source`
  for `docker/Dockerfile.fromsource`, the one CI publishes; without it you get
  `docker/Dockerfile`, the inherited A/B reference, which **CI does not build at
  all** - so local is the only check it gets). Both scripts run the same checks
  CI does. Then point a scratch project at it with one line,
  `TYRAX_IMAGE=tyrax-toolchain:local` in the project's `.env`, and take it all
  the way to a PCSX2 boot. `docker compose config` in the project directory
  prints which image will actually be used; `docker ps -a --filter
  name=<project>` confirms which one the container was created from. Nothing in
  the editor needs rebuilding for this - `docker-compose.yml` is regenerated per
  build and reads that variable. See `docs/toolchain-image.md`.
- **A VU1 packet capture can be armed WITHOUT the GUI**, which is what makes a
  microcode A/B into numbers instead of screenshots:
  `python .agents/skills/tyra-testing/scripts/arm-vucap.py <projectDir> <flushIndex>`
  writes the same `bin/livedbg.cmd` the *Debugger > VU > Capture VU1 packet* button
  **`--full` and `--peek` are what make it a comparison tool.** The plain decode
  prints the first four staged packets and four vertices of each, which is right for
  reading and useless for diffing two builds - the packet that differs is rarely the
  first. `--dump-vucap <dir> --full` prints all of them; `--peek <qw>[,n]` prints raw
  data-memory quadwords as floats and words. That second one is how a microprogram
  reports its own intermediates: **quadwords 1016..1023 are free** in the static
  pipeline's map, and code ABOVE `begin:` runs once per activation while `begin:`
  loops per batch - so a slot pointer set up there turns those eight quadwords into a
  ring with one slot per batch. Without that, every peek reports the LAST batch,
  which is exactly the one where two builds usually agree.
  does, the game answers with `bin/vucap.bin`, and
  `tyrax-editor --dump-vucap <projectDir>` decodes it — chain, VU1 memory, and **the
  GIF packets the program staged for XGKICK**. Name the flush index: "the next packet"
  is a different draw every time, a named index is the same draw forever.
  **Two gotchas.** The responder only exists when the project has instrumentable
  flow-graph nodes (`liveDebugOn = liveDebugEnabled && !syms.nodes.empty()`), so a
  bare scratch project answers nothing — give it one node
  (`--apply-graph <dir> <object> graph.json` with an `OnStart` → `Log` pair is enough)
  and rebuild. And the decode prints only the first few staged packets, so a
  difference deeper in the list shows up in the header counts, not the listing.
- **An image swap used to rebuild NOTHING, which made it the easiest A/B to get
  wrong.** The incremental logic keys off source timestamps, and an image swap
  touches no source, so the previous image's objects were relinked and the new
  toolchain appeared to change nothing. This was not theoretical: three
  consecutive `VCL_FLAGS` probes each booted the *previous* probe's VU microcode
  and produced three identical screenshots, one of which was then chased as a
  rendering bug. Since 2026-08-05 the Runner stamps the VU assembler itself
  (`/tyra/.vcl-stamp`) and prints `VU assembler changed - rebuilding the
  microprograms`. The stamp hashes the resolved `vcl`, `vclpp` AND the `openvcl`
  binary behind the wrapper - hashing only the wrapper missed a rebuilt openvcl whose
  flags had not changed, and the previous binary's microcode was relinked while the
  measurements said the new one should fit. Look for that line after a swap, and in
  general **verify from the log that the work happened** — `grep -cE '(^| )vcl '` (one line per
  microprogram, 25 of them) and `grep -c 'elf-g++ .* -c -o'` — before you believe
  any picture. Note that a full microcode rebuild is ~2 min under Sony's `vcl`
  but **seconds** under openvcl, so a fast build is not by itself evidence that
  nothing was rebuilt.
- **The whole pipeline is incremental, so measure a build by what it
  RECOMPILED, not by the clock.** `grep -c 'elf-g++ .* -c -o'` over the build
  log is the number that means something: on `examples/showcase` (18 TUs, 6
  cores) a build with nothing changed is **0 compiles / ~5 s**, one edited game
  source is **1 / ~9 s**, a moved scene object is **13 / ~47 s** (the scene
  table is in a header most TUs include), an engine `.cpp` is **~7 s**, an
  engine `.vclpp` is **~2 min** (the microprograms), and `--rebuild` is
  **~5 min** from nothing. A "no changes" build that still compiles things is a
  regression with a specific cause — some step wrote a file the compiler reads
  (see the `runner.cpp` row in tyra-editor-dev); find it by comparing mtimes in
  the container against `/src/obj/*.d`.
- **`--rebuild` is the escape hatch** and the control when you suspect the
  incremental logic itself: it recreates the container and rebuilds the engine
  and the game from source. `Clean` is the other hammer — it also wipes the
  host's `bin/`.
- PCSX2.ini is found portable-first (an `inis/` next to the executable), then
  per OS: the Documents known folder on Windows — **Documents may be
  OneDrive-redirected** (e.g. `...\OneDrive - <org>\Dokumenty\PCSX2\`), not
  `%USERPROFILE%\Documents` — or `$XDG_CONFIG_HOME/PCSX2/inis` and the flatpak
  sandbox (`~/.var/app/net.pcsx2.PCSX2/config/PCSX2/inis`) on Linux. Logs and
  screenshots live next to it (`logs/emulog.txt`, `snaps/`).
- **Normalize Windows PCSX2 CLI paths with `[IO.Path]::GetFullPath()`.** In
  2.9.84, a forward-slash `C:/...` supplied to `-elf` reported that an existing
  ELF did not exist; the same file booted with `C:\...`. Normalize `-logfile`
  too. This does not apply to the game's PS2 `host:/` paths. For an isolated
  `-datapath <parent>` on this Windows build, settings were created under
  `<parent>/PCSX2/inis`, not `<parent>/inis`; inspect the actual created path
  before diagnosing a first-run wizard. Keep isolated settings separate from
  the user's configuration.
- Missing `HostFs` = "Failed to load ...png" assert on the first fopen. The
  editor enforces it, but PCSX2 rewrites its ini on exit — if a game asserts on
  asset loading, check HostFs first. (The launcher also configures PCSX2's
  emulated USB ports — USB1 = HID keyboard, USB2 = HID mouse — whenever the
  project's *Keyboard & mouse controls* preference is on; `bin/log.txt` prints
  `KbdMouse: keyboard driver ready` / `mouse driver ready` when the game saw
  the devices. See `docs/keyboard-mouse.md`.)
- **Driving the game: use the Remote Pad, not the emulator's keyboard.**
  `tyrax-editor --pad <projectDir> "<script>"` writes the pad state the running
  game polls out of `bin/livepad.bin` (docs/remote-pad.md), so **no window needs
  the focus on either OS** and the whole class of problems below stops applying.
  It is the honest way to test anything pad-driven unattended:

  ```powershell
  build\tyrax-editor.exe --pad %TEMP%\tyra-editor-test\padtest `
      "stick r 110 0; wait 1.5; stick r 0 0; stick l 0 -127; wait 2.5; neutral"
  ```

  `press cross [s]` / `hold up` / `release all` / `stick l|r <x> <y>` /
  `wait <s>` / `neutral` / `pad 1|2`, separated by `;`. Needs a **debug** build
  with the *Remote Pad* preference on (default) - the driver warns on stderr
  when the project was built without the channel, which is the only way "nothing
  happened" can mean "the game cannot hear you". Four things worth knowing:
  a `hold` with no `wait` after it does nothing visible (the driver detaches on
  exit and the game lets go - on purpose, so a killed script cannot leave a
  direction held); the game reads **only the analog sticks**, so a held D-pad
  `Up` changes nothing (that is the game, and it looks exactly like a broken
  tool); the pad answers every 4th frame over ps2link instead of every frame;
  and the state is dropped after ~2.4 s without a refresh, so a long hold needs
  the driver to stay alive rather than one write. To hold something while
  another tool works, run `--pad` in the background with a long enough `wait`.
  Measured on the fpp fixture: 3 s idle changes 620 px (the PCSX2 status bar
  only), a 1.5 s right-stick turn changes ~197k px, a 2.5 s forward walk ~1.4M -
  and 4 s after the script the frame is idle again, which is what proves the
  release actually happened.
  When a script runs clean and the game ignores it, check in this order: is
  `src/gen/live_pad.gen.cpp` the real runtime or the "nothing to compile here"
  stub (that answers "was this ELF built with the channel" in one line), was the
  game rebuilt since the preference changed (the poller is compiled in - a save
  is not enough), and is `bin/livepad.bin` being written where the RUNNING ELF
  lives (`Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'"` prints the
  `-elf` path; a second fixture directory is the classic mix-up).
  **Two Windows-only ways this looks broken when it is not** (both cost an
  afternoon on 2026-08-03, both diagnosed):
  - **`Start-Process -ArgumentList` does not quote its array elements**, so
    `-ArgumentList "--pad",$P,"wait 1; stick r 110 0; neutral"` reaches the
    editor as loose argv words, the script it parses is the single word `wait`,
    and it dies with `error: line 1: wait needs seconds` — on the **stderr of a
    hidden process**, where nobody ever sees it. The game then does exactly
    nothing, which reads as a dead pad channel. Put the whole `--pad` call in a
    `.ps1` with the script as a LITERAL and `Start-Process powershell -File`
    that (a `param()` does not help: the mangling happens at the outer boundary
    too), or add the quotes to the array elements yourself. Same trap for any
    `;`-separated script argument, `--ui-script` included.
  - **The atomic replace of `livepad.bin` races the reader** (fixed since; the
    symptom is worth recognising because a stale editor binary still has it).
    `livepad::write` renames a `.tmp` over the target, and the running game
    re-opens that same file about every frame through PCSX2's HostFs — Windows
    refuses to rename over or delete a file another process has open, so a
    replace comes back `Access is denied` roughly **once per 200 writes** at the
    driver's 40 Hz refresh (zero denials with the emulator stopped; Linux is
    immune, `rename(2)` over an open file is legal there). The driver used to
    abort on the FIRST failed write, so a long hold died with nothing but
    `error: cannot replace ...livepad.bin: Permission denied` and exit 1 — one 9 s
    script in five, measured, and every single one when the file was contended
    harder. `write` now retries (5 tries, 4 ms apart — absorbs a 60 ms exclusive
    hold) and only a **step** write is fatal: a lost *refresh* is a warning, and
    the run says so in its summary (`pad released (27 refresh write(s) lost to
    the reader)`). So on Windows: **read the driver's stderr and its exit code**,
    and treat "no motion, exit 1" as this rather than as a dead channel.
- **Synthetic input into PCSX2** (the older, focus-dependent path - still the
  only way to reach PCSX2's OWN keys, e.g. F8 or the pause hotkey). **On Linux
  this is the easy side**: `wayland-control.py` (see Screenshots below) injects
  through the compositor, so PCSX2 cannot tell the events from a real keyboard —
  click the render area once to focus it, then send pad keys, holding them with
  `keydown`/`keyup` where a direction has to be held. Per `[Pad1]` in PCSX2.ini
  the ones that matter are **W/A/S/D = left stick**, **T/G/F/H = right stick**,
  **K = Cross**, `Return` = Start, arrows = D-pad. Note the generated game reads
  **only the analog sticks** (`getLeftJoyPad`/`getRightJoyPad` in
  `updatePlayer`), so a held D-pad `Up` changes nothing — that is the game, not
  the injection, and it looks identical to a broken tool. Mouse buttons work
  too, and `movrel` covers the captured-cursor case. **On Windows** it is
  the mess below: plain
  `SetForegroundWindow` from a background shell silently fails — use the
  ALT-tap + `AttachThreadInput` trick and VERIFY `GetForegroundWindow`
  afterwards, or inputs go nowhere. `PostMessage` WM_KEYDOWN to the main
  window works for keys; mouse MOTION only registers via real cursor moves
  (`SetCursorPos` walks — PCSX2 recenters the captured cursor every frame, so
  park-and-hold offsets read as constant velocity), and mouse BUTTONS were
  never seen from synthetic events at all — test buttons by hand. Posting
  synthetic WM_RBUTTONDOWN to the render child can wedge PCSX2's mouse input
  until relaunch.
- For ISO/cdrom0: testing use `Project > Export PS2 ISO`, then boot the ISO in
  PCSX2 (covers the path-conversion code that host: boots skip).
- **Keep the fixture project's path short.** PCSX2's host: loader gives up on
  an ELF path over ~145 characters: emulog stops after `ELF Loading: ...`, the
  EE never reaches `is executing`, `bin/log.txt` is never written, and the
  window is black with no diagnostic. The editor warns, but a scratchpad path
  under `/tmp/claude-*/...` blows past the limit on its own - put e2e fixtures
  in `~/tyra-projects/<name>` (or the Windows equivalent) instead. If a boot
  produces nothing but `TLB Miss` spam, measure the path before debugging the
  game.
- **A relative `-elf` path is a different black-screen failure.** PCSX2 may
  rebase it below the ELF directory, so `examples/foo/bin/foo.elf` becomes
  `examples/foo/bin/examples/foo/bin/foo.elf`; emulog reports `Denying access`
  or `Failed to read ELF`, the entry point is `0xFFFFFFFF`, and `bin/log.txt`
  never appears. The editor launcher resolves an absolute native path before
  launch; do the same when invoking PCSX2 by hand. Reproduce launcher regressions
  with `--build ./examples/<name> --run`, deliberately keeping the CLI project
  path relative, then inspect the running process's `-elf` argument.
- **Keep the project path relative in one native-backend regression run.** The
  Runner must resolve it before invoking `native-build`: that helper changes
  into the project, so forwarding `./examples/foo` verbatim turns it into
  `examples/foo/examples/foo` and fails before make. `--build ./examples/foo`
  exercises this seam; an absolute-only test does not.
- **Docker on Linux runs the container as root**, so a `docker` group that was
  granted in the current login session is not yet active in an already-running
  shell. Either start a fresh session or accept that `docker` needs privilege
  there; the build itself needs no root once the socket is reachable.

### Reading the results

- **emulog.txt**: success looks like `ELF <path> is executing`; failure signals
  are `Assertion` lines or an early exit. **`TYRA_LOG`/EE printf does NOT land
  in emulog** even with EnableEEConsole=true. The reliable failure signal is the
  game's own **`bin/log.txt`** (host: fs) — TYRA_LOG output plus any assertion
  dump, bracketed by `======= TYRA =======` … `================`. A failed
  assert no longer paints the screen (the engine halts quietly, leaving the last
  frame up — see tyra-engine-dev), so grep `bin/log.txt` for the banner rather
  than screenshotting for assert text; the running editor also pops that dump in
  a copyable dialog. (A screenshot still shows *where* the game froze.)
  **On a ps2link deploy there is no `bin/log.txt` at all** — the generated
  `main.cpp` logs to the EE console there instead (a host: write per line is a
  network round trip), and ps2link forwards it to `ps2client`. So the game's log
  is the `[ps2] …` lines of the runner output: the Output panel in the GUI, plain
  stdout from `--build <dir> --run-ps2 <ip>`, and the Debug window falls back to
  the same stream. A game built before 2026-08-06 logs NOTHING over ps2link (the
  EE's stdout was buffered and never flushed) — rebuild before believing silence.
- **The game can photograph ITSELF, and that is the path that never lies about
  which window it grabbed** (docs/devkit.md, "The game's own screenshot"). A
  debug build with *Live Debugger* on takes a one-shot on the same command
  channel as the VU1 capture — **flags bit 6** of `bin/livedbg.cmd`, or
  *Debugger > Screen > Capture frame* — reads its last finished frame out of GS
  VRAM and writes `bin/frame.tga`; the editor decodes that and keeps the picture
  as `screenshots/frame-<date>-<time>.png` in the project, which is what *Show
  file* opens. No desktop, no window, no focus, and it is the ONLY one that
  exists on real hardware. Reach for it whenever a host-side grab is in doubt:
  an occluded window, a locked or disconnected session, a parallel worktree's
  emulator, or a console. Headless: `TYRAX --capture-frame <projectDir> -o
  shot.png` writes the command, waits for the file and decodes it - the
  console A/B recipe is `--build <dir> --run-ps2 <ip>` (the process that stays
  up IS the host: server), `--pad` to move, `--capture-frame` per vantage.

  **It works on a console since 1.55.1 and did not before**, which is worth
  knowing when reading anything measured with it earlier: ps2sdk's
  `ps2_screenshot_file()` creates its output with `open(O_CREAT|O_WRONLY)`, and
  over ps2link that create arrives at the `host:` server as a **mkdir of the
  target name** - the host gets a DIRECTORY called `frame.tga`, the open fails,
  and the function has no failure path to report it with. The runtime writes the
  file itself now (docs/devkit.md, "Why the file is written by the game rather
  than by libdebug"). Over ps2link one capture freezes the game for about three
  seconds; in PCSX2 it lands between two frames.

  Scripted, through the editor's own encoder (the `livedbg.cpp` probe recipe
  below), it is ~10 lines: `Command c; c.seq = n; c.captureFrame = true;
  livedbg::writeCommand(bin + "livedbg.cmd", c);` then wait for `frame.tga` and
  read it. The file is an uncompressed 32-bit TGA, **BGRA, bottom row first**,
  exactly `18 + w*h*4` bytes — PIL opens it directly, and that size is also how
  you tell a finished write from a poll that landed mid-transfer (it is written
  a scan line at a time over `host:`, so mid-write reads are common, not rare —
  and over ps2link the write takes SECONDS, so decide "still writing" from the
  size still GROWING rather than from a number of tries). Its alpha is already
  opaque; a frame buffer's alpha is a working channel rather than coverage, so
  the game writes 255 instead of handing over a half-transparent picture.

  What comes back is the **GS raster**, not the television picture — no aspect
  correction, no letterbox — which is why it is also the cheapest way to read
  the real buffer geometry without touching `ScreenshotSize` in PCSX2.ini. To
  cross-check it against the emulator, crop the PrintWindow grab to its
  non-black columns and compare resized: measured **0.91/255** mean absolute
  difference on an fpp fixture, which is what a correct capture looks like.
- **Screenshots**: PCSX2's F8 via SendKeys is flaky. On Windows use the bundled
  script, which has **two capture back-ends** — and picking the wrong one is how
  a whole run becomes fiction, so read this before the flags:

  ```powershell
  powershell -File .agents/skills/tyra-testing/scripts/screenshot-window.ps1 `
      -ProcessName pcsx2-qt -OutFile <scratchpad>\shot.png              # GDI (default)
  powershell -File .agents/skills/tyra-testing/scripts/screenshot-window.ps1 `
      -ProcessName pcsx2-qt -PrintWindow -Auto -OutFile <scratchpad>\shot.png
  ```

  | | default (GDI `CopyFromScreen`) | `-PrintWindow` |
  |---|---|---|
  | reads | the SCREEN | the window's own content |
  | occluded window | **captures whatever covers it, silently** | works, fully covered |
  | focus | raises the window first (steals it) | raises nothing, moves nothing |
  | fails by | looking perfectly fine | coming back black — and saying so |

  **Use `-PrintWindow` whenever a human is at the machine, whenever anything
  might be in front of the window, and whenever you must not steal focus** —
  which on this repo's constraints is most of the time. Use the GDI default when
  the window is demonstrably clear and you want the exact path every `-Watch`
  number below was measured on. The default is still GDI for one reason: a
  renderer that refuses to redraw on demand prints black, and no flag can make
  that window's pixels appear — whereas GDI's hazard is at least fixable by
  uncovering the window. So the script MEASURES the first `-PrintWindow` grab and
  warns once if it is entirely black, rather than handing back a plausible
  screenshot of a game that looks like it never booted. It never falls back to
  GDI on its own: a silent switch to the unsafe path is the bug this exists to
  prevent.

  Verified on PCSX2 v2.3.205 (software renderer, `examples/upscaler-lab`):
  `-PrintWindow -Auto` captures the render child (958x965, `-Trim` → 958x828)
  during live 50 FPS gameplay **without the window ever coming to the front**,
  drives the whole `-Watch` path unchanged, and on an unoccluded window returns
  the same rect and the same picture as the GDI arm.

  It also works for the editor itself (`-ProcessName tyrax-editor`) — useful for
  verifying viewport rendering without a human.

  **`-ProcessId <pid>` picks WHICH instance.** `-ProcessName` takes the first
  match, and with parallel worktree sessions each running their own PCSX2 that
  silently captures somebody else's game (the script now warns when more than one
  window matches). Select the pid off the `-elf` path:
  `Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" | Where-Object CommandLine -like '*<project>*'`.

  **To WATCH the game over time, use `-Watch DIR`** — the Windows twin of
  `wayland-control.py watch` (below), with the same flag names, the same output
  lines and the same diff metric, so the numbers mean the same thing on both
  OSes. It samples the window on an interval, keeps the full-resolution frames on
  disk as `DIR\frameNN.png` and reports **one downscaled contact sheet** plus a
  changed-pixel table, so a whole drive costs about as much context as a single
  screenshot instead of one read per moment:

  ```powershell
  powershell -File .claude\skills\tyra-testing\scripts\screenshot-window.ps1 `
      -ProcessName pcsx2-qt -Watch <scratchpad>\w -Auto -Trim -Every 0.9 -Count 10 -Tile 224
  ```

  Measured on the fpp fixture (1066x705 window, 831x623 picture): 10 tiles at
  224 px = a 926x576 sheet, **~711 tokens** where the ten full frames would be
  ~6 900. A frame costs ~0.1 s of work on top of the interval, so a 0.9 s
  interval is held to the millisecond. One run put the whole drive in one image:
  four idle tiles at **0.000%**, then 41.9% / 29.1% / 22.5% / 6.9% while a turn
  and a walk were held, then 0.000% again after the release — the same
  idle/drive/release shape as the pixel-counting recipe below, in a single read.

  What differs from the Wayland version, and the traps:
  - **`-Auto` is deterministic here.** Windows HAS per-window capture, so it
    takes the biggest visible CHILD window (PCSX2's render surface is a native
    child HWND) instead of hunting for motion — it works on a paused emulator, a
    parked camera or a static menu, every one of which defeats the Linux
    heuristic. Add **`-Trim`** and the black pillar bars go with it: a 1050x623
    widget came out **831x623**, exactly 4:3, the PS2 frame and nothing else.
    That crop is also what makes an idle frame measure **0 px** instead of the
    status-bar noise the whole window carries (87 px in one second here) — a
    clean instrument, so any non-zero row means the game.
  - **A GDI grab reads the SCREEN, so an occluded window captures whatever is on
    top of it** — silently, and for every frame of a long run. This has now bitten
    twice. First during development: a plain `SetForegroundWindow` from a
    background shell does nothing at all (the same trap as synthetic input
    above), and the "PCSX2 screenshot" came back as another application's window.
    The script answered that by raising the window with the ALT-tap +
    `AttachThreadInput` trick, VERIFYING `GetForegroundWindow` and **warning**
    when it still failed. Then it bit again in the field, the expensive way: with
    the owner sitting at the machine an agent captured **their browser instead of
    the emulator** and did not notice, because a wrong-window capture is a
    perfectly good-looking PNG. **That is what `-PrintWindow` is for** (see the
    table above) — it reads the window's own content, so nothing has to be in
    front and nothing gets raised. Reach for it by default when a human is
    present; keep `-NoActivate` for the case where the window is already clear
    and you merely do not want the focus stolen, and remember that `-NoActivate`
    alone removes the raise **without** removing the hazard.
  - `-Area X,Y,W,H` is **window-relative** — the coordinates you read straight
    off a capture — and it is deliberately NOT cached the way Linux caches
    `area.txt`: the window may have moved since the last run, and its geometry is
    free to ask for. The same geometry flags work for a single `-OutFile` shot.
  - `-OnlyChanged PCT`, `-IdleStop K` / `-IdleBelow`, `-Every`, `-Count` or
    `-For S`, `-Tile`, `-Cols`, `-Sheet`, `-NoFrames` do exactly what their
    `--only-changed` … counterparts below do, including the saturation trap:
    calibrate `-OnlyChanged` against a first run's column instead of guessing
    (verified at 5%, which kept 6 of 11 frames and marked the rest `-`).
    `-IdleStop 2` verified on a static window: it stopped on the second
    consecutive 0.000% frame.
  - It only CAPTURES, so it composes with whatever drives the game. A background
    `--pad` is the usual partner — one `-Watch` over a 1.5 s right-stick hold
    read **17.6 / 27.7 / 21.5%** during the turn and **0.000%** after the
    release — but mind the two Windows traps in the `--pad` bullet above (the
    unquoted `Start-Process` script, and the replace race that used to abort long
    holds); both make a working channel look dead. PCSX2's own held keys are the
    fallback, and this ini binds the left stick to **Z/Q/S/D**, not the W/A/S/D
    the section above quotes. Held keys need the window in front, which `-Watch`
    arranges anyway.

  **On Linux/Wayland use the bundled `wayland-control.py`** — screenshots *and*
  synthetic keyboard/mouse, no human in the loop:

  ```bash
  python3 .agents/skills/tyra-testing/scripts/wayland-control.py shot -o <scratchpad>/shot.png
  ```

  It talks straight to **mutter's own D-Bus APIs** (`org.gnome.Mutter.ScreenCast`
  for pixels over PipeWire, `org.gnome.Mutter.RemoteDesktop` for input). Neither
  prompts, and both work for native Wayland surfaces — which is the whole point:
  the editor's GLFW window and PCSX2's Qt window are Wayland surfaces, so X11
  tools see nothing at all (`xwininfo -root -tree` lists neither). Do not spend
  time on the paths that look obvious and are dead: `gnome-screenshot -f` exits 0
  and writes nothing, and `org.gnome.Shell.Screenshot` / `org.gnome.Shell.Introspect`
  answer `AccessDenied` to any plain session client. The mutter APIs one level
  below them are not gated. Needs `python3-gi` + `gstreamer1.0-pipewire`, both
  stock on Ubuntu GNOME.

  Drive a whole interaction with `script`, which runs it in ONE mutter session —
  a session dies with the process, so a chain of one-shot calls re-negotiates
  PipeWire every time (~0.6 s each) and drops pointer state in between:

  ```bash
  python3 .agents/skills/tyra-testing/scripts/wayland-control.py script - <<'EOF'
  key ctrl+n
  sleep 0.6
  click --at 917,382
  key ctrl+a
  type WlTest_42
  shot dialog.png --area 807,345,375,365
  EOF
  ```

  Coordinates are global screen pixels — the same space a full-screen `shot`
  returns, so read a target's position off one capture and click it in the next
  step. Verified on this box: menus and dialogs opened by clicking in the editor,
  `ctrl+n` / `ctrl+a`, typing that needs shift levels (`WlTest_42` arrives
  verbatim), right-drag and wheel orbiting/zooming the viewport, and `k` reaching
  PCSX2 as **pad 1 Cross** inside the emulated machine (the PS2 BIOS advanced
  past its language screen).

  Two limits. There is **no per-window capture**: mutter's `RecordWindow` wants a
  window id that only the denied `Shell.Introspect` hands out, and the ids are
  random-based rather than sequential (probing 0..79 matched nothing), so capture
  the monitor and `--area` crop instead — and remember an occluded window
  captures as whatever is on top of it. And a **pointer-locked** client (PCSX2
  with mouse capture on, a game grabbing the cursor) never sees absolute motion:
  use `movrel` there, not `move`.

  **To WATCH the game over time, use `watch`** — the same one-session trick
  applied to TIME. It samples the screen on an interval off the
  already-negotiated stream and reports **one downscaled contact sheet** plus a
  changed-pixel table, so a minute of gameplay costs about as much context as a
  single screenshot instead of thirty:

  ```bash
  python3 .agents/skills/tyra-testing/scripts/wayland-control.py \
      watch <scratchpad>/w --auto --aspect 4:3 --every 1 --for 20 --tile 224
  ```

  Measured on the endless-runner fixture (1920x984 screen, 1157x868 render
  area): 9 tiles at 224 px = a 696x576 sheet, **~534 tokens** where the nine
  full-resolution frames would be ~12 000, and the run costs ~1.2 s of wall
  clock per frame on top of the interval. The full-resolution crops stay on disk
  as `frameNN.png`, so the sheet is what you READ and a single interesting
  index is what you open afterwards — that split is the whole point.

  What the flags are for, and the traps:
  - **`--auto` finds the render area by MOTION** (there is no per-window
    capture, see above): it diffs four samples ~0.5 s apart and takes the widest
    and tallest contiguous band of moving columns/rows, so PCSX2's FPS readout
    loses to the picture. It reports only what MOVES — a parked camera under a
    static sky yielded `415,425,1157,536`, the ground half of the frame — so
    add **`--aspect 4:3`** to grow it back to the PS2 picture from its bottom
    edge (`415,80,1157,868` against a true `415,93,1157,868`: 13 px of menu bar
    in the tile, harmless). The rect is cached in `DIR/area.txt` and reused, so
    detection is a once-per-session cost.
  - **A still scene defeats motion detection**, and that failure had to be made
    loud: on the day-night fixture 1.5 s of a slow lighting gradient moved
    **500 pixels of 1.9 M**, and the winner was the `Speed: 64%` text — an 8x10
    box. `--auto` now refuses anything under 2% of the screen or outside a
    0.9-2.6 aspect and tells you to read the rect off one `shot` and pass
    `--area X,Y,W,H` (which is also cached). `--trim` shaves black letterbox
    borders off whatever rect you give.
  - **`--only-changed PCT`** drops frames that changed less than PCT **since the
    last KEPT frame** (they still appear in the table, marked `-`), which is how
    a long watch stays cheap. Beware that the metric SATURATES on a repeating
    scene, exactly like the axis-aligned-walk trap above: the scrolling
    checkerboard terrain never exceeded 25% between frames however long the gap,
    so `--only-changed 25` kept a single tile. Calibrate it against the
    frame-to-frame numbers of a first run, don't guess.
  - **`--idle-stop K`** ends the run after K consecutive frames under
    `--idle-below` (0.05% by default) — "watch until it settles" without a
    fixed count. Verified against a pure-black crop: three 0.000% frames and it
    stopped.
  - `--every S` / `--count N` or `--for S`, `--tile W` (tile width, the only
    real knob on context cost), `--cols N`, `--sheet NAME`, `--no-frames`.
    `wayland-control.py area` prints the detected rect and exits.

  It composes with the Remote Pad: run `--pad` in the background and `watch` in
  the foreground (or the reverse) and one sheet shows the whole drive — turn,
  walk, release — with the diff column as the numeric evidence the input
  arrived, which is the measured-not-eyeballed rule below applied to a sequence.

  **To DRIVE the editor, use `--ui-script`** (docs/ui-scripting.md) - the editor
  runs for real and holds its own mouse and keyboard, naming WIDGETS instead of
  pixels, with **no window focus needed on either OS**:

  ```powershell
  build\tyrax-editor.exe --ui-script <projectDir> `
      'frames 20; click Tools; click "Remote Pad"; shot panel.png; quit'
  ```

  `click|rightclick|hover|doubleclick|hold|drag|wheel|key|text|wait|frames|shot|dump|log|quit`
  plus `expect` / `expect-not` / `expect-checked` / `expect-unchecked`; the exit
  code is 0 only if every step passed, so a scripted GUI run gates a shell
  script. **Always start with `dump`** (`--ui-script <dir> "frames 20; dump"`):
  it prints every widget on screen with its rect AND its checked/open state, so
  you neither guess a label nor read a value off a screenshot. Four things that
  save time: a step that names a target WAITS for it (menus need no sleeps, and a
  timeout prints what was on screen instead), a target is `"Window/Label"` with
  prefix matching (`"Remote/Cross"` works, and so does a menu entry without its
  `...`) — **quoted, with either `"` or `'`** (so on PowerShell wrap the whole
  script in one kind and use the other inside; an unquoted two-word target
  arrives as two tokens and fails). Quoting also makes the target OPAQUE, which
  is the part worth remembering: a `#` or `;` inside quotes is an ordinary
  character, so a name straight out of `dump` — ImGui child regions are
  registered as `Project/##objects_DC0BCE04`, `#` and all — can be pasted into a
  script verbatim, and a window name containing `/` resolves because every split
  point is tried. `shot` writes the same self-captured framebuffer as `TYRAX_SHOT`, and
  what it CANNOT name is anything not made of ImGui widgets - the 3D viewport
  (one big item: `drag` inside it, or work through the Project panel's list), the
  imnodes flow canvas and the ImGuizmo gizmo. **The four pointing steps take an
  optional `<dx>,<dy>` offset from the target's centre**, which is how you reach
  something the editor DRAWS over a widget rather than submitting as one - a
  marker, a handle, a comment icon (docs/comments.md). Anchor on a real item
  near the picture and offset into it: `click 'Viewport/Move (1)' 545,303`
  selected a comment by its icon, with both rects read out of one `dump`. Two
  things about that measurement - take the icon's own pixels off a `shot`
  (find the colour, average it) rather than eyeballing, and pair the click with
  an `expect` that only the intended object produces (`Properties/Copy text` is
  a comment and nothing else), or "it selected something" is all you proved. Not all modals close on `escape` -
  click their `Cancel`; `dump` shows it. **A rect in `dump` is not a promise the
  click will land**: a window taller than the room it got still submits the items
  past its bottom edge, so they are listed with rects OUTSIDE the window, and
  `click` on one presses over nothing and still reports success - which reads as
  a broken feature when the code is fine. Compare the item's y against its
  window's rect (both are in the same `dump`) and **pair every state-changing
  click with `expect-checked` / `expect-unchecked`**, which turns a silent no-op
  into a failed run.

  **Combos and tabs ARE nameable now** (they were not, and both were silent
  gaps): `BeginCombo` never calls ImGui's item-info hook at all, and `TabItemEx`
  calls it BEFORE the box exists, so every combo and every tab in the editor
  dumped as `-`. `uiscript.cpp` closes both - a label reported ahead of its box is
  held until the box arrives, and a target that matches nothing by label is
  matched by RE-HASHING it the way ImGui would (`ImHashStr(label, 0,
  window->ID)`), which is what finds a widget whose label ImGui never reported.
  So `click 'Preview in'` and `click Style` work, and a combo's OPTION can be
  clicked by its own text once the dropdown is open. Two limits remain: the hash
  path needs the EXACT label (a hash has no prefixes) and only reaches widgets
  submitted at a window's own scope.

  **AND A TAB ONLY HAS CONTENTS WHILE IT IS THE SELECTED ONE. SELECT IT FIRST.**
  `BeginTabItem` returns false for every tab but the front one and its body is
  never submitted, so nothing inside it reaches the registry at all: `dump` does
  not list it, `expect` fails, `click` fails - and the failure reads as "that
  control has been removed" rather than "that tab is shut". Every script that
  drove *Project > Preferences* broke the day it grew tabs, and the fix in each
  was one step (`click 'Project Preferences/Display'; frames 5`) before the
  control. Three rules go with it:
  - **Qualify a tab with its window.** Tab labels are short common words and
    `find` takes the first match: *Project Preferences* has a `Build` tab while
    the menu bar has a `Build` menu, so a bare `click Build` opens the MENU. Its
    five tabs are `Display`, `World`, `Rendering`, `Player`, `Build`.
  - **A tab pushes an id, which moves what the hash fallback can see.**
    `BeginTabItem` ends in `PushOverrideID(tab->ID)`, so a widget submitted
    DIRECTLY in a tab is seeded by the tab and not by the window the fallback
    hashes against. Wrapping the tab body in a `BeginChild` restores it (a child
    is a window and reseeds the stack) - which is what Project Preferences does,
    verified: `click 'Project Preferences/Mode'` opens the BLSS mode combo four
    levels in. A tab body with no child needs real labels on anything scripted.
  - **A tabbed dialog stops needing `wheel` and starts needing tab clicks.** The
    trickle trap below still applies to every other long window, but Project
    Preferences no longer scrolls to reach its footer - the footer is pinned
    outside the scrolling region, so `click 'Project Preferences/Close'` lands
    from any tab. A tab BODY still scrolls: with the upscaler on, the Display
    tab's *Advanced...* button is submitted below the child's bottom edge, where
    `dump` prints a rect and NO label and a click on it lands on the footer's
    Close button instead - the clipped-item trap two bullets down, measured
    here.

  **Project Preferences is a WINDOW, not a modal, since 1.20.0.** Its footer
  button is `Close`; there is no `OK` and no `Cancel`, because every edit
  applies as it is made - so a scripted run asserts by reading the model back
  (`key ctrl+s` then grep the `.tyra`, or `--dump`) instead of pressing a
  confirm button. Nothing is blocked behind it any more: `click 'Project
  Preferences/Advanced...'` leaves it AND *Neural Upscaler (BLSS)* open and both
  take clicks, which is the end-to-end check for that change. Two consequences.
  A bare label is ambiguous with two windows up (`Use the upscaler` is in both -
  qualify it), and the upscaler window opens ON TOP, so a `click` on a covered
  Preferences item lands on the window in front; assert covered items with
  `expect-checked`, which does not click. The terrain grid (*Width (units)*,
  *Depth (units)*, *Detail (max grid cells)*) is the one thing that does not
  apply as you type - it writes back on release, so `text` needs a `key enter`
  after it (measured: an uncommitted `text 2` leaves the stored width at 100).

  **A widget whose whole label is
  hidden behind `##`** - a compact search field (`##assetsearch`,
  `##objsearch`) - still has nothing to name and dumps as `-` with a rect and
  nothing to name, so a filter row like the Scene panel's is only reachable by
  CLICKING ITS RECT: read the rect off `dump`, add the editor window's screen
  offset (compare one `dump` rect against one full-screen `shot` to get it -
  ~67,70 on this box) and drive it with `wayland-control.py click --at x,y` /
  `type`. Everything else about the run stays the same, and the assertion is
  the `shot` you crop afterwards.

  **`wheel <target> <notches>`** is how a canvas ZOOM is driven (no widget
  exposes one): it holds the cursor on the target and injects one notch per
  frame, and it is the only step that may resolve a bare WINDOW name, so
  `wheel "Flow Graph" -6` scrolls over the middle of the canvas. The way to
  verify a zoom is not a screenshot but `dump` at two zoom levels: the node
  widgets ARE ordinary items, so their rects give you the scale factor and the
  offsets between them give you whether the layout stayed self-similar
  (PROGRESS 233 measures both to under 0.1%).

  **`wheel` is also how you reach a widget below a long modal's fold - and it
  keeps scrolling for far longer than the step takes.** ImGui trickles queued
  input ONE EVENT PER FRAME (`io.ConfigInputTrickleEventQueue`), so a
  `wheel X -19` hands the window nineteen notches over the following nineteen
  frames *at least*, and a `frames 10` after it is not enough: the view is still
  moving when the next `click` computes its target, the window slides out from
  under it, and the click lands on whatever arrives there instead. It reports
  SUCCESS - the item existed when it was looked up - so this reads as "the button
  does nothing" and costs an hour. Measured driving *Project > Preferences* to its
  BLSS block: at `frames 10` the view moved another 300 px between the `dump` and
  the click and the button was never pressed; at `frames 90` two consecutive dumps
  agree and the click lands. So: **`frames 60`-`90` after any `wheel`, then `dump`
  TWICE and require the rects to agree** before clicking anything. The same
  trickle applies to `text`, which is why the chat recipe below needs its
  `frames 5`. (That measurement was taken on the one-long-stack version of
  Project Preferences; that dialog is tabbed now and its BLSS block is two
  clicks away with no wheel at all. The trap is unchanged for every other long
  window - it is about ImGui, not about that dialog.)

  **Two more `--ui-script` traps, both about a label being an id.** A label with
  an APOSTROPHE cannot be named at all - the tokenizer opens a quoted run on a
  single quote at a token boundary, so `'Will this project's frames get shorter?'`
  parses as two tokens and the step dies with *"click needs one target"*; a button
  that has to be scripted must not have one (that is why the Preferences verdict
  button reads *Will the frames get shorter?*). **An apostrophe in GENERATED TEXT
  is the same hazard wearing another hat**: an unpaired `'` (or `"`) after a
  `#error` is read by GCC as an unterminated character constant, so every
  translation unit that reads the file gets a *missing terminating ' character*
  warning on top of the real diagnostic. The BLSS interlock shipped
  "the upscaler's temporal pass" and put a bogus warning on all fourteen; the
  guard is `errorSafe()` in templates.cpp, now applied to the whole line. Reach
  for a plain word - the hazard is invisible in the C++ that writes the string.
  And **the same label in two
  different windows is legal in ImGui and ambiguous to `find`**, which takes the
  first match - a bare `click Mode` with both *Project Preferences* and the BLSS
  window open pressed the wrong one and then failed on the option that never
  appeared. Qualify as `'Project Preferences/Mode'`. The same rule catches
  `click Project`, which matches the Project PANEL'S DOCK TAB long before the menu
  bar; the menu is `'##MainMenuBar/Project'`.

  **On Windows PowerShell 5.1, put the script in DOUBLE quotes and its targets in
  SINGLE ones** - not the other way round. Native-command argument passing
  re-parses the string and eats embedded `"`, so
  `TYRAX --ui-script $P 'click "Remote Pad"'` reaches the editor as
  `click Remote Pad` and fails; `"click 'Remote Pad'"` survives intact. Both
  spellings are equally valid to the tokenizer, so this costs nothing - but it is
  the same class of mangling as the `Start-Process -ArgumentList` trap in the
  `--pad` bullet above, and it fails with the same unhelpful message.

  Two things about the node canvases specifically (PROGRESS 247, which needed a
  screenshot of a node tooltip). **A node's param widgets register only while
  its window is the FRONT tab.** Behind another tab the window is drawn with
  `SkipItems`, so `dump` lists the node rects (unlabelled - imnodes gives them
  no label) and *nothing inside them*, which reads exactly like "the canvas is
  unreachable". The Flow Graph ships docked behind Viewport in the Default
  layout, and dropping a window from the layout's `open` list does NOT help -
  Viewport is not optional and is not in that list. Set `"activeLayout"` in the
  project's `.tyra` to a layout whose recipe FOCUSES the window instead
  (`LayoutRecipe::Debugger` = index 3 focuses Flow Graph; `Procedural` focuses
  Procedural), then everything inside the nodes is nameable as
  `"Flow Graph/<Param>"`. **And `wheel` parks the cursor**, which is how you
  reach a tooltip that hangs off no widget at all: it holds the mouse at the
  target's centre while the notches arrive and ImGui keeps that position
  afterwards, so `wheel "Flow Graph" 1; wait 1.6; shot x.png` screenshots
  whatever the canvas shows for the point under the window's middle - put the
  node there (screen = canvas origin + model position x nodeScale, both readable
  off one `dump`) and you have the node-hover tooltip. What stays out of reach
  is the **add menu**, which needs a right-click on empty canvas.

  The editor can also **capture its own framebuffer** on a timer, with no
  display permissions at all: set `TYRAX_SHOT=<dir>` (and optionally
  `TYRAX_SHOT_EVERY=<seconds>`, default 2) and it writes `<dir>/shotNN.png` every
  interval (`App::captureFrameIfRequested`). It reads what the editor DREW rather
  than what was presented, so it is the one path that survives the AMD present
  quirk that leaves the window blank. It cannot click anything - before
  `--ui-script` existed the way to reach a panel behind a menu was to pre-open it
  by adding its key to the active layout's `open` list in the project's `.tyra`
  (`kLayoutWindowKeys` in app.cpp has the names - `"drone"`, `"tree"`,
  `"material"`, ...), which is how PROGRESS 210/211 verified whole tool windows.
  That trick still works and is fine for a pure "does it render" check; anything
  that needs a click or an assertion is now a UI script.

  For **editor viewport** work an **offscreen GL harness** (PROGRESS 208) is
  still the better instrument when you want numbers instead of a picture.
  A hidden GLFW window (`GLFW_VISIBLE` false, `GLFW_INCLUDE_NONE`
  before glfw3.h so the loader's symbols win), `glInit()`, a real `Viewport`,
  real `render()` calls at a real panel size, then `glReadPixels` off
  `lastImageFbo_` into a PNG through the already-linked `stb_image_write`
  (do NOT define `STB_IMAGE_WRITE_IMPLEMENTATION` — menubake.cpp.o has it, and
  stbi_load comes from app.cpp.o, so link ALL of `build/`'s objects minus
  `main.cpp.o`, `-no-pie`). That isolates the viewport image from the UI, is
  measurable (bounding boxes, bar widths, pixel ratios) rather than
  eyeballed, and works with no display permissions at all. What it cannot
  cover — the surrounding UI and anything dragged by hand — say so in the
  commit and leave it for a human.

  **A screen effect that cannot be SEEN and one that is not happening look
  identical** (retired PROGRESS entry 231). A Camera Shake at amplitude 0.05 measured **0
  pixels differing** across four captures 250 ms apart — the ease-out cuts it to
  0.03, and a 3 cm camera translation is sub-pixel at 512x448 with nothing closer
  than 5.6 m. At amplitude 2.0 the horizon swept 215 px and 545k pixels differed.
  So before debugging a subtle visual, do the arithmetic on how many PIXELS the
  effect is worth at the render resolution, and re-run the fixture with the
  effect turned up past that. The flip side is useful too: a fixture whose frames
  are otherwise byte-identical between captures is a clean instrument — that zero
  is what lets a 200-pixel horizon shift mean something.

  **A capture is worth more when it is measured** (PROGRESS 132). Whichever
  tool produced the PNG, read pixel ROWS out of it rather than eyeballing:
  plateau widths and adjacent-step sizes are what told flat shading apart from
  Gouraud on a cylinder, and two engine builds A/B'd from a frozen fixture came
  out byte-identical, which is a much stronger statement than "looks the same".
  A few lines of PIL will do it.
  Driving PCSX2 by hand (rather than through `--build --run`) inherits none of
  the launcher's setup: the Runner is what forces `HostFs` and the USB ports in
  `PCSX2.ini`, so run through the editor at least once first, and set
  `Renderer = 13` (software) in that ini while PCSX2 is **closed** — it rewrites
  the file on exit.

  **When an A/B compares two objects in ONE frame, prove which is which**
  before reading anything into it. The screen's X runs opposite to world X in
  the generated game (a Player at yaw 0 looks along +Z), so "the left one" is
  the object at *positive* X. An hour went into a banding hypothesis about the
  wrong cylinder. The cheap disambiguator: force one object's colour to
  something absurd for a single run and see which one changes.
- **A VU1 TIMING hazard is invisible in PCSX2 under EVERY renderer, and only a
  console shows it.** The rule above is about the GS; this one is about the VU,
  and switching renderers does nothing for it. PCSX2's VU does not model the
  pipeline hazards the hardware has, so a microprogram that reads a register one
  row too early runs *correctly* in the emulator. Measured the hard way: a branch
  at a label whose condition was produced in a jump's delay slot clipped against
  the wrong frustum planes on a real PS2 — triangles appearing and occluding the
  screen, changing with camera movement — while the same ELF rendered a clean
  picture in PCSX2, logged zero asserts, and passed a path-sensitive value oracle
  over 277 traces and 2631 branch conditions. Every value oracle is blind to it by
  construction: the defect changes WHEN a register is readable, not what is
  computed. If a change touches VU scheduling, latency or padding, PCSX2 is a
  smoke test and the console is the verdict. Symptom vocabulary for the related
  ADC-bit class: **stray smeared triangles at screen edges**, which the HW
  renderer also masks.
- **A temporal effect needs a SETTLE test, not a frame diff.** "The two
  captures are identical" does not mean the picture is frozen, and reading it
  that way cost a full round on the motion-blur change. Two independent reasons,
  both live on the stock `fpp` fixture: a heavy trail over a **repeating**
  pattern averages it FLAT (the checkerboard ground smeared over ten frames of a
  yaw turn is near-uniform green, and uniform green looks the same from every
  heading - the axis-aligned-walk trap one dimension over), and a long trail is
  indistinguishable from a freeze *while the camera moves* anyway. Ask instead
  whether the picture CATCHES UP: drive the motion, `neutral`, wait a few
  seconds, capture twice. A settled frame that is SHARP is a trail; one that is
  smeared - or, in the case that produced this note, still showing the **boot
  splash** thousands of frames into the run - is a freeze. One look at the image
  answers it, where three pixel tables did not. Also inspect static high-contrast
  HUD text after the idle clear: a diagonal down-right tail means the 1:1
  history sprite sampled texel boundaries instead of centres, even when point
  filtering is enabled. Motion blur's default idle
  clear is ONE sharp frame after about 0.12 seconds below its meaningful-motion
  rate threshold, not a permanent zero
  amount: after the settle capture, keep the camera parked and move an object
  across it. The object must still trail. Test the option-off arm separately
  because its 16-bit fixed-point residue is expected. Test at a forced low frame
  rate or on hardware too: a per-frame displacement threshold passed at 60 FPS
  in PCSX2 and failed at ~30 FPS on PS2 because pad drift doubled per frame.
- **Rendering correctness**: switch PCSX2 to the **software renderer** before
  judging visuals — the HW renderer masks GS raster-window wrap bugs that real
  hardware shows. Give the game a few seconds to reach a steady state, then
  screenshot; compare against a known-good screenshot when hunting regressions.
  The reverse trap bites within ONE run: **an axis-aligned walk over the flat
  checkerboard terrain is nearly invisible to a pixel diff**, because
  translating along the grid maps the repeating pattern onto itself — a 2.5 s
  forward hold from the default FPP pose changed an 11-pixel band at the
  terrain's far edge and nothing else, which reads as "input never arrived".
  Turn the camera first (right stick), then walk: the same hold then moves
  ~150k pixels.
  **A strict pixel A/B between two RUNS usually cannot work** — an orbiting or
  auto-spinning camera is at a different phase in each boot, and the
  interlaced FIELD modes alternate fields, so window captures never line up
  (a "1.7M pixels differ" result is almost always this, not your change). Two
  ways out: freeze the camera/pose in the fixture (what the VU0-skinning
  entry did), or better, **compare the DATA on the console**: log an FNV hash
  (and the first few raw bit patterns) of whatever the change produces from
  inside the game, and diff the log. For a format change you can even load
  BOTH formats in one run and compare them against each other — one build,
  no camera dependency, and it isolates host-vs-EE float differences from
  real bugs (see PROGRESS 163).
- **Performance**: PCSX2's FPS overlay, software renderer, 3+ samples. Full PAL
  frame rate is 50 FPS — the generated showcase scenes hold it. For *where* a
  frame is spent, enable the built-in **frame profiler** (debug build profile +
  *Preferences > Build > Show frame profiler*): per-phase EE ms on the HUD
  (whole frame / scene / usable-highlight / particles). For **GS VRAM**
  specifically, a debug build prints `VRAMSTAT` lines into the game's
  `bin/log.txt` (texture binds/hits/uploads/re-uploads/evictions, resident
  count, free MB, largest free block) — the honest way to tell "the scene is
  thrashing textures" from "the scene is just heavy"; see
  [docs/gs-vram.md](../../../docs/gs-vram.md). Parallel worktree sessions each
  run their own emulator, so when several are up
  `screenshot-window.ps1 -ProcessName pcsx2-qt` grabs whichever it finds first
  (it warns, but the frames are already wrong) — pass **`-ProcessId <pid>`**
  with the pid whose `-elf` path is your project. A `--build --run` used to reap
  **every** PCSX2 on the machine by name and interrupted those sessions
  repeatedly; since 1.22.0 it closes only the instance whose `-elf` names your
  own ELF, so a launch of your project no longer ends somebody else's
  measurement. Their *build* is still shared ground (one container per project
  name, one engine volume per checkout), so when another agent's session is live
  it is still politer to build with plain `--build` and launch PCSX2 yourself on
  `bin/<name>.elf` (`-logfile <path>` keeps your emulog out of theirs).
  **Adaptive BLSS needs a hardware decision gate.** PCSX2 can verify codegen,
  forced threshold transitions and the moving-picture gate, but its software
  rasterizer may never cross the real fill-bound threshold. On PS2, capture the
  same parked pose once native and once with Plain + adaptive. Require the log
  to report `Adaptive resolution: REDUCED`, capture through `--capture-frame
  <projectDir>`, and compare HUD frame/scene milliseconds. Flat scene time with
  a shorter whole frame locates the gain in GS fill. Stop the host session
  before the next boot.

  For a finer breakdown,
  the manual COP0/HUD deep-dive (own the generated `terrain_game.cpp`, bracket
  phases with `mfc0 $9`, deterministic camera orbit, in-run A/B, engine-side
  counters) is written up in [docs/profiling.md](../../../docs/profiling.md) —
  frames are almost always EE-bound; `endFrame` time is mostly vsync idle, not
  GS load. **For a real A/B in milliseconds** — an engine change, a feature that
  claims to make frames shorter — the same page's *frame-timing rig* is the
  instrument: engine counters behind `TYRA_FRAME_PROFILE` (default 0, so a
  shipped build carries nothing) and a once-a-second `FRAMETIME` line with
  mean/median/p95, plus a raw per-frame dump so two runs can be compared as
  PAIRED samples. Three rules from it that generalise: drive the camera from a
  **frame index in an object script**, not `--pad` (whose driver refreshes off
  the host wall clock, so the two runs never show the same view); turn Live
  Link / Live Debugger / Live Logic / Remote Pad / Time Machine **off** (a
  debug build polls `livepad.bin` through HostFs every frame); and **run the
  page's GS fill calibration before quoting any PCSX2 number about GS cost** —
  measured, PCSX2 under-reports fill by **76x**.
  **Level 2 is a fairness-drained diagnostic build, not ordinary frame timing.**
  Level 1 carries the packet-structure walker and the pipeline telemetry and is
  not transparent - HUD SCENE 13.21 ms uninstrumented, 17.70 at level 1, 13.04
  at level 2 on the same console pose. Every nonzero FrameProfile value enables
  `sync.align3D()` in RendererCore::endFrame. Matching SCENE cannot prove
  identical overlap or cadence. Use FrameProfile=0/HardwareTrace=0 with existing
  ordinary-clock boundaries and same-ELF Off/On/Off plus reverse controls for
  ordinary TyraX2 pricing. And read `pre`/`stall`/
  `period`/`miss` as well as `work`: `work` starts at `beginFrame()`, so a
  frame can miss its field with `over20=0`. **Over ps2link, a debug build's
  `miss` is mostly the devkit**: Remote Pad polls `host:` every 4th frame there
  and Live Debugger every 25th, and compiling both out took the Motor District
  day frame from 15/50 misses to 0-1/50 (docs/profiling.md). The same builds
  are also the ones whose software reset wedged the console (`freepad: DMA
  Busy`), while the devkit-off build survived two resets in a row.
- **Testing whether a picture is STILL needs a period-2 test, not a diff** — and
  **three** instruments in a row got this wrong before it stuck, so the full
  rules live in docs/profiling.md, "The stability gate". The short form, each
  clause paid for: freeze the camera **and every emitter** (on
  `examples/upscaler-lab` the running particles change more between frames than
  the artefact does, and they bury it); point it at **textured** content (a
  quarter-pixel resample cannot change an untextured box, so a minimal fixture
  measures clean and truthfully and tells you nothing); capture **back to back**
  rather than on a stride (an even stride lands on the same phase forever);
  **never `-Trim`** and take the crop once (trimming black borders re-registers
  a shifted picture to an identical image, so a displacement becomes invisible
  by construction); report a **cross-correlation lag** as well as a pixel count;
  and cluster by pairwise difference — two balanced clusters, near-zero within
  and large between, IS the alternation. Then check the verdict against a
  labelled A/B/C where you already know the answer, rather than against a
  threshold you chose.
  And on Windows **select the PCSX2 instance by the
  project on its command line** (`Get-CimInstance Win32_Process ... CommandLine
  -like "*<project>*"`, then `-ProcessId` that pid) — `-ProcessName pcsx2-qt`
  takes the first one it finds, and with parallel worktrees running that silently
  captures somebody else's game and looks exactly like a screenshot. **A GDI grab
  reads the SCREEN**, so an occluded PCSX2 captures as whatever is on top of it
  and yields a plausible, entirely fictional "stable" table — capture with
  **`-PrintWindow`**, which cannot see anything but the window itself, and diff
  frame 0 of one arm against frame 0 of another before believing any of it.
  **And then read "The motion gate" below**: freezing the camera and the
  emitters is what made this artefact reproducible, and it is also what makes
  this instrument blind to every fault that only exists while the picture is
  moving — four of which reached the owner on this branch.

- **What is the OWNER debugging right now?** When the user asks about "my
  scene", "the last capture", "why does my model look like that", do NOT guess
  at project paths — their projects live wherever they put them, and a blind
  `Get-ChildItem -Recurse` over the disk finds nothing useful. Ask the machine:

  ```powershell
  build\tyrax-editor.exe --debug-state
  ```

  It prints, per project, the live devkit artifacts in `bin/` with **how old**
  each is, and decodes the interesting ones inline — `vucap.bin` as
  *"frame 661, flush 2/9, 16 mesh(es), 94 tris in, 512x448"*, `livedbg.bin` as
  *"frame 1111, scene 0, HALTED"*. The last line names the freshest artifact on
  the whole machine, which is almost always the thing being talked about. Then
  go in with `--dump-vucap <thatDir>` (or read `bin/log.txt`).

  Where the list comes from, and its limits:
  - **Running editors publish themselves** (`devsession.hpp`): one small file
    per process under `%LOCALAPPDATA%\tyra-editor\sessions\<pid>.ini`
    (`$XDG_STATE_HOME/tyra-editor/sessions/` off Windows), carrying the open
    project, the build profile, whether the game is live/halted and at which
    frame, and **which transport** — `pcsx2` or `ps2link`. A file per pid, so
    several editors at once each get a line. Liveness is the **heartbeat**
    (refreshed every ~4 s), not a pid probe: a session that stopped beating is
    shown as `stale` rather than hidden, because a crashed editor's last known
    project is information. This is the source that works when nothing else
    does — a project opened for the first time, or a game on real hardware with
    no local emulator process to find.
  - **`editor.ini`'s recent-project list** (`%LOCALAPPDATA%\tyra-editor\editor.ini`,
    machine-global, shared by every build and worktree). It is rewritten the
    moment a project is **opened**, so entry 0 is the last one opened — but a
    project created by `--new` and never opened in the GUI is not in it, and it
    has been seen to miss a project that WAS open (unreproduced; the session
    pointer is the reason that no longer matters).
  - **the default projects folder** (`~/TyraProjects` unless Preferences moved
    it), scanned for the projects the list misses.
  - Anything else needs the path: `--debug-state <projectDir>` reports one
    project directly.

  **A running game beats all of it** — that is a process query, not a file one:

  ```powershell
  Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" | Select-Object CommandLine
  ```

  The `-elf` path is `<projectDir>\bin\<name>.elf`. Same trick for
  `tyrax-editor.exe`: its command line carries the project it was opened on,
  and its `MainWindowTitle` reads `TyraX - <project>`. And **never kill either
  process by name** — the owner may be sitting in front of one (a link step
  failing with *"cannot open output file tyrax-editor.exe: Permission denied"*
  means exactly that; ask them to close it, or link a check binary under
  another name).

  **On a real PS2 there is no game process to find, and the channel dies with
  the editor.** A ps2link deploy is served by a `ps2client.exe` that the RUNNER
  spawns (`src/runner.cpp`), so closing the editor takes the file server with
  it: the console keeps running but every devkit file freezes at its last
  write, and commands written to `livedbg.cmd` are never seen. Symptom:
  `livedbg.bin` stops advancing while the console still answers `ping`. The fix
  is a redeploy (*Run on PS2*, F6), not a retry — and it is why
  `--debug-state` reports the transport.

  A missing first `[ps2]` log line does **not** prove `execee` failed: the UDP
  command may have reached the console while its tty reply was lost. The Runner
  waits 15 seconds, reports an unconfirmed launch, and keeps `ps2client` alive.
  Never "clean up" that timeout by killing the process — doing so removes
  `host:` from a possibly running game, whose exact symptom is checkerboard
  text, missing models and silent audio. Use Stop on PS2 explicitly when the
  console really did not launch.

  **A deploy no longer kills anybody else's file server, and the story of why
  is worth keeping.** `deployToPs2`, `stopPs2` and `clean` used to run
  `platform::killByName({"ps2client"})` — `taskkill /F /IM ps2client.exe`,
  **machine-wide, by name** — so a *Run on PS2* of a DIFFERENT project (or of
  the same project from another worktree's editor, which this repo routinely
  has several of) silently took down the file server of the session you were
  watching. Diagnosed once from timestamps alone: `livedbg.bin` and
  `livetime.bin` in project A froze to the second at the moment project B was
  deployed, while `[ps2]` log lines kept scrolling in the editor's Output
  panel. **That log is the trap** — it is UDP straight from the console to
  whichever `ps2client` is listening, so it does not go through the file server
  at all and keeps arriving after the file channel is dead. Two transports, one
  dead, and only the live one is visible.

  Since 1.22.0 the Runner reaps only the servers it owns (its own by handle, a
  stale one of the SAME project by its command line, an orphan no running
  editor claims) and **refuses**, naming the project that holds the channel,
  for anything else — docs/ps2link-setup.md, "One file server at a time". So
  when a channel is dead now, ask `--debug-state`: it lists every `ps2client`
  with the console (`-h`) and the game (`execee host:<name>.elf`) its command
  line names, which is the same identity the Runner decides ownership by. If
  that ELF is not the project you are debugging, somebody else has the channel
  and your deploy will say so rather than stealing it.

  **`platform::killByName` is gone from the tree, deliberately.** Use
  `platform::processesNamed(name)` + `platform::killProcess(pid)` and decide
  ownership from the command line; a process whose command line cannot be read
  is never a target. Note the discriminator is the command line and not the
  working directory, even though cwd would be sharper (the deploy runs its
  server with cwd = `<project>/bin`): Linux answers that with one readlink of
  `/proc/<pid>/cwd` and Windows has no supported way to ask, and a key only one
  platform can compute is worse than a slightly weaker key both can.

  That leaves a bind for **scripted** hardware debugging: a probe needs the file
  server alive, but the editor that hosts it also drives `livedbg.cmd`, and two
  writers on that file is a known hazard. Break it by hosting the server
  yourself. **A game that is still running does NOT need a redeploy** — it is
  blocked on `host:` and resumes the moment a server answers:

  ```bash
  cd <projectDir>/bin && ../../tools/ps2client/bin/ps2client.exe -h <ps2-ip> listen
  ```

  `listen` serves the console's file traffic and nothing else — within seconds
  `livedbg.bin` starts advancing again and captures work.

  **Do not send ps2client COMMANDS at a console with a game loaded.** Measured
  the hard way: with a game running (or starving), any client that connects is
  immediately conscripted as its file server and the command never reaches
  ps2link — `reset` and `dumpmem` both returned -1 while the game's `host:` opens
  scrolled past. Attaching a `listen` server first does let a command through,
  and **the one that got through froze the game**. The reliable sequence is the
  runner's: nothing else attached, `reset` (exit 0 only when the link is free),
  then `execee`. And ps2link's extra commands are **not** the free lunch the
  help text suggests: on the pinned build `dumpmem` answers `EE: pkoDumpMem()
  write failed` (the destination file is created, zero bytes) and `scrdump`
  exits 0 having written nothing — vestigial pko-era plumbing, not working
  tools. Anything wanted from them is a **ps2link patch**, which is the normal
  workflow here: the console always runs OUR ps2link (`tools/ps2link/` clones a
  pinned upstream, applies `tyrax.patch` and builds it in Docker via
  `build.sh`/`build.ps1` — see docs/ps2link-setup.md).

  **ps2link is no longer hardware-only: it runs in PCSX2.** A SECOND, portable
  copy of the emulator (`-portable`, its own `inis/bios/logs`, so it cannot
  disturb a parallel session or the editor's own launches) with **DEV9 bridged**
  onto the LAN boots `ps2link.elf` and answers the real `ps2client` — same
  subnet, same ports, `reset` and `execee` included. A wedge then costs
  `Stop-Process` instead of a walk to the console, which is what makes a
  Run → Stop → Run A/B a two-minute scripted test: measured 2026-07-31, r4 falls
  through to the BIOS browser and drops off the LAN where r6 comes back and runs
  the payload again. The full recipe, the four gotchas (`[DEV9/Eth]` is the ini
  section, `EthDevice` is the bare adapter GUID, `host:` is served by PCSX2's
  HostFs relative to the `-elf` directory and NOT by ps2client, ping proves
  nothing in either direction) and the limits are in
  **docs/ps2link-setup.md — "Testing a change without a console"**. The limit
  that matters: **the emulator cannot produce an EE exception** — replayed the
  r4 `BadAddr 8` crash there and the `printf` returned normally, because main
  RAM starts at address 0, so a NULL dereference is an ordinary load. Faults,
  `crash.txt` and the crash handler stay hardware tests; sequencing, restarts,
  IOP reboots and the protocol do not.

  **ps2link is not entirely untestable any more.** `tools/ps2link/test/run.ps1`
  (`run.sh`) compiles the REAL patched `build/iop/net_fio.c` against stub
  IOP/lwip headers in `test/shim/` and drives it with a scripted fake socket —
  framing, EOF, short reads, buffer clamps. What makes it evidence rather than
  decoration is the `-Pristine` / `--pristine` mode: the same tests against the
  untouched upstream file, which must FAIL (it does, 7 of 18, three of them by
  spinning past a 2000-call `recv()` ceiling). Use this layer for any change to
  the `host:` protocol code before reaching for hardware; threads, the SIF and
  the GS are still console-only. Note the `#include "net_fio.c"` trick — the
  harness pulls the .c in so it can place `pko_fileio_sock`, which is static.

  Since r2 of the patch, several "the console is hung, hit Reset" failures are
  gone: a closed `ps2client` socket used to spin the IOP `host:` thread forever
  while holding its semaphore, an unexpected reply desynced the stream for good,
  and `pkoReset()` waited on an inverted `SifIopSync()` on every deploy. **r3**
  adds `spu2Silence()`, so the SPU2 no longer drones the dead game's voices
  through a reset — which means **"Stop on PS2" no longer deploys
  `tools/silencer/silencer.elf` and no longer sleeps ~7 s**; if you are timing
  Stop, that is why it got faster. The tool stays as a manual fallback for a
  pre-r3 console. Check the boot banner reads `TyraX ps2link r3` and the log has
  `SPU2 silenced` before blaming anything else. The
  `dumpmem`/`scrdump` uselessness above is NOT explained by any of that and is
  still open — r2 only stops the failure leaking a `host:` fd each attempt. (A
  tempting theory, recorded so it is not re-tried: that the EE side passes
  newlib `O_*` values where the wire wants `FIO_O_*` (`O_RDONLY` = 0, but
  `FIO_O_RDONLY` = 1). It is wrong — the engine's asset loads use plain
  `fopen("rb")` through the same path and work fine, so newlib values are what
  this `host:` expects.)

  (Verified: a session
  orphaned for 20 minutes came straight back, no reboot, no rebuild.) Only when
  the game is actually gone do you need the runner's two commands —
  `ps2client -h <ip> -t 10 reset`, then `ps2client -h <ip> execee host:<name>.elf`
  with `cwd = <project>/bin`, that second process being the file server for the
  whole session. Use the **main checkout's** copy of ps2client either way: a
  worktree path is a different binary to Windows Firewall and pops a prompt
  nobody is watching.

  With the server yours and the editor closed, a probe can pin each flush in
  turn and summarise the frame — which is how "my model shows 2 meshes" was
  traced to a bag flush full of terrain (PROGRESS 204). Since PROGRESS 205 the
  **flush map** makes that one read instead of a walk: `livedbg.bin` (snapshot
  **v4**) ends with a 64-byte stats block (FPS, flushes/quadwords/vertices to
  VU1, GS VRAM free/low-water/largest/resident, object counts, free EE RAM) and
  one 8-byte row per bag flush (qw, unpacks, verts, program). A ~60-line Python
  probe reads it — that is how the block was verified, cross-checking the VRAM
  figures against the game's own `VRAMSTAT` log line and the flush map's vertex
  counts against `--dump-vucap` of the same capture. **v3 snapshots still
  parse** (a console running an older build keeps working, minus stats), so
  when stats are missing the first question is "was the GAME rebuilt?".
  Free EE RAM is measured only when asked (command flags **bit 5**): the
  engine's measurement allocates every free block until malloc fails.
- **Flow-graph logic, without a pad or a screenshot**: a debug build with the
  *Live Debugger* preference on (docs/live-debugger.md) writes
  `bin/livedbg.bin` every 6 frames - per-node hit counters, a ring of recent
  fires, the flow variables and save values, the halted flag - and reads
  `bin/livedbg.cmd` (breakpoint list, halt/step, force-fire). Both are small
  fixed-layout binaries (`src/livedbg.hpp`), so **a ~40-line Python probe
  replaces the whole editor for scripted verification**: read the snapshot to
  assert which nodes ran how often and what the variables hold, and write a
  command to prove Pause freezes the counters, Step frame advances exactly
  one frame, or a force-fire runs a trigger's branch (that is how entry 191
  was verified). The key -> object/node map is `src/gen/livedbg.sym`; the
  editor and the probe must not both drive `livedbg.cmd` at once (the editor
  rewrites it whenever it goes missing or its state changes).

  **Prefer a C++ probe over a Python one here, and link the editor's own
  decoder**: `src/livedbg.cpp` has no GL/ImGui/project.hpp dependency, so a
  ~60-line `main()` compiled with **just that one file** (`g++ -std=c++20
  -static probe.cpp <repo>/src/livedbg.cpp -I<repo>/src` — `-static`, or the exe
  dies with `0xC0000139` looking for MinGW's DLLs) calls `loadSymbols` +
  `readSnapshot` and prints per-node hit counts with their node ids and types,
  every watch variable BY NAME, and the armed timers. A hand-rolled Python
  reader can disagree with the format; this one cannot. That turns "did this node
  fire, how often, and what did it leave behind" into a command — which is how
  the 84-node flow-graph expansion (entry 231) was verified: wire every new
  mechanism to an unattended trigger, have each write a **distinct predictable
  integer** into a flow variable, and read the lot back in one shot. `hashMatch`
  in that dump is the check that the ELF's symbol table is the one the editor
  thinks it is.

  Two things that dump makes clear and would otherwise read as bugs: a trigger
  whose exec output is **unwired** has a hit count of 0 (codegen does not
  instrument an empty chain) even though its bool/number outputs work fine; and
  an event's payload trailing the live counter is the **one-frame bus latency**
  showing up as a number, not a lost write.
- **Inspect what went to VU1**: arm a capture (Debugger > VU, or command flag
  bit 3 in `livedbg.cmd`) and the game writes `bin/vucap.bin`;
  `tyrax-editor --dump-vucap <projectDir>` prints the decoded chain (DMA tags,
  VIF codes, UNPACK destinations, the MSCAL entry point) and the first vertices.
  That CLI is how the decoder is verified without the GUI - a healthy capture
  shows `UNPACK V4_32 -> VU1 addr 0` for the scales, referenced `num=21` blocks
  for the double-buffered vertex data, and model-space positions with w=1.0. It
  also lists the **meshes** in the flush (a bag flush carries a dozen; the GUI
  draws one at a time), which **bag flush of the frame** the capture is, the live
  render resolution, and the findings the panel paints amber. **A project with no
  runnable flow-graph node generates no devkit layer at all**, so a scratch
  fixture needs a graph (`--apply-graph`) before it can capture anything -
  `live_debug.gen.cpp` being a 220-byte comment is the tell. Consecutive captures
  of the same draw are byte-identical in LENGTH (same bag, fixed 16 KiB memory
  tail); tell them apart by mtime or the frame in the header, never by size. A
  scripted probe selects the flush the same way the panel does: flags bit 3 arms
  a capture, bit 4 means "index in bits 8-23", and without bit 4 each capture
  walks to the next flush (that is how PROGRESS 201 was verified). **Delete
A stale `CMD_VERSION` in a hand-rolled arming script arms NOTHING, silently:
the game drops a command whose version it does not know
(`if (magic != CMD_MAGIC || version != CMD_VERSION) return;`), so the script
prints "armed" and `--dump-vucap` answers "no capture yet" - on the console
AND in the emulator, which makes it look like the transport. `arm-vucap.py`
was stuck on version 1 against a game at 2 for exactly that long. Check the
constant in `src/livedbg.cpp` before suspecting anything else, and note that
one arming that DOES land sets `vuCapPending` and the tap then refuses every
later one until the write finishes.   `livedbg.cmd` before booting a fixture** - a leftover command is applied at
  boot and eats the first capture, which reads exactly like an off-by-one in the
  walk. If
  you see tags like `refe qwc=32789` or float garbage, the walker lost sync (see
  the by-reference rule in tyra-engine-dev).
- **Symbolize an address from a running/crashed game**: `tyrax-editor
  --symbolize <projectDir> 0x...` runs the container's
  `mips64r5900el-ps2-elf-addr2line` against `bin/<name>.elf.sym`, the unstripped
  copy a DEBUG build keeps (`Makefile.base` writes it when the generated Makefile
  sets `KEEPSYM=1`; the shipped ELF is `strip --strip-all`ed, so a release
  project has nothing to resolve against). Needs the build container up.
- **Testing a crash is harder than it looks**: **PCSX2 cannot produce an EE
  exception at all**, so catching one is a HARDWARE-ONLY test (entry 246).
  Everything tried passed through unharmed there: a signed-overflow `add`, an
  illegal opcode, a write to address 0 (main RAM starts at 0) and a misaligned
  load. On a real console the reliable trigger is one line in a user-owned
  script - `volatile int a = 0x7FFFFFFF, b = 1; asm volatile("add %0, %1, %2" :
  "=r"(r) : "r"(a), "r"(b));` - which raises cause 12 and lands a real
  `bin/crash.txt`; `--symbolize` then names the exact source line. (Installing
  the handler no longer wedges anything - that was `ee_dbg_install(2)`, fixed in
  246.) What IS testable in the emulator: the report format
  (write a synthetic `bin/crash.txt` and let the editor parse + symbolize it),
  the TYRAX error block (a `.flownode` calling `TYRA_SOFT_ERROR` puts a real one
  in the game's log), and the heartbeat post-mortem (kill the game and watch the
  Debugger notice).
- **Prove a release build is devkit-free**: `tyrax-editor --audit-release
  <projectDir>` reads the built ELF and exits 0 (clean) / 1 (something leaked),
  printing text/data/bss so the debug-vs-release cost is a number. Every release
  build also runs it and logs the verdict. **Negative-test it too** - run it
  against a DEBUG ELF and confirm it fails and names the findings, otherwise a
  broken check reads exactly like a clean build (entry 193).
  **It also flags a MEASUREMENT build** (1.123.2) - an opt-in profiling macro
  left switched on, which is the mistake every recipe on this page ends by
  warning about. It scans `.rodata` for the tags those macros own (`FTCLIP`,
  `FTPKT`, `STAPIPRET`, `STAPIPMISS`, `STAPIPBAKE`, `VRAMRES`, `VRAMEVICT`,
  `ROADINDEXVERIFY`, `WHEELBAKE`) and prints them as `measurement build -
  .rodata`. Negative-test THIS half too, and it is cheap: build the same
  project with `TYRA_WHEEL_REBUILD_REPORT=1` (generated game, no engine
  rebuild) or `TYRA_FRAME_PROFILE=1` (engine) and the finding must appear and
  then disappear again. **Add the tag when you add a gate** - a macro whose
  log line is not in that list ships silently.
- **Live Logic patches without the GUI**: `livelogic.cpp` has no GUI
  dependency, so a ~100-line host harness (link it against the editor's
  `build/CMakeFiles/tyrax-editor.dir/src/*.obj` minus `app`/`viewport`/
  `gl_loader`/`main`, plus `vendor/ufbx/ufbx.c.obj` and your own
  `STB_IMAGE_IMPLEMENTATION` TU) can `project::load` a project, edit graph
  params in memory, `livelogic::compile` + `encode` and write
  `bin/livelogic.bin` - i.e. do exactly what `App::liveLogicTick` does. Combined
  with the Live Debugger's telemetry that gives a fully scripted end-to-end
  check of hot-patched logic: measure a node's fire RATE before and after the
  patch (entry 192). **When a patch half-works** (the visible effect lands but
  debug keys/state look wrong), suspect the wire STRIDE first, and check that
  `build/tyrax-editor.exe` was rebuilt before the `--build` that generated the
  game's parser - a stale editor binary generates a stale interpreter.
- **The log panels' severity split** (docs/log-panels.md): `logview.cpp` is a
  pure function of text - no ImGui, no `Project` - so which bucket a line lands
  in is checkable from a 40-line harness (`g++ -std=c++20 -Isrc harness.cpp
  src/logview.cpp`) fed a realistic build log, no editor needed. Assert TWO
  things there, because both broke while it was written: that the counts are per
  ENTRY (a gcc error plus its snippet is one error, not four), and that parsing
  the log **one line at a time** through `parse(log, from, state, out)` +
  `appendPartial` gives byte-identical results to one-shot parsing - the panels
  classify incrementally while a build streams, and the carried continuation
  state is what a chunk boundary breaks. The panel itself is then a `--ui-script`
  job (`click 'Output/3 errors'`, `expect-unchecked 'Output/Select text'`), with
  one catch: the **Debug** window is a dock TAB behind Output in every built-in
  layout, and a docked tab is an unnamed item, so no script can select it -
  verify it by temporarily pointing that layout's `pendingFocusWindow_` at
  `"Debug"`, screenshotting, and reverting.
- **The editor window sometimes renders WHITE on this machine** (title bar
  only, capture is blank) - a GL present quirk, not a code regression. The
  check that settles it costs 30 seconds: capture
  `D:	yra-editoruild	yrax-editor.exe` (the main-repo baseline binary)
  the same way. If that is blank too, GUI visual verification is unavailable
  for this session: say so in PROGRESS rather than claiming a visual check
  that did not happen.
- **Audio**: the *editor's* own audio (the Drone Generator's audition,
  `src/audiopreview.cpp`) is testable directly — a host harness can open the
  device and print peak levels (PROGRESS 210), and the generator's DSP needs no
  device at all: link `dronegen.cpp` alone, render a preset and measure the
  samples (automation included — PROGRESS 211). For the GAME's audio, EE-side
  logs are invisible, so meter the PCSX2 process instead —
  on Windows the WASAPI session peak meter (e.g. via `AudioMeterInformation`),
  on Linux `pactl list sink-inputs` (the PCSX2 sink input's volume/peak, or a
  short `parec` capture of the monitor source). Silence vs bursts at expected
  times proved music/sfx features before; a by-ear speaker check stays with the
  human.
- **Two-player modes** (docs/multiplayer.md): the split/shared toggle is
  testable with no controller at all through the Remote Pad - give the scene two
  Player objects and a pause menu with the "Player count" option block, then
  `--pad <dir> "press start; wait 0.5; press cross"` and screenshot: the frame
  visibly flips between full-screen and the top/bottom split (or the pulled-back
  shared camera). **Pad-2 hot-join is now scriptable too** (`pad 2; press
  start`) - it no longer needs a second physical pad in PCSX2's Pad2 slot, since
  the overlay is applied to the game's own second connector.
- **Flow-graph / gameplay logic**: an `On Button` / `On Action` trigger is
  reachable unattended now (`--pad <dir> "press cross"`), so prefer that over
  rewiring the behavior to `On Start` / `Every N Seconds` just to test it. What
  still needs a human: mouse FEEL, analog ramps judged by eye, and the editor's
  own on-screen pad being CLICKED (the panel writes through the same
  `livepad::write` the CLI does, but a synthetic click into the editor is its own
  problem) - say so in the commit message, that's the established convention.

### The unattended input test, end to end

The whole recipe, on Windows, with nothing focused and no human. Every step is
described above; this is the order that works, and the shape a PROGRESS entry
about a pad-driven feature should be able to quote.

```powershell
$P = "$env:TEMP\tyra-editor-test\padtest"      # short path - PCSX2 needs it
$S = "<scratchpad>"
build\tyrax-editor.exe --new padtest "$env:TEMP\tyra-editor-test" 100 100 fpp
build\tyrax-editor.exe --build $P --run        # boot it
Start-Sleep 22                                 # Tyra logo + splash + scene load
$shot = ".claude\skills\tyra-testing\scripts\screenshot-window.ps1"
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\idle1.png"
Start-Sleep 3
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\idle2.png"   # CONTROL
build\tyrax-editor.exe --pad $P "stick r 110 0; wait 1.5; neutral"
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\turned.png"
build\tyrax-editor.exe --pad $P "stick l 0 -127; wait 2.5; neutral"
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\walked.png"
Start-Sleep 4
powershell -File $shot -ProcessName pcsx2-qt -OutFile "$S\idle3.png"   # RELEASED
```

The cheap version of the same run is one `-Watch` (above) over the whole drive:
it counts the pixels for you, per frame, and hands back one sheet instead of
five images — the four bullets below still apply, they are just columns then.

Then count changed pixels between the pairs with a few lines of PIL
(`ImageChops.difference(...).get_flattened_data()`). Four things make this a real
test rather than a screenshot:

- **the idle pair is the control.** Without it "the picture changed" proves
  nothing. Measured on this fixture: 3 s of idle changes ~600 px and every one of
  them is inside PCSX2's own status bar (the FPS/EE numbers) - so anything in the
  hundreds of thousands is unambiguously the game.
- **turn before you walk.** A straight walk over the flat checkerboard maps the
  repeating pattern onto itself and can change almost nothing; the yaw turn is
  the loud signal (~197k px here, vs ~1.4M for a 2.5 s walk after it).
- **the trailing idle shot proves the RELEASE.** Back to status-bar-only
  (~1.6k px) is what shows the pad was let go rather than the input having simply
  stopped arriving - the one failure mode a "did it move?" test cannot see.
- **crop the status bar out** if you want a cleaner number: it is the only thing
  moving in an idle frame, so its rows are pure noise for every comparison.

### Recording a run and proving it reproduces (docs/input-replay.md)

The pixel-diff recipe above answers "did the game react". This answers the
harder one - "did it do **the same thing** as last time" - and it needs no
screenshots at all, because the game checks itself and the exit code is the
verdict. Requires the debug profile and `"inputRecorder": true` in the `.tyra`.

```bash
# Record. --pad takes the same script the --pad command does; --seconds is a
# FLOOR, so idle frames after the script still get recorded.
tyrax-editor --record $P recordings/smoke.tyrarep \
    --pad "wait 6; stick l 0 -127; wait 3; press cross; wait 1; neutral" --seconds 14

# Perform it again. 0 = reproduced exactly, 3 = diverged, 1 = could not run.
tyrax-editor --replay $P recordings/smoke.tyrarep ; echo "exit=$?"
```

`--replay`'s exit code is the whole point: a recording is a regression test for
a **play session**, which is the thing `--pad` alone could never be (it can
drive a game, but nothing afterwards could say whether the game did the same
thing). The game logs its own verdict, and every line it prints is prefixed
`Replay:` - one anchor for a grep over `bin/log.txt`:

```text
Replay: finished 705 frames, 0 divergences
Replay: finished 705 frames, 304 divergences (first at frame 400)
Replay: diverged at frame 400: pos (0 1.8 10.301) yaw 0 pitch 0, expected (0 1.8 10.5) yaw 0 pitch 0
```

For staged PCSX2 captures, `TYRA_LOG` replay verdicts can be in the owned
stage's `game/log.txt`, while `pcsx2-capture.py --wait-log` searches emulog.
A capture exit code or timeout screenshot alone does not prove replay completion.
Require the actual terminal frame count and zero divergences in the game log,
matching selected ELF/recording hashes, and the recorded semantic event sequence.

The host replay reader can recover a valid prefix and return success with
`Recording.truncated=true`. Full canonical-recording acceptance additionally
requires `truncated=false`, parsed frames equal the declared count, a finalized
terminal/footer, valid CRC and no trailing bytes. Keep recovered prefixes and
rejected/tampered recordings separate from complete replay evidence.

Three checks make this a real test rather than a smoke test, and all three were
run on the branch that added it:

- **Read the recording back before trusting the replay.** A file of 705
  all-neutral frames replays perfectly and proves nothing. `--replay-dump
  <file.tyrarep>` prints the timeline without running anything - every button
  press and every jump in the fingerprint too big to have been walked, frame
  numbers on both - and with a frame range (`--replay-dump f a b`) the raw
  per-frame view, sticks included. Read it against the `Portal:` / `Pick:`
  lines the game writes into `bin/log.txt` (docs/devkit.md): that pairing is
  what turns "it sometimes throws me across the room" into "the grab is frame
  390 and frames 391-407 move 0.75 of a unit each with the stick CENTRED".
  Recordings made since 1.85.0 carry those events THEMSELVES, so the dump
  prints "grabbed object 38" beside the press with no emulator involved, and a
  replay reports a missing one by name instead of as a position delta.
  Assert the stick really left centre, a button was really held,
  and the fingerprints really move. On the FPP fixture: 208 frames with the
  stick off-centre, 7 with a button, 669 of 705 carrying a fingerprint (the first
  ~36 are boot, where scripts have not started), player z 0 -> 20.8, and `dt`
  ranging 0.020..0.080 - the loading hitches, recorded.
- **Tamper with it.** Change ONE frame's axis byte and re-encode (so the CRC
  stays valid - the test is "different input, different run", not "damaged file
  refused"). It must come back `exit=3` and name that exact frame. Measured:
  changing frame 400's left-stick vertical gave `first at frame 400`.
- **Fight it with the Remote Pad.** Run `--pad "hold left; wait 8"` from a
  SECOND process against a replaying game. It must still report 0 divergences -
  that is what proves `Pad::setState`'s overwrite beats `injectVirtual`'s
  overlay, and it is the one property a single-process test cannot see.

A fourth is worth running on any project with a runtime procedural volume set to
*a new world every run*: the seed is the one non-deterministic number the game
asks for, so grep `bin/log.txt` for `Procedural <name>: N instances, seed S`
after both runs and check S is identical. On `examples/cube`: 27 instances, seed
329243691, both times.

### The motion gate: does the picture survive being MOVED?

**Every check above this line freezes the camera.** That is what makes them
reproducible, and it is exactly what makes them blind. Four defects on the
upscaler branch reached the owner because no harness could see them:

| defect | why every gate missed it |
|---|---|
| the jitter shake | the sampler used an **even frame stride**, so every capture landed on the same jitter phase and it reported a perfectly still picture |
| terrain streaking at grazing angles | nothing ever *looked* at a moving ground plane |
| BLSS x frame extrapolation tearing | **parked, all four arms are indistinguishable**; it exists only in motion |
| black frames with triple buffering | found because a human happened to watch an agent's emulator |

**A frozen fixture buys repeatability at the price of a whole class of faults.**
The motion gate is the other half:

```powershell
# one arm = one four-leg run of the fixed route
powershell -File .claude\skills\tyra-testing\scripts\motion-gate.ps1 `
    -Project $env:TEMP\tyra-editor-test\mgate -Out <scratch>\armB -NoAnalyse
# then compare it against the arm with ONE knob changed
python .claude\skills\tyra-testing\scripts\motion-gate.py <scratch>\armB `
    --baseline <scratch>\armC --bands 8
```

`motion-gate.ps1` captures; `motion-gate.py` decides (and is the cross-platform
half - point it at any burst directory, so a Linux capture can feed it). Exit 0
= nothing flagged, 1 = something flagged, 2 = the burst is not usable. A
four-leg run is ~25 s of capture and ~1.5 GB of raw frames; analysing two arms
is ~3 min.

#### The fixture

`scripts/routecam.cpp` is the route: a **global script** (`TYRA_SCRIPT`, no
attachment) that drives `ctx.cameraOverride`/`cameraEye`/`cameraAt` from its own
**frame index**, in four 200-frame legs that close into a loop -

| leg | what it is | what it is for |
|---|---|---|
| `hold` | parked at one pose | the parked stability gate, as a leg: the noise floor, and the one place where "the picture moved" needs no argument |
| `pan` | 50 deg of yaw | a near-rigid lateral move - the cleanest case for the residual |
| `dolly` | 6 units forward, camera dropping 1.6 -> 0.9 | textured ground at a **grazing angle**, and a zoom that has no global translation at all |
| `return` | the way back | reverse dolly plus reverse pan |

Copy it into a fixture project's `src/scripts/`, fix the namespace, and set:
**`displayMode: progressive`** (interlaced alternates fields, which is a
period-2 signal by construction), **`showFps`/`showMemory`/`showProfiler` off**
(a live frame counter inside the picture is pure noise), **`liveDebug` on** (the
gate reads the game's frame counter out of `bin/livedbg.bin`), and the emitters
left **RUNNING** - one of the four faults lives in exactly that combination.
`examples/upscaler-lab` copied to a short path is the fixture this was built and
measured on.

#### Determinism: a frame-indexed route, and the pad as a ONE-SHOT trigger

The Remote Pad refreshes at 25 Hz off the **host** wall clock, so a stick lands
at a different frame offset in every run - which is why every measurement on
this branch used a frame-indexed script camera instead. This gate does the same:
the route is a pure function of the script's frame index, so two runs traverse
identical content. **The pad's only job is to zero that index once.** A one-shot
event's arrival jitter shifts the whole route equally instead of perturbing each
capture, and if the press never lands the route still runs - only its phase is
unknown. Three things fell out of building it:

- **`getClicked()` did not reach a global script from `--pad`.** Measured: a
  `press cross` reset nothing and three runs landed at three different route
  phases; the same script reading **`getPressed()` with its own edge** resyncs
  every time. `livepad::tick` raises `clicked` inside the frame it polls, so
  anything reading it on the wrong side of that call never sees a remote press
  at all. Use `getPressed()` and find the edge yourself.
- **Schedule the legs in GAME FRAMES, not seconds.** The emulator does not run
  at 100 % and its rate is not constant: this fixture measured 50-61 fps across
  its own four legs, and **28.8 fps with frame extrapolation on** (the world
  runs at half rate by design). Wall-clock scheduling walks the burst out of the
  leg it belongs to within one loop; polling `livedbg.bin`'s frame counter
  between bursts does not, and costs nothing.
- **A parked leg whose picture is bobbing measures as "moving".** Take
  parked-vs-moving from the ROUTE (the leg's name), never from the measurement,
  or the gate switches to the moving-leg statistic and discards the one finding
  that needed no argument at all.

#### The statistic, which is the hard part

Particles on `upscaler-lab` change more between frames than the artefact the
parked gate hunts, and a walking camera moves every pixel. "% of pixels that
changed" is not a signal here, it is a description of the route. So the gate
does not measure the difference between two captures - it measures **the part of
that difference that a rigid move of the whole picture cannot explain**:

```
d       global displacement, by phase correlation (integer + sub-pixel)
raw     mean |A - B|                      <- dominated by the route
mc      mean |shift(A, round(d)) - B|     <- what the move did not explain
mc/raw  the unexplained FRACTION - scale-free, so it does not care how much
        time passed or how far the camera went
```

Legitimate motion lands in `d`. Parallax, disocclusion and the emitters put a
floor under `mc`, and that floor grows with the amount of change, which is what
dividing by `raw` removes. Each defect is then something a rigid move cannot
express - and each has its own column:

- **a bob** is a displacement on top of the route. On the `hold` leg it is the
  ENTIRE displacement, reported in pixels, needing no threshold argument. `mc`
  compensates only the INTEGER part on purpose, so a sub-pixel bob stays in the
  residual rather than being absorbed by the estimator meant to find it.
- **a tear** is two displacements in one picture. Correlate horizontal BANDS and
  look for a **step**, not for disagreement: forward motion makes every band
  disagree with the whole frame (measured: dy of 0,0,0,0,+3,+6 sky to ground,
  every frame of the dolly leg), so the statistic is the largest adjacent-band
  jump *in excess of* the typical one. A band votes only when its own
  correlation is at least 0.4 as confident as the whole frame's - the good bands
  of a panning frame score 0.65-0.85 and the two that reported a bogus 30 px
  scored 0.26-0.33, so an absolute floor does not separate them.
- **a black frame** is a luma collapse. No statistic; just say it.
- **streaking** is spatial, so every residual is reported PER BAND as well:
  "the ground band is 6x the sky band" is the shape that artefact makes.
- **something that stopped being DRAWN** is a per-tile question, and it is the
  one that hands over a diagnosis. When a hardcoded `ZBUF` mask made a
  full-screen pass stamp depth through the texture heap, the tell was not the
  missing terrain everyone was looking at - it was that the **crosshair** had
  gone too, in an arm whose terrain was untextured. On the `hold` leg both arms
  are parked at the SAME pose, so their per-pixel MEDIAN frames (the median
  removes the particles) compare tile by tile with no alignment and no
  statistics: a tile that changed while its local variance collapsed **stopped
  being drawn**; a tile that changed and kept its detail merely looks different,
  and is reported as the much weaker thing it is.

**Runs are compared as DISTRIBUTIONS over the same route, per leg** - never
capture k against capture k. The captures are not frame-locked to anything (see
below), so a paired comparison would report the sampler's own phase as a
finding. **Change ONE knob between the arms** and the route, the emitters and
the reconstruction are common mode; a verdict the baseline also produces on the
same leg is dropped rather than reported.

#### Rules this branch paid for, and that the scripts encode

- **CONSECUTIVE CAPTURES, NEVER A STRIDE.** The capture loop sleeps for nothing.
  It reports the interval it achieved (measured: **48-59 Hz** through
  `PrintWindow` against a 50-60 fps game, i.e. ~1.0-1.3 game frames per capture)
  and **warns when that lands within 0.04 of a whole number of frames** - an
  even stride samples one phase of a period-2 artefact forever, which is exactly
  how the shake survived its first harness.
- **A ROUTE SAMPLED BY `--capture-frame` NEEDS A LENGTH COPRIME WITH THE
  ADVANCE.** The capture path freezes the game for a fixed spell, so the game
  advances almost exactly the same number of frames between two
  `--capture-frame` calls. Give a frame-indexed camera route a round length and
  the two are commensurate: every run walks the same handful of poses forever.
  Measured on a console A/B of `examples/showcase` - a 1920-frame route sampled
  eight distinct poses, and three DIFFERENT builds produced digit-identical
  score sequences, which reads exactly like "the change did nothing". A prime
  route length (1913 was used) walks the whole route instead, and because the
  advance is deterministic every arm still walks it in the SAME order - which
  is what makes two builds comparable frame by frame. Confirm the alignment
  rather than assuming it: search a small offset window for the minimum mean
  |diff| and check it lands on 0.
- **`-PrintWindow` by default.** A GDI `CopyFromScreen` reads the SCREEN, so an
  occluded window captures whatever is physically in front of it - that once
  grabbed the owner's browser instead of the emulator and produced a perfectly
  plausible table. PrintWindow reads the window's own content, raises nothing
  and steals no focus. **Select the emulator by the project on its command
  line** (`-Project` does it; `-ProcessName` takes the first of several
  worktrees' emulators).
- **INSTRUMENT OUTSIDE THE LOOP YOU ARE PERTURBING.** The game's frame counter
  is read from `bin/livedbg.bin` once before a burst and once after, never per
  capture, and the per-capture index is interpolated and labelled as an
  estimate. Adding 24 per-frame log lines inside the boot-banner loop moved the
  black-frame defect's trigger and made it vanish; 45 HostFs reads a second is
  the same kind of poking.
- **`Start-Process -ArgumentList` does not quote its array elements**, so a
  `;`-separated pad script arrives as loose argv and the driver dies on a stderr
  nobody reads - which looks exactly like a dead pad channel. The `--pad` call
  lives inside the `.ps1`, and **its stderr and exit code are read out loud**.
- **A frame missing geometry entirely is a different question from a frame
  drawing it wrong.** The gate writes the two frames of its worst capture plus
  the 8x-amplified unexplained residual into `<leg>/look/`, because that
  distinction discarded two wrong theories in an hour and only a picture answers
  it.
- **Take the crop ONCE** and never `-Trim`: a crop that follows the content
  re-registers a picture that slid by a line into an identical image. The crop
  is found from the first frame of the first arm and reused for both (`--box`),
  and it needs a *coverage* rule rather than a bounding box - PCSX2's own FPS
  readout sits in the letterbox, and one line of thin white text drags a plain
  bounding box back over the black bars.

#### What it caught, and what it could not (2026-08-11)

**The acceptance test is retrospective**: a gate that cannot flag a defect we
already understand is not a gate, and saying so is a better outcome than tuning
a threshold until something trips. Fixture `examples/upscaler-lab` copied to
`%TEMP%\tyra-editor-test\mgate`, PCSX2 software renderer, progressive 480p,
emitters running, 110 captures per leg. **Arm C is the reference** (BLSS on,
`blssJitter` off, no extrapolation - the shipped default) and every other arm is
**one knob** away from it.

| arm | leg | measurement | verdict |
|---|---|---|---|
| **C** reference | hold | move **0.00 px**, raw **0.120/255** | the noise floor, with the emitters running |
| | pan | move 4.12 px, mc/raw 0.514 | - |
| | dolly | no global shift at all; band mc ramps 0.22 -> 14.7 sky to ground | the zoom signature |
| | (own flag) | PERIOD-2 on `pan`, 74/34 at 0.121 | present in EVERY arm, so dropped from every comparison |
| **B** `blssJitter` **on** | hold | **5.34 px** of displacement per capture pair on a PARKED camera; mc **43.2x** the reference | `PICTURE MOVES`, `WORSE WHEN PARKED` |
| | pan | **2.41x** the reference's unexplained residual | `WORSE IN MOTION` |
| | dolly / return | mc/raw splits **53/56 at 12.1x** and **55/54 at 7.2x** | `PERIOD-2`, new against the reference |
| **D** BLSS **x** frame extrapolation | (whole run) | the world rate halves to **28.8 fps** by design | frame-scheduled legs absorbed it with no change |
| | hold | 0.24 px on a parked camera; mc **26.7x** the reference | `PICTURE MOVES`, `WORSE WHEN PARKED` |
| | return | **2.24x** in motion; band mc **0.37 sky -> 29.6 ground, an 80x ratio** | `WORSE IN MOTION` - and that ratio IS the grazing-angle ground artefact, localised |
| | dolly | mc/raw splits **50/59 at 5.3x** | `PERIOD-2`, new against the reference |

**Run to run it repeats**: a second, independent capture of arm D on the same
build read **26.93x** against the same reference where the first read 26.66x,
and 0.23 px of parked displacement against 0.24. That is the number that says
the sampler's own phase is not in the answer.

Three things in that table are worth more than the numbers:

- **The jitter shake is caught on the parked leg AND in motion**, which is the
  whole point - the parked gate could only ever see the first.
- **BLSS x extrapolation separates even PARKED, at 26.7x** - and the reason is
  the fixture rule everything else fought: the **emitters are running**. Every
  other frame is synthesised and a synthesised frame freezes the particles, so
  the arm that froze the emitters for repeatability is exactly the arm in which
  all four configurations look identical. Extrapolation is also a period-2
  process by construction (real, synthetic, real, synthetic), which is why the
  clustering comes out perfectly balanced.
- **The gate flags the reference arm's own `pan` leg** (74/34 at 0.121, 4.0x).
  That is honest and it is not the upscaler being exonerated: it is either the
  24 fps model animation stepping under a 60 fps camera or BLSS' temporal pass
  under motion, and this instrument cannot tell those apart. It is reported as
  the baseline's own flag and subtracted from every comparison, which is what
  keeps it from being read as a finding about the arm under test.

**Forcing the refused combination.** BLSS x frame extrapolation is a build-time
`#error`, and the whole refusal lives in one generated TU:
`--refresh-gen` writes `src/gen/blss_interlock.gen.cpp` (and deletes it the
moment the clash is gone). So a scratch fixture gets past it by **deleting that
file and then building in the container by hand** - `--build` would regenerate
it:

```powershell
tyrax-editor --refresh-gen $P            # prints "[blss] BUILD WILL BE REFUSED"
Remove-Item "$P\src\gen\blss_interlock.gen.cpp"
docker compose --project-directory $P -f "$P\docker-compose.yml" up -d
docker compose ... exec -T compiler sh -c "rsync -a --delete --exclude=.git --exclude=obj --exclude=bin /host/ /src/"
docker compose ... exec -T compiler sh -c 'cd /src && make -j$(nproc)'
docker compose ... exec -T compiler sh -c "rsync -ac --include=*/ --include=bin/** --exclude=* /src/ /host/"
```

**What it could NOT do.**

- **The black-frame / triple-buffering defect was not reproduced.** Its fix is
  three hunks inside `vendor/tyra`, and reverting them was out of this change's
  scope. The DETECTOR is verified instead, against a real burst with three
  captures blacked out: it named all three by capture index, route leg, time,
  game frame and file. That is a check of the test and its reporting, not of the
  defect - and the difference matters. What says the capture path would see the
  real thing is the fix's own diagnosis: three of eight consecutive PrintWindow
  captures came back black.
- **"What stopped being drawn" refuses to answer on a bobbing leg**, by design.
  A shaking picture smears its own median, local variance falls everywhere, and
  the tile test then reports the whole textured half of the frame as gone - 41
  tiles on the jitter arm, none of which stopped being drawn. So it prints
  `NOT ASKED` above 0.5 px of bob. Fix the bob, then ask.
  Even below it, read the tile list as a SHAPE rather than a count: a handful of
  scattered tiles in the sky and at the edges is the translucent particle field
  reconstructing differently between two arms (three GONE and three NEW on the
  extrapolation arm, all of them haze), while the case this exists for - the
  terrain, or a crosshair - is a contiguous block or a lone tile that holds a
  sprite. `--tiles-x/--tiles-y` set the grain; 24x18 is ~40 px here.
- **It cannot separate "the scene legitimately animated" from "the
  reconstruction hiccupped"** inside one arm - see the reference arm's own `pan`
  flag. The arm comparison is what makes a finding, and that needs a second
  build.
- **PCSX2 only.** Admissible for correctness (which is all this measures);
  never quote a GS-fill or per-function number from it.

### Measuring a console-only flicker: the failure-rate fixture

Some corruption exists only on a real PS2 and only in SOME frames of a static
scene (the 1.81.1 slot-pool race: a lamp's corona as a sliver in 4-19 of 30
frames, never in PCSX2). A single `--capture-frame` per build is then a coin
toss dressed as a verdict, and two builds photographed once each will "differ"
for no reason. The fixture that works:

1. **Pin the run**: an input replay (`bin/replay.in`, docs/input-replay.md)
   pins `dt`, and a tiny script parks the camera on a fixed pose from a frame
   number on (`examples/showcase`'s `vantagecam.cpp` shape: pose A until frame
   1400, pose B for ever after). The same pose then renders for the rest of
   the run, so every capture is of the same picture.
2. **Sample, don't shoot**: wait for `--debug-state` to report a frame past the
   park, then take 24-30 free-running `--capture-frame`s a few seconds apart.
3. **Score each against ONE reference** of that pose (a build known good, or
   PCSX2) with the HUD rows masked (`d[:70,:] = 0`; the FPS/MEM text differs
   between builds), and report `pixels > 40` per sample. The number is the
   **rate**, and the distinct values tell you how many interleavings there
   are (a race gives a small discrete set; garbage gives all different).
4. **Bisect with barriers, not theories**: an arm per synchronisation point
   (GS FINISH after every bag, a VIF1 FLUSH packet the EE waits for, a FLUSH
   in the VIF stream only, one wait at one suspected site), 24+ samples each,
   read the FPS off the HUD too. In 1.81.1 only the arms where the EE waited
   fixed it - that single fact located the writer.

Three traps: the fixture compares CONTENT, so both arms must be the same
project directory (two scratch copies with a moved object "differed" by exactly
one "PICK UP" prompt and cost a day); `--capture-frame` resumes a halted game,
so never mix it with halt/step walking inside one sample series; and
`--debug-state` reads `bin/livedbg.bin`, which the PREVIOUS run left behind at
its last frame - delete it before launching or the "past the park" wait
returns at once and the series photographs the moving camera.

### The shadow A/B rig: one command per switch

Dynamic shadows (docs/shadows.md, docs/flashlight.md) are the case the layers
above are awkward for: the change is a **switch**, the evidence is **where the
picture got darker**, and the honest test is the same authored frame rendered
twice with one setting moved. Two scripts make that one command.

```powershell
# 1. the fixture, once - headless, no Docker, SHORT path
powershell -File .claude\skills\tyra-testing\scripts\make-shadow-fixture.ps1 `
    -Editor build-dev\tyrax-editor.exe -Force

# 2. the A/B, per switch
powershell -File .claude\skills\tyra-testing\scripts\shadow-ab.ps1 `
    -Editor build-dev\tyrax-editor.exe `
    -Project $env:TEMP\tyra-editor-test\spotab `
    -Vantages $env:TEMP\tyra-editor-test\spotab\vantages.json `
    -Toggle spotShadowVolumes -Values true,false `
    -OutDir <scratchpad>\spotab
```

`-Toggle` is any key the manifest writes on a line of its own —
`spotShadowVolumes`, `flashShadowVolumes`, `blobShadows` — and it is **inserted**
when the file does not carry it, which every project that never touched the
setting does not. **`bakedShadows` is the one toggle that needs a step in
between** (docs/shadows.md): it reads a CACHE, so patching the key alone
switches a feature the project has no bake for. Run
`tyrax-editor --bake-shadows <project>` once with the key true, then A/B it —
the cache is content-hashed and survives the toggle going false and true again,
so one bake serves every row of the run. For each (value x vantage) the rig patches the setting and the
Player's pose, runs `--build --run` under a hard timeout, waits `-Settle`
(14 s), screenshots **the emulator whose command line names this project**,
greps the game's own `bin/log.txt` for `Assertion` / `=======` banners, and
writes `report.md` with a per-row table plus a delta table. Build logs and a
copy of each boot's `log.txt` land beside the screenshots.

**The fixture** (`%TEMP%\tyra-editor-test\spotab`, from the `fpp` template):
flat 100x100 terrain, **two** lamp+caster+wall groups 20 u apart — a spot light
4 u up (half-angle 30, radius 12, `"shadowVolumes": 0` so the PROJECT switch is
what decides), a 1.5 u box 0.9 u off the cone axis, an 8x4x1 wall 3 u behind it
— and a frozen Player with the flashlight on. Two groups because the
interesting failure of a per-light feature is not "does one lamp work" but
whether the second one starves: the count band is one buffer for the whole
frame. `vantages.json` frames lamp 1 from 8 u, **both** groups from 20 u back
midway between them (from 8 u they are 51 degrees off the axis and a 60-degree
frame holds neither), and lamp 2 from 8 u.

Five things it had to get right, each of which produced a confident, wrong
answer first:

- **A torch that sits on the eye casts no visible shadow at all.** The camera
  occludes exactly what the light does, so every shadow hides behind the thing
  casting it and both switch positions photograph identically — measured, with
  the volumes demonstrably working: **d centre 0.004, d lower third 0.000**. The
  fixture drops the torch by the clamp's full metre (`offsetDown`) and 0.6 m to
  the side, which puts the caster's shadow on the wall as a band ABOVE its own
  silhouette. Same trap for a spot: a light co-located with the camera is not a
  test.
- **A new project applies an AMBIENCE PRESET on top of its settings.** `--new`
  ships one ("Default", blue sky, ambient 0.55) with `defaultAmbience: 0`, so a
  manifest that says `ambient: 0.1` still renders a sunny afternoon and every
  shadow is washed out. The fixture sets `defaultAmbience: -1`.
- **`-Trim` cannot be used, and neither can a plain content bounding box.** The
  crop is found from the FIRST capture and reused for every one after it (the
  motion gate's rule: a crop that follows the content re-registers two different
  pictures into the same rectangle). Finding it needs a **coverage** rule rather
  than a bounding box — PCSX2 paints its own `FPS 58.35 [P]` overlay inside the
  black letterbox, and that one line of thin text stretched the measured picture
  from 959x691 to 959x829, which put the FPS-strip region on pure black.
- **`powershell -File` does not split `-Values true,false`**, so the rig gets one
  string, writes `"key": true,false,` into the manifest, and the build fails with
  `spotab.tyra is malformed` — which points at nothing. It splits on commas
  itself now.
- **A region metric reports 0.0000 for "nothing changed" AND for "the subject
  was not in my rectangle."** The `between` vantage frames two lamp groups, so
  both sit at the EDGES: its centre region read exactly 0.0000 for a switch the
  whole frame scored 0.00005 on, against a 0.00000 control. There is a
  whole-frame column for that reason — read it first, then the regions to say
  *where*.

**Read the numbers, not the pictures.** Each row reports the mean luma (0..1) of
the **whole frame**, of the **centre region** (the caster and the wall behind
it) and of the **lower third** (the ground), plus the **ink coverage of the FPS
strip** — which is a liveness check and NOT a reading of the counter: a black
window measures as a beautiful shadow everywhere, and 0.000 ink is what catches
it.

**Pass the same value twice for the noise floor.** `-Values true,true` boots one
configuration twice; the delta table is indexed by variant, so the second row is
what two boots of an unchanged project differ by. A delta smaller than that is
not a finding. Measured on this fixture (frozen camera, `progressive` display,
`--pad` never touched) the noise floor is **exactly 0.00000** — nine boots
across three vantages produced byte-identical means for the two `false` arms, on
every region. That zero is what makes a 0.0134 mean something.

**Pass ONE value and a line of vantages and the same rig is a WALK** — which is
how you answer "from where does this shadow stop being drawn at all", a question
no A/B of a switch can reach. The vantages are then not three views of one
subject but one subject seen from a sequence of standpoints, and the column to
read is `centre mean` against the vantage name. That is how the projected-shadow
slot flicker (1.70.1) was found: `examples/night-walk` copied to a short path,
its twelve `projShadow` casters left alone, the Player walked sideways past the
shed at x = 0, 4, 6, 7, 7.5, 8, 9, 10 with each pose aimed at the shed's base,
`-Toggle showFps -Values true` (a no-op key set to what it already was, so the
rig runs exactly one arm). The shed's ground shadow is a hard-edged black quad
through x = 7.5 and entirely gone at x = 8.0 — frame mean 0.10289 → 0.11360,
centre 0.187 → 0.216, the picture getting BRIGHTER as the shadow left. Half a
unit sideways, and the numbers say which half-unit. Two things that made it
work: aim every vantage at the same world point (compute the pitch and yaw per
standpoint rather than reusing one rotation, or the subject walks out of the
measured region and a real change reads as a confident 0.0000), and RENAME the
scratch copy's project, because a Docker container is named after the project
and the owner may be building the original in their own checkout at the same
time (renaming means fixing the namespace in `src/scripts/*.cpp` too, or the
Runner's pre-flight refuses the build).

**Everything that can reach Docker is bounded** and killed with its children on
timeout, because a `docker` call on this machine blocks forever instead of
failing. And nothing is ever killed by name: the emulator the rig captures and
closes is the one whose `-elf` names the project, the same discriminator
`Runner::killEmulatorsFor` uses — a parallel worktree's PCSX2 is neither
captured nor reaped.

**Windows only so far**, like `motion-gate.ps1`: the capture is
`screenshot-window.ps1`, the measurement is System.Drawing and the emulator is
found through `Win32_Process`. Nothing in the design is platform-specific — a
Linux twin swaps the first for `wayland-control.py shot --area`, the second for
ten lines of PIL and the third for `/proc/<pid>/cmdline`. Say which OS a
reported number came from.

#### Proven on the flashlight (2026-08-21)

The spot runtime does not exist yet, so the rig was proven on
`flashShadowVolumes`, whose volumes have worked since 1.62.0. Nine boots
(`-Values true,false,false` — the repeated value is the control), three
vantages, PCSX2 software renderer, 959x691 picture, **all nine logs clean**:

| vantage | frame mean, `true` -> `false` | d frame | d centre | control (arm 2 - arm 1) |
|---|---|---|---|---|
| lamp1-8u | 0.14608 -> 0.14846 | **+0.00238** | **+0.0134** | 0.00000 |
| between  | 0.11111 -> 0.11115 | +0.00005 | 0.0000 | 0.00000 |
| lamp2-8u | 0.13705 -> 0.14004 | **+0.00299** | **+0.0173** | 0.00000 |

Turning the volumes OFF makes the picture BRIGHTER, which is the right sign:
the shadow is what was removed. The control column is the load-bearing one —
the two `false` arms came back byte-identical on every region of every vantage,
so the fixture is deterministic to the digit and the deltas are the switch and
nothing else. `between` is the honest 0: at 20 u the torch barely reaches those
casters, and that vantage exists for the SPOT test, where each lamp lights its
own group regardless of where the camera stands.

The screenshots show it as plainly as the numbers do: with the switch on, the
box lays a hard-edged rectangle across the wall and a wedge across the ground;
with it off, the wall is evenly lit and there is no shadow anywhere.

**`-Toggle spotShadowVolumes` is already wired end to end on the same fixture**,
even without a runtime: flipping it and running `--refresh-gen` moves
`SPOT_SHADOW_VOLUMES_USED` in `inc/scene_data.hpp` between `false` and `true`
(the two lamps resolve the predicate). So when the runtime lands, the rig is a
one-liner away from a number — see docs/backlog.md, "Run the shadow A/B rig
against the spot runtime".

## Verifying the AI Assistant (docs/ai-chat.md)

An AI feature looks untestable and is not: three of its four layers need no
backend at all, and the fourth needs no tokens.

**0. Is the documentation searchable?** `TYRAX --search-docs "<query>" [page]` runs
the assistant's `search_docs` over the pages baked into the exe and exits 1 when
nothing matches - so a doc-facing change ("does anything still say
`--max-turns`?") is one command, no project and no backend.

**1. What is it told?** `TYRAX --chat-prompt [projectDir]` prints the whole system
prompt - the tool catalog built from `aichat::tools()`, the documentation index
derived from `docs/*.md`, the object type/property tables and the live project
context. This is the `--list-nodes` trick applied to the chat, and it is the
check for anything prompt-shaped ("is my new tool described?", "did that doc page
join the index?", "how big is the prompt?"). With no project argument you get the
no-project-open variant.

**2. The pure half, from a harness.** `aichat.cpp` has no ImGui, no GL and no
`App`, so the doc index, the reply parser, argument validation, every read tool
and the transcript trimming can be exercised from a host `main()`. The cheapest
way to link one is to reuse the objects a dev build already produced:

```bash
OBJS=$(ls build-dev/CMakeFiles/tyrax-editor.dir/src/*.o | grep -v /main.cpp.o)
g++ -std=c++20 -O0 -I src -I build-dev/generated -I vendor/imgui -I vendor/imguizmo/src \
    -I vendor/imnodes -I vendor/stb -I vendor/ufbx -I vendor/miniaudio \
    harness.cpp $OBJS build-dev/CMakeFiles/tyrax-editor.dir/vendor/*/*.o \
    build-dev/CMakeFiles/tyrax-editor.dir/vendor/ufbx/ufbx.c.o \
    build-dev/libimgui.a build-dev/vendor/glfw/src/libglfw3.a \
    -ldl -lrt -lm -lGLX -lOpenGL -lpthread -o harness
```

(Everything but `main.cpp.o`, plus a harness with its own `main()`. The GUI code
comes along unused - it costs a link, not a display.) The same recipe works for
any host-only module that a plain `g++ aichat.cpp` cannot link on its own because
it reaches `project.cpp`. **On Windows** it is `src/*.obj`, no `main.cpp.obj`,
and the libraries CMake links: `-static -lopengl32 -lgdi32 -lcomdlg32 -lshell32
-lole32 -luuid -lws2_32 -limm32` (miss `comdlg32` and the only error you get is
`GetOpenFileNameW` from `platform.cpp`, which says nothing about a file dialog).
That harness is how the procedural read tools were checked without a backend -
`runReadTool` for `get_proc_graph` / `list_proc_nodes` over
`examples/procedural`, plus a `procGraphJson` -> `parseProcGraph` -> `procGraphJson`
round trip over all six of its volumes, which must come out **byte-identical** or
`set_proc_graph` loses whatever differs.

**2b. A malformed reply is its own test, and it needs no editor at all.**
`aigen::repairJson` + `json::parse` are the path a model's broken envelope takes
(a bare `"` left in prose is the common one, and it makes the whole document
unparseable). Copy `repairJson` and `extractJsonObject` into a scratch file next
to `src/json.cpp` and assert on the recovered `say` and on whether the tool calls
SURVIVED - that second half is the one that catches the interesting bugs, because
salvaging the prose while silently dropping the calls looks fine on screen and
ends the turn. Cases worth having: a clean envelope (must be untouched), a stray
quote with and without calls, a lone backslash, a markdown fence, `\uXXXX`
escapes (they must come out as UTF-8, not `?`), and plain prose (must NOT parse
as an envelope). Reuse ONE `json::Value` across the failed parse and the retry,
too - that is exactly how the stale-partial-parse bug in `json::parse` surfaced.

**3. The whole loop, with a FAKE backend and no tokens.** The backends are
external commands found on `PATH`, so a shell script called `claude` earlier on
`PATH` IS a backend. Have it drain stdin, count its invocations in a state file
and answer from a canned sequence, and the multi-step tool loop becomes
deterministic and free:

```sh
#!/bin/sh
cat > /dev/null                                     # the prompt arrives on stdin
n=$(cat "$STATE" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "$STATE"
case "$n" in
1) echo '{ "say": "Placing it.", "calls": [ { "tool": "add_object", "args": { "type": "box", "name": "lever", "position": [4, 0, -6] } } ] }' ;;
*) echo '{ "say": "Done." }' ;;
esac
```

A fake is also the only convenient way to feed the window a reply that is
BROKEN in a specific way - answer with an unescaped quote (`{ "say": "the "main"
scene" }`) or a lone backslash and check that the window shows prose rather than
JSON, which is what the repair path exists for.

Drive the window with `--ui-script` (no focus needed, see above) and check the
RESULT in the project rather than on screen - that is what makes it a test:

```bash
STATE=/tmp/fake PATH=/tmp/fakebin:$PATH TYRAX --ui-script <proj> "
frames 20; click Tools; click 'AI Assistant'
click 'AI Assistant/Allow project edits'      # see the trap below
click 'AI Assistant/##chatinput'; text 'add a lever'; click 'AI Assistant/Send'
wait 12; key ctrl+s; shot /tmp/chat.png"
TYRAX --dump <proj>                            # is the lever there?
TYRAX --refresh-gen <proj>                     # did its graph compile?
```

A fake can also drive paths a real backend reaches only after a long, expensive
session: have it answer with a HUGE tool result (`read_doc` of the biggest page)
to push the conversation over its budget, and have it recognise the compaction
request by its prompt (`grep -q "You are compacting a conversation"`) so the whole
compact-then-continue loop runs deterministically. That is how the context meter
and compaction were checked - 63.6k of context down to 5.8k, verified in the saved
chat file (`role: ['summary', 'user', 'assistant']`) rather than by eye.

The same trick reaches the tools that are expensive or destructive: a canned
`build_game` proves the park-and-resume (the turn stops, the Runner runs for real,
the outcome is appended to the tool result already in the transcript and the loop
continues - measured end to end with one Docker build), and a canned `set_section`
that sends an EMPTY list proves the shrink guard refuses it, then that
`confirm_replace` gets through. Check the destructive ones against the SAVED
project (`--dump`, the `objects/*.json`, the manifest) rather than the chat: the
tool result says what it did, the file says what happened.

Worth building canned replies that MISBEHAVE, because that is the half a real
backend will not reproduce on demand: a fenced envelope with prose around it, a
`name`/`arguments` spelling instead of `tool`/`args`, an invented node type, an
unknown property, a wrong object name. All of those have honest paths and the
transcript's tool rows say which fired.

**Two traps specific to testing anything that RUNS the game from a chat.** A
hermetic `XDG_CONFIG_HOME` isolates PCSX2 as well - its BIOS, memory cards and
settings live under `$XDG_CONFIG_HOME/PCSX2`, so the emulator either fails to
start or comes up unconfigured and the run looks like a broken feature. Symlink
the real one into the scratch config (`ln -sfn ~/.config/PCSX2 $CFG/PCSX2`) and
keep the editor isolated. And remember that **a build compiles the IN-MEMORY
model**: an assistant that added an object and built it produced a correct ELF
while the `.tyra` on disk still knew nothing about it, so the NEXT session's build
regenerated an empty debug runtime and no channel ever appeared - which reads
exactly like a broken devkit. A multi-session test has to save.

**Four traps this cost.** Set `XDG_CONFIG_HOME` (or `LOCALAPPDATA`) to a scratch
directory for any run that touches the chat: history files and `editor.ini` both
live there, so a hermetic run neither pollutes the machine's config nor inherits
whatever the last run left switched. **A popup's items live in a window of their
own** (`##Popup_<hash>`), so `dump` lists them under that name and not under the
window that opened it - a `dump | grep` that stops at the first screenful will
miss them and read as "the popup never opened"; grep for the LABEL, and confirm
with `shot` before believing a popup is broken (that cost an hour here, and the
popup was working the whole time). And uiscript's `text` needs the widget to have
processed the keystrokes before the button that consumes them is clicked -
`ImGui::BeginDisabled` on an empty field means a Send clicked too early is a
no-op that still reports success, so put a `frames 5` between typing and
clicking. `chatAllowEdits_` is persisted in `editor.ini`, so a run
that toggled it leaves it that way for the NEXT run - a scripted click on the
checkbox is a toggle, not an assignment, so assert with `expect-checked` right
after (a whole loop reading `[failed]` on every edit is that switch, not a bug).
And the reply is consumed by `aiChatTick`, which runs from `drawUI` whether or not
the window is open - so a `wait` long enough for N backend invocations is what a
multi-step turn needs, and closing the window mid-turn does not strand it.

## Verifying the packages and the update check (docs/updates.md)

Both halves are checkable on the machine you are on - neither needs CI, and
neither should be believed without this.

**The pure half first.** `update::compareVersions` and `update::parseRelease`
have no ImGui, no GL and no `Project`, so they take a 40-line harness (the
treegen/placement pattern):

```bash
g++ -std=c++20 -I src -I vendor/glfw/include harness.cpp \
    src/update.cpp src/json.cpp src/platform.cpp build/vendor/glfw/src/libglfw3.a \
    -o uh.exe -lshell32 -lole32 -luuid -lws2_32 -lcomdlg32 -lgdi32
```

Feed `parseRelease` a REAL payload, not only a hand-written one -
`curl -sL https://api.github.com/repos/<any>/releases/latest` is one command and
it is what proves the asset picker survives GitHub's actual shape. The same
harness can call `fetchLatest()` for the live path; against a repository with no
releases the honest answer is *no releases have been published yet*, which is a
state and not a failure.

**The UI half** is an ordinary `--ui-script` run - the modal is reachable with
no network luck involved, because a failed check opens it too:

```bash
./build/tyrax-editor.exe --ui-script "frames 10; click Help; click 'Check for updates'; wait 3; shot upd.png; dump"
```

The automatic startup check is deliberately skipped while a UI script is running
(`uiScriptActive_`), so no unattended run can have a dialog open itself mid-step.

**The installer is verified by installing it**, and that is worth the five
minutes because the thing it can silently get wrong is the LAYOUT. Note the
first trap: a silent install started from a console-attached shell just sits
there until something kills it - drive it with `Start-Process -Wait`.

```powershell
./installer/build-installer.ps1 -SkipBuild
Start-Process dist\TyraX-Setup-<v>.exe -Wait -ArgumentList `
  '/VERYSILENT','/SUPPRESSMSGBOXES','/CURRENTUSER',"/DIR=$env:TEMP\tyrax-test","/LOG=$env:TEMP\ins.log"
```

Then prove the installed tree WORKS rather than merely exists, which is two
commands and covers every exe-relative lookup at once:

```powershell
$env:TEMP\tyrax-test\bin\tyrax-editor.exe --vu-check           # finds <exe>/../vendor/tyra
$env:TEMP\tyrax-test\bin\tyrax-editor.exe --new t $env:TEMP    # then grep the engine path
Select-String tyrax-test $env:TEMP\t\docker-compose.yml        #   in the generated compose
```

Finish with `unins000.exe /VERYSILENT` and check that the directory, the Start
Menu folder and the `HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall`
key are all gone - an installer that cannot uninstall itself is a worse bug than
one that cannot install.

**The Linux packages are the same idea and cost less.** All three come out of
one staged tree, so the tarball is the one to unpack and RUN - it proves the
layout for the other two:

```bash
./installer/build-package.sh --skip-build --all --out-dir /tmp/pkg
tar xzf /tmp/pkg/tyrax-<v>-linux-x86_64.tar.gz -C /tmp/pkg
/tmp/pkg/tyrax-<v>/bin/tyrax-editor --new t /tmp/pkg   # then grep the engine path
grep engine-src /tmp/pkg/t/docker-compose.yml          #   in the generated compose
```

That last line is the whole point: it must name the UNPACKED tree's
`vendor/tyra` and not your checkout's. `--deb` needs `dpkg-deb`
(`apt-get install dpkg-dev`), `--rpm` needs `rpmbuild` (`apt-get install rpm`);
inspect what they declare rather than trusting the spec - `dpkg-deb -I`/`-c` and
`rpm -qip`/`-qRp`/`-qlp` print the metadata, the dependency list and the file
list, and a missing `zenity | kdialog` there is an editor whose Open button does
nothing.

**The self-update is testable without a release**, and it should be, because
every branch of it is a refusal:

```bash
g++ -std=c++20 -I src harness.cpp src/update.cpp src/json.cpp src/platform.cpp -o uh
```

Put that binary at `<root>/bin/`, write a `.tyrax-package` marker beside it and
call `update::installKind`/`selfInstallBlocked` for each word: `tarball` (empty
= the button appears), `deb`, `rpm`, no file at all (source checkout), and
`tarball` in a `chmod a-w` directory. Then drive the real thing - build a
throwaway "new release" tarball of the same harness, call
`update::runInstaller` on it, and check afterwards that the tree carries the new
files, that a file the new release does NOT contain survived (the overlay is
deliberate) and that the binary was relaunched.

## Verifying rigid-body physics (docs/physics.md)

Three layers, in order. **The hull** is a host harness: link
`src/physhull.cpp` + `src/primmesh.cpp` + `src/objparser.cpp` and print what
`build()` returns for a model - the unit box must read volume 1.0 and
covariance 0.0833 on the diagonal, and a legged prop must keep its feet as the
lowest corners (`examples/physics-playground`'s stool/table/chair/barrier all
report four at y = 0). **The codegen** is `--refresh-gen` + a grep of
`inc/model_data.gen.hpp` for `PHYS_HULL_COUNT` and `MODEL_PHYS_HULL`. **The
behaviour** is a PCSX2 boot of a short-path copy plus `--capture-frame`
contact sheets of the fall, and a rest check (two captures seconds apart after
settling must not differ where the props are).

Energy is the thing a screenshot cannot judge - a solver that injects energy
looks like "lively" bounces. Instrument the COPY's generated
`src/terrain_game.cpp` (never the template) with a `TYRA_LOG` of the awake
count and the summed squared velocity every 100 frames, compile it with
`tools/toolchain/native-build.sh` directly (an editor `--build` regenerates the
instrumentation away), and require both to fall. Cost is the same trick with
`profTicks()` around the `updateObjectPhysics()` call, against a baseline arm
generated by an editor built from the parent commit into its own worktree -
same example, same engine flags. Two traps from that round: `pkill -f
<pattern>` matches the shell running the command itself and kills it (select
PCSX2 by pid, reading `/proc/<pid>/cmdline` with `tr '\0' ' '`), and a second
PCSX2 left running on the same ELF halves both frame rates.
## Verifying the particle library (docs/particles.md)

`--bake-particles <projectDir>` re-bakes every effect's generated texture,
re-syncs the linked emitters and regenerates - run it twice and `md5sum` the
PNGs (the second run must change nothing), then grep `inc/scene_data.hpp` for a
linked emitter's row (its values are the EFFECT's, its last column is
`emitAdditive`) and `inc/model_data.gen.hpp` for `MATERIAL_PATHS` and
the `VEHICLE_SMOKE_LOOKS` row of a definition whose `smokeEffect` names the
effect (`inc/scene_data.hpp`: `effect` 1, the frame `.mtl` paths;
`examples/vehicle-playground`'s Rally 04 links "Rally dust", a two-frame
flipbook). The textures themselves are a harness away:
`particletex.cpp` links with `-I src -I vendor/stb`, a TU defining
`STB_IMAGE_WRITE_IMPLEMENTATION` and a one-line stub of
`project::particleLayerStem` (`bakeEffect` calls it). On Windows link it
`-static`: from Git Bash the `/mingw64` runtime DLLs shadow scoop's and the
harness exits 127 before `main`. The built-in tyre puff's recipe
(`vehbake::builtinSmokeRecipe`) was picked this way, side by side with the
hand-written puff it replaced. Composite each over a DARK and a LIGHT
background before judging it - a premultiplied flame looks right on one and a
smoke puff's edge only shows on the other. On the console, `examples/particle-lab`
is the fixture: `--build --run` then `--capture-frame`.

## Choosing the right depth

| Change | Minimum honest verification |
|---|---|
| A package or the update check | The section above: a harness for the pure half, `--ui-script` for the modal, a real install (silent on Windows, unpack the tarball on Linux) + `--vu-check`/`--new` FROM it for the layout, then uninstall / a driven self-update |
| Editor UI (a panel, a dialog, a toggle) | Layer 0 + a `--ui-script` run that opens it, does the thing and ASSERTS it (`expect`/`expect-checked`), plus a `shot` to look at. No focus, no coordinates, either OS |
| Editor viewport (rendering) | Layer 0 + a screenshot of the affected panel (`shot` from a UI script, `TYRAX_SHOT` on a timer, or `screenshot-window.ps1`/`wayland-control.py` from outside) - and measure the pixels rather than eyeballing |
| Serialization (`.tyra`) | Layer 1 `--new` + reopen; round-trip save/load diff |
| Codegen / templates | Layer 2 grep or harness, then one Layer 3 boot |
| Engine (`vendor/tyra`) | Layer 3 always — compile with the native backend (and Docker too for compatibility-sensitive changes); SW-renderer screenshot for anything visual |
| Audio | Layer 3 + peak-meter check |
| Anything a player DOES (buttons, walking, menus, two players) | Layer 3 + `--pad` (see the recipe above) — an idle control shot, then drive, then measure. No human, either OS; `watch` (Linux) / `-Watch` (Windows) collapses the whole drive into one contact sheet |
| Anything that changes how a frame is BUILT or PRESENTED (the upscaler, frame pacing, extrapolation, buffer counts, a full-screen pass) | Layer 3 + **the motion gate**, two arms one knob apart. A parked A/B cannot see a fault that only exists in motion, and four of those reached the owner on this branch |
| A dynamic-shadow switch (spot/flashlight volumes, blob shadows, a per-object shadow mode) | Layer 3 + **the shadow A/B rig** — `make-shadow-fixture.ps1` then `shadow-ab.ps1 -Toggle <key> -Values a,b`. Quote the `report.md` deltas against a same-value control run, not a pair of screenshots |
| ISO export | Export + mount the ISO on the host + boot it in PCSX2 |


## Animated directional GI

Use examples/probe-lighting in a scratch directory. Bake with `--bake-gi DIR
--gpu`, build and boot in PCSX2 software mode, then walk from courtyard through
the doorway with Remote Pad. Compare frozen poses with the old dominant-lobe
path, keeping the same baked table. Check two pose-sharing instances near the
opposite side lights: their bags must retain separate directions. Check yaw
and uniform scale, GI disabled/dead probes, and both viewport shading modes.
The current signed RGB SH mode must retain independent coloured directions
and darken back-facing normals; L2 and surface-transfer PRT are not implemented.

Verified on Windows: probe-lighting GPU bake, Docker/PCSX2 software boot,
fixed-pose L1 A/B with a byte-identical return-to-new control, both editor
shading modes, and a separately built/booted GI-disabled fallback. The example
README records the sampled coefficients and limits; these are not hardware
performance measurements.


### Animation and RGB SH regression checks

`--vu-check` includes signed chromatic coefficients in the randomized comparisons
and a numeric RGB oracle (opposing normals, rotation, both lighting modes,
negative output and saturation). This still needs a PS2 Docker build and a
software-renderer walk through `examples/probe-lighting`.

For animation timings enable `TYRA_SKEL_PROFILE` in skel_instance.hpp, rebuild,
and inspect per-instance `SKELTIME`. Measure ordinary playback, not the frozen
lighting comparison. For LOD stress, use three humanoids at meshLod 1.5 in a
scratch copy, walk through the doorway and force all three tiers in generated
code if camera distances do not cross each threshold. Distinguish a hang that
is reproduced from an old observation that is not.

When restoring generated C++ with Copy-Item, touch its LastWriteTime or clean
the scratch game objects: Copy-Item preserves old timestamps, and make may
otherwise relink the previous instrumented object despite different source.


Lighting DMA lifetime regressions need temporal checks: an arithmetic VU test
and a single screenshot cannot expose a dangling REF to stack data. Hold the
camera/pose fixed and compare a sequence of frames (excluding the FPS HUD),
then restore normal animation and walk the scene. Exercise the actual packet
submission; `--vu-check` validates microcode, not the EE source pointers.

## Eight-view foliage impostors

Use examples/impostor-grove (its README has reproducible generators). Verify
`--resave` retains `impostor`, `impostorDistance` and `impostorBillboard`, `--refresh-gen` collects both
model paths, then build/run a short-path copy. Show profiler logs representation
transitions as `IMPOSTOR object=... far=...`; walk towards and away from a tree.
Compare a frozen-camera control with every distance set to zero. Verify binary
alpha and deterministic atlas bytes after regenerating assets. Rendering changes
need a screenshot, not just the model table. Host fixture C++ helpers belong under
`authoring/`: a root-level .cpp can expand the Makefile's unquoted find glob and
leave SOURCES empty (`undefined reference to main`).

For eight-view captures, compare a frozen single-tree near/far pair and orbit it:
`IMPOSTOR VIEW` logs sector changes with Show profiler enabled. Verify the 512x256
atlas, all eight ordered parts, yaw rotation and fallback for tilted transforms.

Universal capture: select a non-tree multi-material OBJ, activate the Properties
tab, click Bake impostor, save/reopen and inspect assignment plus alpha/Kd. Check
material overrides and missing textures/reflection failures; no assignment should
change on failure. The grove now includes Waystone for this path. Close floating
tool windows (or remove their open flag in the scratch layout) before scripted
scene selection; an overlapping Tree Generator can intercept the click.

Selection: verify list selection shows the full model box, then a viewport click
hits the same model. A frozen orbit camera aimed inside an empty part of a model
AABB tests fallback. Put an alpha-cutout card in front of a smaller prop to check
that transparent texels do not steal a surface hit. Test a distant billboard and
rotated/scaled instances; no PS2 rebuild is required for editor-only picking.

The viewport image is named `Viewport canvas` for `--ui-script` (1.74.1).
Set a scratch project camera to aim at the test point, then `click 'Viewport canvas'`
then `key ctrl+s` and check `editor.selectedObject` in the scratch manifest
(scene rows are not checkable widgets). This injects a real viewport click without
OS focus; the test hook only registers the existing image rectangle.

Verified on Windows for 1.74.1: grove scene-list selection shows the whole
Waystone bounds; a viewport click in its empty AABB selects it; a transparent
foreground quad lets the mesh behind win, whereas its opaque version wins
itself; a distant Waystone impostor remains clickable.

Grove ground regression: freeze the player at (48, 0, 48), heading -135,
pitch -12. The old floor material (-s 16 16) loses detail there; -s 0.5 0.5
restores it in PCSX2 software rendering. Terrain material scale counts repeats
per world unit, not over the whole map. Compare a fixed ground crop; do not
infer a missing texture binding from a flat colour alone.

GPU impostors: `--bake-impostor PROJECT MODEL OUTPUT_STEM 4|8|16 [--gpu]` prints
backend and total time without assigning an object. Compare CPU/GPU atlases for
all counts on a cutout tree and multi-material Waystone: same dimensions, binary
alpha, close silhouette masks, matching Kd/interior colours; GL edge coverage is
not byte-identical. Invalid counts fail before output. Verify Capture views in
Properties only changes saved `impostorViews` after a successful bake, resave an
old eight-view object, and boot mixed 4/8/16 assets in PCSX2 while orbiting. Run
`--gi-gpu-check` after changes to the shared bake context and inspect the viewport
after GPU baking to catch context restoration bugs.

Verified 1.75.0 on Windows: model UI baked/saved 16 GPU views, Tree Generator
baked/saved 4 GPU views; choosing 16 without baking kept a legacy object's 8.
The capture oracle passed all counts (Waystone IoU 1.0, oak >0.999); forced CPU
fallback was byte-identical to CPU PNG output and invalid counts wrote nothing.
GI's shared-context oracle still agreed (0.0022% relative mean error). The mixed
4/8/16 grove built and booted in PCSX2 software mode; Remote Pad motion produced
capture-sector updates without allocation failures. Higher atlas counts consume
VRAM: the tested mixed scene had about 35 KiB free after moving, with evictions
during motion, so do not describe larger view counts as free.

## Vehicle terrain stability

**Damage** (docs/vehicles.md): `--vehicle-check`'s `-- damage --` block holds
the host rules. In the game, `On Start -> Enter Vehicle` seats the player in
vehicle-playground's CC96 facing +z; `--pad "hold r2; wait 12; release all"`
drives it into the arena wall and `bin/log.txt` answers with a `VEHDMG ... hit`
line (impact, damage %, vertices moved, microseconds). The throttle is R2 and,
since 1.147.0, reverse is L2 held from a standstill (`hold l2`; it brakes
first if the car is still rolling) - the left stick only steers. To SEE a
front dent from the chase rig, back off a few units and HOLD R3 (the rear view
puts the camera ahead of the nose) while `--capture-frame` runs - against a wall
the rig sits inside it. The editor side needs no game: Damage tab > Hit front
with the car framed via the manifest's `editor.cam` (`[yaw, pitch, dist, tx,
ty, tz]`, radians) and `selectedObject` - edit that key by its `editor` block,
not with a global replace: vehicle definitions have a `"cam"` key too. Loose
pieces: `VEHDMG <car> lost <kind> ...`; the Ravager into the arena wall from the
start loses kind 5 and 1 in the hit, and a burst of `--capture-frame` right
after `lost 1` appears catches the bonnet in the air. To force a door off,
park another car broadside across the start lane in a scratch copy (its object
JSON `position` / `rotation`) and raise its `damageLoose`.

Run `--vehicle-check` after changing vehicle contact or transforms. Its bank
fixture restrains horizontal translation but retains gravity (zero gravity
would prevent initial clearance from settling). It covers six headings and
20/25/50/120 Hz, generic-renderer versus wheel-rig orientation including Euler
singular headings, missing terrain contacts, and alternating 50/8.33 ms steps
with only a bumper supported. The latter must not generate launch velocity.
These are host properties, not a PS2 frame-rate measurement: build and drive
vehicle-playground for runtime validation, and measure wheel-batch changes on
the console/emulator with an unchanged mesh and camera.

Whether a tyre touches the ground is a LOG LINE, not a screenshot: the game
prints `VEHCONTACT car N def D parked|driven gap1000 ... roadlift1000 ...`
(lowest drawn tyre vertex minus `groundSurfaceAt`, per wheel, in mm; a parked
car once as it sleeps, the driven car every second, nothing on a far tier).
0 is on the ground; -119 on a road was the terrain-only contact bug, -7 a
definition radius behind its baked wheel (docs/vehicles.md, "Wheels on the
road surface"). Check a car on a road AND one on bare terrain - the road
lift column tells them apart.

## Triangle strips: a host property test, then one knob in PCSX2

A stripifier is the kind of code that renders *almost* right, so check it where
checking is free. `src/meshstrip.cpp` has no GL, no ImGui and no `Project`, so a
harness links in seconds:

```bash
g++ -std=gnu++20 -O1 -I src -o stripcheck harness.cpp src/meshstrip.cpp
```

The property that matters is not "it produced a strip" - it is that the strip
draws the SAME TRIANGLES. Expand the output back (triangle i of a run is
`v[i], v[i+1], v[i+2]`, never across a run boundary), canonicalise each triangle
so winding does not matter (nothing backface-culls) and drop degenerates, then
compare the multisets. Assert the structural invariants in the same pass: every
run but the last exactly `kRun` vertices, every run length a multiple of 3, and
the result smaller than the list it replaces. Feed it grids in BOTH orientations
- a 200-cell row and a 200-cell column of one grid behaved completely
differently and that is how two real defects were found (seed order, and the
three-versus-six seed orientations) - plus a cube, ten cubes, and a pile of
triangles sharing nothing, which must be REFUSED rather than stripped.

**Take the multiset over the attributes the BAG receives, not over all eight
floats.** `meshstrip::Weld::kNoNormal` (docs/model-pipeline.md, "The weld key is
a property of the BAG") welds on position and UV, so a strip vertex may legally
carry a different normal from the corner a given triangle had - an all-eight
comparison FAILS on a correct stripper, which is the kind of check that gets
"fixed" the wrong way. A worked instance is
`examples/vehicle-playground/authoring/wheel-strip-2026-09-17/stripcheck-wheels.cpp`,
which runs the property against the district's three REAL baked wheels rather
than against synthetic input:

```bash
g++ -std=gnu++20 -O1 -I src -o stripcheck     examples/vehicle-playground/authoring/wheel-strip-2026-09-17/stripcheck-wheels.cpp     src/meshstrip.cpp
./stripcheck examples/vehicle-playground/.res-baked/vehicles
```

**And a hand-written grid strip needs a PICTURE, because no count can see its
one failure mode.** The quad diagonal flips unless each column pair is emitted
far-corner-first, which on a terrain-following surface is a different surface -
54 pixels of one 512x512 self-capture, against arms that were each
byte-identical over three repeats. `meshstrip` output is immune (the multiset
test covers it); roads, terrain and the projected-shadow receiver patch are not.

**The PCSX2 arm is one knob, not two builds of different trees.** Build two
editor binaries from the SAME worktree differing only in whether the bake calls
`meshstrip::build`, and run both against one project directory. The engine, the
assets and the generated game are then byte-identical between arms and the only
difference is what the `.tmdl` carries - which is what makes a pixel diff
attributable. Freeze the camera from a global script (`TYRA_SCRIPT`, no
attachment), set `displayMode: progressive`, and capture with the game's OWN
`--capture-frame` rather than a window grab: on the Motor District that gave
**three byte-identical captures per arm**, so any non-zero difference is the
change.

Aim a pose at the clip path deliberately. The guard band sends most
screen-straddling packages down the cull route, so an ordinary outdoor vantage
exercises the strip-to-list expansion ZERO times - `sexp=0` in the `FTCLIP` line
says so. Park the camera inside a building and it runs thousands of times.
And read `verts` from that line, not the triangle counts: a strip package
reports its degenerate joins as triangles.

### Roads and terrain (1.96.0)

Those are grids, they are stripped too, and neither goes through `meshstrip`,
so the harness above does not cover them. Their oracle is
`examples/vehicle-playground/authoring/verify-road-twins.py` (Python 3 + g++,
no emulator): it compiles the real host tessellator AND the actual generated
`buildRoads` body with storage stubs, then checks the strip output vertex for
vertex and chunk for chunk between the two, asserts the same run invariants
(interior runs exactly `roadgen::kStripRun`, every run a multiple of 3), and
compares the expanded triangle SET against the list emitter's. Run it before
anything else - it is seconds, and it is the only check that sees both twins.

Since 1.117, road V is rebased by a whole repeat per chunk to keep physical-GS
ST values bounded. The list and strip chunk boundaries can differ, so the
oracle compares their V modulo integers while keeping positions, U, fractional
V and the host/runtime strip stream exact. Do not replace that with raw V
equality: it would reject texture-identical output and tempt the hardware fix
back out. A stationary physical-console capture is still the acceptance gate;
PCSX2 did not reproduce the stretched-one-texel failure.

**Road HEIGHT queries have their own gate, and it is an in-game one** (1.123,
docs/roads.md). `roadSurfaceAt` is sampled 17 times per blob shadow, per light
pool and per glow patch, and it is answered from a uniform XZ grid built once
after the roads are. The grid must be a NO-OP, so the check is
`TYRA_ROAD_INDEX_VERIFY` in the generated `terrain_game.cpp` - default 0
(it costs more than the work it checks, so it never ships and never goes into a
measurement). Flip it to 1 AFTER `--refresh-gen`, build with `native-build`
directly, drive, and read `ROADINDEXVERIFY checked N bad M` out of the game's
log; `ROADINDEX cells NxN entries M` at scene load says the grid was built at
all. Measured on the district: 140 000 queries, 0 mismatches, day and night.

Two traps, both of which make a green run worthless. **An off-road query agrees
trivially** - both sides answer "no road" - so a run that never walks onto
asphalt proves nothing; the deliberately broken control arm stayed green for
40 000 queries for exactly that reason, and only went red once the walk crossed
a street. And **falsify it before believing it**: shifting the query one cell in
x must make it report a first mismatch, with the point that produced it.

**The static-batch grouping has the same shape of oracle, and for the same
reason.** `TerrainGame::buildStaticBatchList` is generated code that runs on
the EE; `src/staticbatch.cpp` is its host twin (what *Tools > Static Batches*,
the viewport overlay and `--batch-report` all read). Their oracle is
`examples/vehicle-playground/authoring/verify-batch-twins.py` (Python 3 + g++,
no emulator, seconds): it lifts `buildStaticBatchList` VERBATIM out of
templates.cpp by string index, compiles it beside the twin against small stubs,
and diffs the batch assignment member for member over eleven fixtures - texture
and cell and lamp and strip-run keys, the singleton drop, the oversized-model
guard, batching off, and a deliberate MISSING-TEXTURE case (acquireTexture
returns nullptr for a file that is not on disk, so every missing texture
batches with every untextured primitive - a twin that "fixes" that is wrong).

Two things worth copying. It asserts the shape of what it LIFTED (the key
names must still appear in the body) so an agreeing diff of two implementations
cannot silently become a diff of nothing. And it was checked that it can FAIL:
break one key in the twin and two fixtures disagree with a readable diff and a
non-zero exit - an oracle nobody has seen fail is not evidence.

The ELIGIBILITY half needs no harness at all: `batchStatic` in a generated
`inc/scene_data.hpp` IS the twin's stage-1 verdict, so `--refresh-gen` plus a
census of that column checks it (87 of 142 on examples/vehicle-playground). And
`--batch-report`'s totals can be read against the running game's own two log
lines ("Static batching: eligible 87, solo 22" / "65 objects in 48 batches").

Since 1.105 it also prints, per fixture, the three numbers behind the road's
lateral budget (docs/roads.md): the **worst surface error** against the dense
reference, the **worst UV drift in texels**, and the **worst T-vertex seam**.
Two things about that seam check are worth stealing for any mesh reduction.
It is an exact T-VERTEX test - a vertex lying strictly inside another triangle's
edge in XZ and off it in Y - because SAMPLING a surface at its own vertices
reads barycentric noise off every triangle that merely touches the point, and
that noise floor (measured here at 5e-5) is larger than the seams worth finding.
And its fixtures had to be built for it: the original eight are analytic
surfaces, curved everywhere, with no coplanar runs to merge, so they passed a
reduction they could not exercise. The three `heightfield ...` fixtures lay a
road over a triangulated 4-unit grid - the district's real ground - and are the
only ones in the file where the merge fires at all.

**The PCSX2 knob for these is one expression, and it has to be flipped in
`src/templates.cpp`, not in the generated game.** `buildRoads` and
`buildTerrainChunk` each gate on `minPackageSize() >= 72`; raise that number to
something no class derives and the same scene builds as triangle lists.
Editing the generated `src/terrain_game.cpp` does NOT work - `--build`
regenerates it first and your probe is gone (the same trap as any generated-
file edit). Flip it in templates.cpp, build the editor, and **keep a COPY of
each binary** (`build-dev\tyrax-editor-strips.exe` /
`build-dev\tyrax-editor-lists.exe`), then build one fixture with each. Two
things the copies must respect: they have to live INSIDE the checkout, because
the editor finds `tools/toolchain` and `vendor/tyra` relative to its own
executable and a copy in a scratch directory fails with "Native toolchain files
are missing"; and confirm the two really differ
(`grep -c 'minPackageSize() >= 4096U'` reads 2 and 0) - the binaries come out
the same SIZE, so a mis-timed copy looks exactly like a successful one.
Terrain additionally needs a terrain MATERIAL to strip at all -
`TERRAINSTRIP ... strips 0` in `bin/log.txt` is that case and not a failure.

The render-cost row `Roads` is ready road/junction cull and submission, not
generation; roads are built once at scene load. `Procedural` explicitly skips
owner `-3` road chunks and now means only procedural volumes/prefabs. Old CSVs
from before 1.117.6 reported both together as `Procedural`, so do not compare
those labels directly without summing the new rows.

**Do not use `--build --run` for this.** It kills every running PCSX2, and with
parallel worktree sessions that is somebody else's game. Launch
`pcsx2-qt.exe -elf <project>\bin\vehicle-playground.elf` yourself and talk to
it through that project's own `bin/livedbg.cmd` (`--capture-frame <project>`),
which is per-project and cannot reach the other instance. Run the editor
through a normal `--build --run` once beforehand so `HostFs` and
`Renderer = 13` are already in `PCSX2.ini`.

**The district's NIGHT poses are not frozen, and no settle time fixes them.**
Freezing the camera is not enough: at night the lamps flicker, so three
captures of ONE arm differ from each other at 6 s of settle and still differ at
30 s. That makes any between-arm difference at a night pose unreadable, and it
is a property of the fixture rather than of whatever you changed. The two DAY
poses are byte-identical across three captures in both arms, so base a pixel
A/B on those and quote the night poses' own within-arm spread as the noise
floor if you use them at all. Check the repeats PER POSE before reading any
difference - "the fixture is static" is an assumption this one does not meet.

For "do both arms draw the same thing", read the producers' own lines -
`ROADSTRIP scene N strips S packages P triangles T` and `TERRAINSTRIP scene N
chunk cx,cz ...`. Those triangle counts are surface counts with degenerates
dropped and MUST match across arms; `FTCLIP`'s cull/clip/guard triangles are GS
primitives and legitimately differ by about 1.5x between representations.

## Motor District and flat-road spans (1.85.0)

The roadgen.cpp / templates.cpp buildRoads twins sample every lateral height,
then retain the established horizontal collapse, or merge a non-flat span's
lateral cells into maximal runs within the two budgets in roadgen.hpp
(`kSpanFlatness`, the surface and the seam; `kSpanShear`, the UV). Do not infer
planarity from the shoulders: an interior crown or saddle must retain its
samples. Run examples/vehicle-playground/authoring/verify-road-twins.py
for a compiled comparison of both actual implementations, then build/drive the
example. ROADS now logs emitted vertices as well as chunks. The district's
seven-road network is an EE memory stress case, not just a screenshot fixture.

## Measuring a CONTENT change, where the arm is the editor (1.105)

A codegen change has no engine macro to flip, so the two arms are two EDITOR
binaries generating the same example. Build both from the worktree under test -
`./build.ps1`, copy the exe aside, `git checkout <base> -- src/<the changed
files>`, build again, copy aside, restore - and record BOTH hashes.
`examples/vehicle-playground/authoring/road-lod-2026-09-16/build-arm.ps1` takes
the editor as a parameter for exactly this reason, and holds the baked asset
tree constant across the arms so the only difference is generated code.

Three things that round taught, all of them cheap to repeat:

- **`--profile quiet-debug`, not `debug`.** The live tools' `host:` pollers cost
  6.44 ms of `work` and put +3.5 to +4.4 ms outliers inside `update`. With them
  off the repeatability floor was **0.016 ms of `work`**, eight times tighter
  than the same rig at `debug`.
- **A map-wide count is not a frame.** The road reduction removed 9 798
  triangles from the district and **614** from the garage-day frame. Measure the
  pose that is slow, not the inventory.
- **A quality criterion can usually be turned into a number instead of a
  screenshot.** "Coarse terrain must not bury a road" became the worst rise of
  the coarse ground above the road's own lift, at every dense sample of every
  road (`terrain-lod-burial.py`); "no distracting LOD transitions" became the
  worst mesh disagreement converted into pixels at the distance the band
  switches (`terrain-lod-step.py`). Both run in seconds with no console.

Textured vehicles: vehbake::Result::textures holds bin-relative names and PNG
bytes for source images; bakeProject and vehicleRefreshBake both write them.
The viewport resolves ModelPart::bakedTextureRel per draw, never a cached GL
name. Palette UV fixup must only visit palette parts (real UV V=-1 is valid).
A wheel/body image and Kd match permits textured wheels in body distance tiers.
Use the GGBot GLB in Motor District to check the PNG references, actual in-game
texture, four detected wheels and the distant wheel silhouette. Rigid nodes
sharing one material may collapse to one dominant owner during import; the
example preparation retains one material slot per wheel.

Shared dynamic env sampling must use the LEVEL capture's world-up, not the
pitched chase camera's up. Static sphere-map images keep the view basis and
reflected-ray probes keep their own basis. The viewport envSt shader mirrors
this distinction. To diagnose a reflection, inspect the env target separately
from the final car: populated target + unchanged car in a scenery hide/show
comparison is a sampling problem, not proof that the capture failed.

Mixed vehicle definitions keep separate runtime wheel batches so a palette car
and a textured car never sample through the last vehicle's image. Verify both
cars together at near range; far tiers carry their own baked wheels.

Wheel batches retain their per-definition position, colour and UV buffers until
scene unload: PATH1 DMA can still read one definition while the next is being
prepared. The conservative CPU reject must cover the actual rig (wheel mesh
radius, track, wheelbase, full suspension travel, steer, spin and body
attitude), and must use the active camera/frustum plus the body's visibility,
draw-distance and split-band gates. Check a near/far LOD crossing has neither
duplicate nor missing wheels, then capture the separate `Wheels` render-cost
row; it is a diagnostic phase, not a hardware FPS result.

## Render-cost capture

Use `--profile-frame PROJECT -o report.csv` against a debug game with Live
Debugger on and a live host server. Verify matching sequence/footer, finite
phase/object values, repeated captures and CSV output. For UI changes drive
Render cost / Measure render cost / Keep as baseline / Copy render cost CSV,
and click the column headers: each sorts, and a third click on the same header
clears the sort back to the original grouping.
Object rows are nested costs inside Objects. Measurements drain the pipeline;
verify ordinary FPS separately and capture the same camera on physical PS2.

### Manual ps2link launch: require the resident-IOP marker

Prefer `--build PROJECT --run-ps2 IP`, which creates the marker automatically.
If running ps2client manually, first create and verify **an absolute path** to
`PROJECT/bin/ps2link.run` containing `ps2link`. Launch with `PROJECT/bin` as the
working directory. `-ps2link` on execee alone is insufficient with this crt0.
A missing marker resets IOP, removes the host filesystem/network service and
can require a physical reboot; a later UDP poweroff cannot repair that loss.
Do not chain a failed marker write into an execee command. In PowerShell use
`-ErrorAction Stop` and `Test-Path -LiteralPath` before launching. A working
directory already ending in `bin` must not receive another relative `bin/`.
Also stop an emulator serving the same project before hardware captures: its
fresh `livedbg.bin`/`frame.tga` can otherwise disguise a disconnected console.

### A debug build over ps2link is NOT the player's frame (2026-09-23)

Read this before quoting ANY frame time or FPS off a console. Three separate
debug-only costs sat inside every Motor District measurement for two days, and
each one looked like a property of the scene. All three were measured on a
physical PS2 with `TYRA_FRAME_PROFILE 2` (below) at a parked vantage.

| devkit cost | cadence over ps2link | what it did to the frame |
|---|---|---|
| **Remote Pad** polls `host:livepad.bin` | every 4th frame (every frame in PCSX2) | a network round trip in `loop()` ahead of `beginFrame()` |
| **Live Debugger** polls `livedbg.cmd` / writes the snapshot | every 25th frame (6th in PCSX2) | the same, rarer |
| **HUD `MEM` readout** (`showMemory`) - FIXED in 1.123.10 | every 2 s of game time | `getAvailableRAM()` malloc-probed the heap until it failed: **+31 ms** on one pose, **+75 ms** on a fragmented one. It now reads `mallinfo()` + `EndOfHeap() - sbrk(0)`; a build older than 1.123.10 still has the hitch |
| **`TYRA_FRAME_PROFILE 1`** | every frame | the packet walker + pipeline telemetry: HUD SCENE 13.21 -> 17.70 ms |

The first two together: the day frame read **26.3 ms with 15 of 50 frames
missing their field; with Remote Pad and Live Debugger compiled out it read
20.4 ms and 0-1 of 50** - the shipped scene already held 50 FPS. 14.5-16.5
network round trips per 50 frames against 14-16 measured misses. The MEM probe
is the regular **hitch every two seconds** a debug build shows (one 94 ms
frame per 50 at 25 FPS); it is gone the moment `showMemory` is off, 8 periodic
spikes against 0 in a paired emulator run.

**The clean console measurement, in order:**

1. In the project's `.tyra`: `remotePad`, `liveDebug`, `liveLink`,
   `liveLogic`, `timeMachine` and `inputRecorder` false (and `showMemory` too
   on an engine older than 1.123.10).
   (No Remote Pad also means no scripted day/night toggle - plan the poses.)
2. For diagnostic FRAMETIME, `TYRA_FRAME_PROFILE 2` omits the packet walker
   but retains the fairness `align3D()` drain. Label those intervals serialized.
   For ordinary asynchronous timing use FrameProfile=0/HardwareTrace=0 and a
   separately calibrated ordinary-clock sampler; do not add a completion wait.
   Devkit-off alone is insufficient: authored VEH/VEHCONTACT logging and
   Vif1Queue arena reports/RAM probes can remain active. Inspect actual compiled
   code and runtime log families, and privately suppress healthy telemetry
   before calling a fixture quiet. Keep WARN/ERROR/TRAP/assertion paths.
3. `--refresh-gen`, then `tools/toolchain/native-build.ps1` directly.
4. Deploy with a bare `execee` straight after a power cycle (next section),
   and read `FRAMETIME` off the ps2client stdout. `pre + work + stall` must
   come to `period`; `miss` high with `over20` low means the cost is OUTSIDE
   the render (`work` starts at `beginFrame()`). `FTUPD`, printed beside it,
   splits the update half of `pre` into ten sections - `in` large means the
   devkit's host polling, not the game.
5. **Apparatus check:** a same-pose HUD SCENE match is only a coarse diagnostic
   check. It does not prove equal overlap, cadence or observer cost. Ordinary
   pricing requires actual native FrameProfile=0/HardwareTrace=0 bindings,
   quiet-window checks and same-ELF sampler controls in both orders.
6. `FTRAW` gives every frame's `work`. A periodic spike is a TIMER - check the
   gaps: exactly equal gaps are frame-counted, gaps alternating 50/51 or 99/101
   are seconds converted through `g_frameDt` (`everyFrames(s)`), which is how
   the MEM probe was found in one read.

**TWO different console failures look alike - tell them apart first.**

- **Empty ps2client log, not even `loadelf:`** - historically an immediate
  execee after reset raced IOP startup (2026-09-23). Silence alone does NOT
  distinguish a lost command from a running game with missing tty output.
  Follow the editor protocol at the start of this skill: 10-second settle and
  keep a live client after the 15-second log timeout. The old automatic
  kill/re-send recipe is superseded; it can remove a running game's assets.
- **`freepad: DMA Busy ... ret = 0` repeating** - a real IOP wedge on the next
  boot, and only a power cycle clears it. That is the one below.

**Two arms must be looking at the same thing.** A pad whose stick rests off
centre moves the view on one boot and not the next (the car's glance camera
did exactly that until 1.124.2 and faked a +2.7 ms regression). Compare
`Loop_past_draw_count` / `Loop_past_coarse_count` in the two captures before
comparing any millisecond, and take a `--capture-frame` of each arm.

**Resetting a console that is running a debug build can wedge it.** From the
evening of 2026-09-22 most `reset` + `execee` cycles of a debug build came back
with `freepad: DMA Busy ID = ... ret = 0` repeating (or an entirely empty
ps2client log) and `livedbg.bin` stuck at frame 1, and only a physical power
cycle cleared it; a devkit-off build came back clean from two consecutive
resets. The prime suspect is the reset landing while the devkit's `host:` I/O
is in flight. The polling code is months old, so WHY it became frequent is not
known - do not write a root cause. What works: straight after a power cycle
ps2link is idle, so send a **bare `execee`** - a probe-`reset` followed by a
launch-`reset` failed twice in a row. Budget one clean boot per power cycle for
a debug build, and measure on a devkit-off build, which both resets cleanly and
is the frame the player gets.

**`--build --run` (PCSX2) deletes `bin/ps2link.run`.** Re-create it before the
next console deploy (`printf 'ps2link' > <project>/bin/ps2link.run`), and keep
the `execee` inside the script that checks it - a guard that only `exit`s is
bypassed by the next command in a `;` chain, which is how a launch without the
marker reset the IOP mid-load and cost a power cycle.

### PCSX2 savestates and GS dumps, unattended (docs/emulator-captures.md)

`scripts/pcsx2-capture.py run <elf> --stage --states 3 --gsdump` starts a
**private** PCSX2 (`-datapath` under `%TEMP%\tyra-editor-test\pcsx2-capture-<slot>`,
its own ini with PINE on), takes savestates over PINE and one single-frame GS
dump, writes `report.txt` and stops only the PID it started. `gs <dump>` /
`state <p2s>` analyse hand-taken captures of any title. Needs
`pip install zstandard numpy`.

- **No focus, no global input.** The GS dump is PCSX2's own hotkey, rebound to
  F12 in the private ini and delivered by `PostMessage` to that PID's window,
  which is inside the rule in "never inject global input". Windows only; Linux
  skips the dump step.
- **One `--slot` per parallel run.** The user's global `PCSX2.ini` is never
  touched.
- **A savestate lands at VSync, so VIF1 is always idle.** For a TyraX game it
  shows at most the last ping-pong packet. Use the VU1 packet tap
  (`arm-vucap.py`) for our own chains; the savestate analyser is for games that
  build one frame chain.
- **GS pixel counts are scissored coverage, not fill.** Compare with them; do
  not price the GS with them.

### What is SUBMITTED is not what is SEEN: ask by REMOVAL

An inventory ranks a frame by what each producer hands the GS. It cannot say
whether any of it reaches the screen, and a whole front was once promoted on the
assumption that it did. The instrument that settles it needs **no new engine
counter and no engine edit**, because the game can already hide an object at
runtime (`ctx.objects[i].visible`):

> hide exactly one object, photograph the frame, diff it against the control.
> **Zero pixels changed means the object contributed nothing by ANY path** —
> silhouette, projected shadow, baked AO or reflection — so it is free to cull
> and there is no quality argument to have. A non-zero count IS its on-screen
> contribution, and `triangles / visible pixels` then ranks the population.

`examples/vehicle-playground/authoring/world-visibility-2026-09-17/` is the
worked example. `probe-visibility.ps1` drives the whole population **from one
boot** — write the index set to the fixture's command file, wait, then
`--capture-frame` — which is the difference between twenty minutes and an hour
per object.

Three checks come BEFORE any verdict is read, because every claim of this shape
is a claim about a zero and a broken instrument produces zeros too:

- **within-probe repeatability**, then **control drift** (photograph the control
  first AND last in the same boot; a run whose control moves is dead), and
- **a positive control for every zero.** A lone zero cannot tell "invisible"
  from "the hide never reached that index". Photograph the suspected occluder
  removed too: if the object then paints pixels, the hide reached it and it was
  genuinely behind something. **Do not read that pair against the control** —
  the pair and the occluder-alone probe change the same screen region, so they
  report the same count of different pixels and the comparison silently
  confirms nothing. Compare pair against occluder.

Also worth knowing: the self-capture writes over `host:` fs and that write can
tear (`wrote N of M bytes - the host: write did not complete`). It is a
transient — **retry the capture** rather than letting it kill a long single-boot
run, which is exactly what it did the first time.

### Proving "collisions and picking are unchanged" WITHOUT a capture

A capture can only show the poses you thought to photograph. When a change is
supposed to leave gameplay data alone, diff the GENERATED SCENE DATA instead —
it covers every object at once and it is exact.

The obvious version of that diff FAILS, and the reason generalises to any
change that adds an asset. Adding models to a project inserts them into
`model_data.gen.hpp` **in place**, so every model index above the insertion
shifts and a plain diff of `scene_data.hpp` reports a change on most rows. The
coupe's `model` going 11 -> 13 is the same vehicle body. So:

1. read both `model_data.gen.hpp` tables and build `index -> name` for each;
2. rewrite the control's model field to the candidate's index for the SAME
   NAME;
3. diff, and require the residual to be only the fields the change owns.

`examples/vehicle-playground/authoring/impostor-threshold-2026-09-17/verify_scene_identity.py`
is the worked example. Two things it learned the hard way: select the object
rows by their FIELD COUNT (other tables in the same header share the
`{...}, // name` shape), and expect **consequences** as well as the change —
assigning an impostor also clears `batchStatic`, because a batched member has
no bag of its own to swap. Report those rather than allow-listing them silently.

### Judging a POP: the ratio, not the pixel count

A representation swap (impostor, LOD tier) is supposed to change the picture, so
"the pixels changed" is not a defect and a capture A/B at one pose cannot decide
anything. Park the camera at a series of STATIONS across the switch distance —
stations, not a moving camera, because the capture path freezes the game and two
arms' `--capture-frame` calls never land on the same frame of a moving route.
Then compare:

* `move(ctl)`  — pixels changing between adjacent stations in the CONTROL. This
  is the yardstick: what the eye already accepts as "the camera moved".
* `move(cand)` — the same step in the candidate, which contains the swap.

**A swap step whose ratio to the control step is near 1 is invisible against
motion that is happening anyway; one far above 1 is a pop, and the ratio is how
bad.** Judging by the candidate-vs-control column alone is wrong — that column
is large wherever the cheap representation is showing at all, including where
nothing is popping.

Cover BOTH kinds of swap. Distance swaps need an approach route; a multi-view
billboard also changes view sector as the camera moves AROUND the object, and
rotating the camera in place does NOT test that (the view is chosen from the
object-to-camera-POSITION vector), so add an orbit at a fixed radius.

### Measuring a SKIP-WHEN-UNCHANGED change: three fixtures, not one

`--keep-routes` exists because the district's traffic is parked and a change
that skips work when an input did not change scores 100% on a still scene. The
camera is parked too, and that is the other half of the same hazard. The
reflection reuse budget (1.106.0) is the worked example, and the shape
generalises to any such change:

- **the parked fixture is the BEST case, and it must be labelled as one.** It
  answers "what is this worth when nothing is happening" and nothing else.
- **a MOTION fixture answers what it is worth in play.**
  `authoring/reflection-probe-2026-09-16/motion-sampler.py` replaces the
  sampler's four poses with four camera regimes (idle / straight / a gentle
  turn / a hard turn), keeping the 1440-frame window and the
  `district-benchmark.csv` "safe to write now" signal. Its result was the
  finding: the saving falls as the camera turns faster and reaches ZERO in a
  hard turn, which is exactly the case the change would be rejected for.
- **an INVALIDATION fixture answers the question that actually decides it** —
  does the skip ever hold something it should have thrown away?
  `content-sampler.py` parks the camera and changes the SCENE instead (an
  object hidden and shown, an object sliding, a day/night flip), so pose drift
  is zero by construction and only the invalidation can force work. Give it a
  regime where NOTHING changes as its own control: if that one does work, the
  other three prove nothing.

Two rules from it worth stealing. Predict each regime's number before the run —
"one capture per toggle, 120 of 120 for a thing that moves every frame" is a
sharp prediction and a shape is not. And state the quality cost in the unit the
change is written in and record it FROM INSIDE the game: this one reports the
worst staleness it actually permitted, in pixels of the reflection's own
128-pixel target, so the acceptance criterion is a measurement rather than a
screenshot.

### Devkit cadence overrides

Round-trip all five cadence fields, including missing/default and out-of-range
values. Generate automatic, overridden, disabled and release variants; overrides
must only gate host I/O, never graph execution or replay/pad updates. Exercise
Preferences input/save/reopen, compile and boot the generated debug game, verify
snapshot frame deltas, then halt/resume and capture at a slow command interval.
Check longer report intervals do not repeatedly flag a live game as hung.

## Full-asset gate for performance fixtures

An isolated native build is not an asset bake. A copy excluding bin and
.res-baked must first materialize the complete baked resources (normally with
an editor build), then add instrumentation and use native-build directly.
Before timing, compare deployed PNG/TMDL/MTL files with the intended baked
reference, reject missing/different resources, verify fresh frame IDs and
inspect actual GS captures after the sampling window. A September 14 fixture
with only sfx/vehicles directories lacked 66 PNGs and 11 TMDLs: placeholders
and skipped models made matching baseline/candidate triangle counts falsely
reassuring. See docs/performance-hardware-recheck.md. Empty redirected host
logs or a failed ping alone do not prove a failed boot; check new telemetry.

**And the GENERATED SOURCES are half of that gate, not just the assets.** A
fixture script that copies an example copies its **committed** `inc/*.gen.hpp`,
`src/gen/` and `src/terrain_game.cpp`, and examples' generated files in this
repo drift silently (that is a standing condition, not an accident). Compile
that with `native-build` directly - which every instrumented fixture does,
because an editor `--build` would regenerate the instrumentation away - and you
are measuring **a different game from the one the editor under test produces**,
with nothing in any log saying so. Measured on `examples/vehicle-playground`,
2026-09-15: a stale fixture reported **12.17 texture re-uploads per frame** in
garage day and **3.6 ms more render submission** than the same scene
regenerated with the editor being compared, which records 0.000 re-uploads and
0 evictions in all four poses. An entire GS VRAM investigation was launched at
that ghost. So `--build` (or at least `--refresh-gen`) the fixture once before
instrumenting it, and **check a counter against the previous known-good run
before concluding anything from a change in it** - a number that moved because
the fixture is stale looks exactly like a regression.

**AND `--refresh-gen` IS NOT ENOUGH ON ITS OWN, because the EDITOR BINARY can be
older than HEAD.** `build/tyrax-editor.exe` is a build artifact, not a property
of the checkout: a worktree sitting at the branch tip can hold an exe compiled
hours earlier, and `--refresh-gen` will then faithfully regenerate the fixture
with the OLD baker while every log line says "regenerated". Measured on the
EE-submission probes, 2026-09-16: the binary was built at 15:17 and the commit
that raised the VU1 package ceiling landed at 17:07, so seven arms were baked
with `stripRun = 72u` against a tree whose `meshstrip::kRun` reads 75 - a legal,
self-consistent fixture that is simply not the one the branch-tip table
describes.

**The capture hash does not catch it, and believing it does is the trap.** The
strip run changes how a surface is cut into runs, not which pixels it covers, so
the garage-day frame hashed to exactly the expected value. Check the things that
are actually functions of the baker:

```bash
grep -E "ROADSTRIP|TERRAINSTRIP" <results>/host.log   # packages 470 / 588 verts, 8 pkgs at 75
grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp  # the baked constant itself
```

Compare the binary's mtime against the commit that last touched what you are
measuring (`git log -1 --format=%ad <path>`), and rebuild the editor when in
doubt. A round whose arms all share one wrong fixture keeps its DELTAS - that is
what saved the probes - but loses the right to be quoted against anyone else's
absolute numbers.

**One more comparability trap from the same round: `--profile debug` leaves the
LIVE TOOLS ON, and two of their pollers run inside the instrumenter's `update`
bracket.** `livepad::tick` and `livedbg::tickFromLoop` `fopen` `livepad.bin` and
`livedbg.cmd` over `host:` every frame - network I/O inside the measurement.
Measured on the same fixture, garage day: **4.79 ms of `update` and 6.44 ms of
`work`**, which is why that control read 36.51 ms against a published 30.42 for
what a reader would assume was the same scene. `benchmark-district.py
--profile quiet-debug` is the same build profile with `liveLink`, `liveDebug`,
`liveLogic`, `timeMachine`, `remotePad` and `inputRecorder` off; with them off
the two tables agreed to 0.35 ms of `work` and 0.00 ms of `total`. **Say which
profile a frame-time table was taken on, or it cannot be read against another
one** - and treat sporadic multi-millisecond `update` outliers as host-I/O
contention rather than as a finding.

## Hardware timeline capture

Use tools/hardware-trace.py arm PROJECT before a boot, then export the complete
bin/hardware-trace.csv to HTML/Perfetto. The engine captures bounded RAM events
without new drains and writes after sampling. Scope totals overlap; VIF1 DMA
wait is not VU1 execution, and VIF/GIF snapshots are not utilization. Compare
unarmed/armed controls and reject dropped or stale events. See
docs/hardware-profiler.md for start-frame semantics and capture limits.
For debug builds, inspect per-frame `Live_debug_poll` and `Live_debug_flush`
before blaming gameplay update spikes. An unattached debugger should write its
one boot marker but no periodic flushes; a valid command attaches it and resumes
the configured report cadence.
Vehicle projects expose `Vehicles_update`, `Vehicle_smoke_update` and
`Vehicle_skids_update`; nested `Vehicle_sleep` events count parked instances
that skipped the expensive ground/collider/suspension path.

### Bounded hierarchy and observer controls (1.170)

The default `TYRA_HARDWARE_TRACE=1` retains the opt-in boot feature. A missing
config leaves compiled runtime guards; it is not the same as a build with
`TYRA_HARDWARE_TRACE=0` consistently defined in engine AND generated game.
Compiled-out APIs are inline no-ops with `constexpr active=false`: there must be
no trace clocks, register reads, file access, allocation or implementation calls
in the optimized path. Host symbol/disassembly checks support this contract;
check the target build too before claiming target dispatch cost.

`hardware-trace.cfg` accepts `first frames states [detail=0] [capacity=8192]`;
first is an absolute zero-based engine-loop index, frames is1..32, capacity is
128..32768, and the remaining input must be whitespace only. Coarse capture
reserves Frame/Pad/Game/Info, refreshed generated Update/Scene, submission,
existing completion waits and pacing. Detailed capture adds object/package,
snapshot/preflight/chain-copy/fixup/mutable-copy, native sizing/emission and
cache-writeback scopes. Register snapshots remain an independent option.
Refresh generated sources: an Update recorded only AFTER its children does not
establish their parent hierarchy. Do not insert additional fences for tracing.

`Scope(name, Kind::Span, false)` is explicitly coarse; the default third argument
is true (detailed-only). `Scope::finish()` is idempotent and destructors close
early returns. Labels must have static lifetime. The recorder is main-EE-thread
only. `reserveCapture(capacity)` retains storage across runtime controls;
`configureCapture(first,frames,states=false,detail=false,capacity=8192)` arms a
future window, `disable()` preserves storage, and `release()` frees it. Use these
outside active capture. `loopIndex()` identifies the current engine loop; when
arming from game code, beginFrame has already run, so use loopIndex()+1 or later.
Inactive `context()` returns unknown job/context fields, not stale metadata.

The editor keeps the exact five-column `hardware-trace.csv`. Use
`hardware-trace-v2.csv` or the per-window `hardware-trace-v2-<first>.csv` sidecar
for explicit event/parent IDs, kinds, source jobs, sequence, context,
requested/effective decision and clock epoch. END carries drops and invalid
hierarchy/clock counts. Reject incomplete, dropped, invalid, stale or
source-mismatched captures. Scope parents are reserved on entry; compatibility
raw-duration records have kind=legacy and unknown parent, so they remain
inclusive-only and cannot enter the exclusive ledger. Default8192 slots use
about480KiB on EE; log actual allocation size. Event density depends on detail
and fixture. The private32-frame/8192-slot detailed emulator capture overflowed
and was rejected; revised calibration uses8 frames/32768 retained slots.

For startup reservation cost, keep one instrumented ELF and all trace hooks
inactive, read a private explicit 0/1 reservation config before game assets,
and compare fresh boots 0/1/0. This controls code layout while allowing asset
heap placement to change. The private ring-only fixture uses three 1,800-frame
blocks, 128 independent samples each, and state/address inspection outside the
window. Native and both emulator settings validate structure; physical cost
requires actual boot controls. Declared state digests with live physics dt do
not prove full vehicle-state or pixel equality. See the ring control evidence
linked from `docs/hardware-profiler.md`; do not call this compiler-pruned hook
overhead or add its result to overlapping scope totals.

Job0 and contextUINT32_MAX explicitly mean unknown, including a pending source
from before capture. A capture-generation stamp prevents prior-window job IDs
from aliasing new ones. `record_job` follows the current EE recorder;
`present_job` identifies the ACTUAL CPU presentation source and may differ while
previous-job waits nest inside the current recording scope. Never infer N-1
from frame numbers. The source is not universally the display latch: triple
buffers may queue for a later existing ISR; no ISR or TV photons are measured.
Requested pipeline mode differs from effective queue recording after fallback.
Scope self-time is elapsed EE time, including preemption and unclassified waits,
not useful CPU instructions or EE/VU/GS utilization. Snapshot points are not
continuous hardware occupancy. Some bank-reclaim waits remain inside enclosing
work; unlabelled time is not proof of scalar math.

Price the observer with matching off/coarse/off/detail/off/detail+states/off
arms in ONE instrumented ELF, storage reserved in ALL arms, identical warmed
scene/camera/replay/clock/order/window identities, and common independent clock
samples. Export runs after capture and can delay its enclosing/following loop:
exclude the export-bearing sample in every arm. The current private8-sample
fixture therefore retains7 comparable samples. Small single-boot windows are
exploratory; repeat boots and use both adjacent controls. Independent sampling
and FrameProfile also cost work. A separate compiled-out ELF changes layout and
held heap, so its delta is not pure dispatch cost. Unchanged paced wall time is
not free instrumentation; never subtract a universal overhead constant.
**No physical timeline-v2 observer overhead is accepted yet.** Require complete
valid repeated controls before adding that claim.

See [the timeline hierarchy/observer protocol](../../../docs/hardware-profiler.md#hierarchy-and-observer-controls-1170)
and `tools/hardware-trace-analysis.md`. Export V2 with
`python tools/hardware-trace.py export PROJECT/bin/hardware-trace-v2.csv -o REPORT`;
`controls MANIFEST.json -o REPORT.json` validates declared matching controls.

### Native hardware timeline (1.92)

`src/hardware_timeline.cpp` reads the same bounded CSV as the offline exporter.
Debugger > Hardware timeline arms the next boot and loads completed captures on
demand, with frame selection, zoom, raw marker tooltips and inclusive totals.
No browser, Python or extra debugger polling is required. Engine detail scopes
separate package creation/classification, qbuffer copies and packet construction.
See `docs/hardware-profiler.md`; use unarmed controls to rank performance.

### Retained static command data acceptance (1.96)

The A/B is **one knob in one worktree, one project directory**:
`TYRA_STAPIP_RETAINED_COMMANDS` in
`vendor/tyra/.../static/core/stapip_qbuffer_renderer.hpp`, 1 against 0. The
engine is rebuilt by the ordinary `--build` because the header is an engine
source; the generated game, the assets and every other number are common mode.

Three checks, in this order, and the first two are the ones that mean something:

- **The packet must be byte-identical, so the picture must be.** Frozen camera,
  `--capture-frame` three times per arm, confirm the three repeats of each arm
  are byte-identical to each other BEFORE comparing arms. Anything non-zero
  across arms is a bug in the capture-and-replay, not a rendering difference to
  interpret - the change writes the same bytes to VIF1 by construction.
- **The counts must not move.** VU1 packages, submitted vertices and packet
  flushes come from the same bags producing the same packets, so `FTCLIP`'s
  `verts` and `flush` and the frame-cost CSV's submission count must be
  identical between arms. A moved count means a bag stopped being submitted.
  The new counters are `STAPIPRET retained=N rebuilt=M per frame, cache=K KB`
  (a debug engine logs them every 300 frames) or
  `StaPipCore::takeRetainedCommandHits/Builds` from the game.
- **Then, and only then, the time.** And **PCSX2 cannot price this change**: it
  emulates no EE data cache, while the change trades computing bytes for
  reading them out of a cold ~128 KB arena. Its delta is an upper bound on the
  hardware saving and must be labelled as one.

Stress the invalidation deliberately - it is the whole correctness surface.
Force texture evictions with a batch pending, switch the pipeline away and
back, cross an LOD threshold in both directions (a tier swaps the bag's vertex
pointer AND count), reload the scene, and move the camera over hundreds of
frames. `rebuilt` per frame is the instrument: it should be near zero on a
parked pose and spike exactly where geometry changed.

### The content-version contract, and its negative test

`BagArray<T>` (docs/bag-content-version.md) is what makes the baked stream's
invalidation structural instead of a rule: every array the generated game hands
a `StaPipBag` is one, `data()` is const, and every mutation stamps
`contentVersion`. **The whole argument for that design is that a raw write does
not compile, so the check is a NEGATIVE one and it needs no PS2 toolchain:**

```bash
tools/bag-array-enforcement.sh <project>/inc/bag_array.gen.hpp
```

Seven cases against the REAL generated header (`TYRAX_BAG_ARRAY_NO_TYRA` drops
only the four `bind()` overloads): two positive - the sanctioned API compiles,
and the stamp MOVES on every mutation - and five escapes that must each be
refused. **Falsify it before believing it**: make `data()` non-const in a copy
of the header and it must go red on exactly the two cases that property guards
(measured: `passed 5, failed 2`). The positive control is load bearing too - a
header that failed to compile at all would "pass" every negative case.

Two traps this conversion produced, both of which read as something else:

- **`bind()` aims the bag at `data()`, and an EMPTY vector's `data()` may be
  null**, which the engine rejects with *"Vertices are required in 3D render
  bag!"* at the first frame. A raw C array could not have that problem and could
  not carry a stamp either; size the ring before binding. Size it before the
  FIRST updater/builder too, not merely before the lazy bag setup. Vehicle
  smoke clears dead slots, skid geometry may spawn in the physics update, and
  vehicle glow writes its lit-lamp geometry before checking whether its bag
  exists. Indexing or spanning those zero-length arrays called `Vec4::set`
  through null. PCSX2 hid the write because its RAM starts at zero; hardware
  reported cause 3, `BadAddr 0`, immediately after the loading screen.
- **A new generated file must join `refreshGenerated`'s allowlist, not just the
  `--new` scaffold.** `inc/bag_array.gen.hpp` was written at creation and not on
  refresh, so every existing project - and every performance fixture, which is
  how it was caught - regenerated code that needs it and then failed to compile.
  Same silent shape as the `live_pad.gen.cpp` mistake the allowlist comments
  already name.

### Baked VIF stream acceptance (TYRA_STAPIP_BAKED_STREAM)

Same shape as the retained-command A/B above and one extra rule.
`TYRA_STAPIP_BAKED_STREAM` in the same engine header, 1 against 0, one project
directory, plus `TYRA_FRAME_PROFILE` and `TYRA_STAPIP_BAKED_REPORT` set to 1
in **both** arms (they are measurement flips - revert them before committing).

- **The gate is the picture plus three counters**, and the counters are free:
  `FTCLIP` gives `cull`/`clip`/`guard`, `verts` and `flush`, and `STAPIPBAKE`
  gives `chainQw` - the DMA chain quadwords the static pipeline hands VIF1 per
  frame, which is the number the change exists to move and is compiled into the
  control arm too. Nothing here is a millisecond, and a PCSX2 millisecond about
  this change would not be admissible anyway.
- **That counter gate only works while the flush cadence is held fixed, and it
  is the reason the spike could not deliver the prize.** A run of packages under
  one `REF` tag cannot cross a packet flush boundary, so pinning `packetFlushes`
  pins the saving. Any change that moves the cadence must use the redesigned
  gate instead - docs/baked-stream-acceptance-gate.md: a canonical, NOP-stripped
  hash of the word stream VIF1 actually receives (decoded at the packet tap,
  with texture mutations interleaved in order) plus byte-identical pixels over a
  pose sweep. It constrains what the GS is given without constraining the chain
  that built it. The `FTCLIP` routing counts become **diagnostics that explain a
  hash failure**, not gate conditions.
- **A gate arm is never a timing arm.** The hash reads ~2.7 MB a frame and costs
  10-25 ms; that is fine, because it is a separate ELF from anything you time.
  Do not read a millisecond off a build that carries it.
- **Drive the Motor District fixture by its pose file, never with `--pad`.**
  `benchmark-district.py`'s sampler reads `bin/district-benchmark-pose.txt`
  every 30 frames **after** its 1440 measured frames, and it writes
  `bin/district-benchmark.csv` at exactly that moment - so the CSV appearing is
  the "it is safe to write to bin/ now" signal. Phase 0 is garage day, which is
  the only pose to compare pixels at (the night poses have authored lamp flicker
  and twinkling stars and never settle).
- **Launch PCSX2 yourself** (`pcsx2-qt.exe -batch -nogui -logfile <yours> -elf
  <abs path>`), never `--build --run`, which reaps other worktrees' emulators -
  and delete `bin/livedbg.cmd` and `bin/frame.tga` before the boot and between
  captures, or the first `--capture-frame` reads the previous arm's file.
- **The stale-libtyra trap has a specific shape here**: the feature is an engine
  header, so the ELF only moves if `libtyra` was re-archived. Check the build log
  for `Engine sources changed - rebuilding libtyra...` followed by an
  `elf-ar rcs bin/libtyra.a` line, and `sha256sum` the two ELFs - they MUST
  differ. A null A/B is only evidence if the two arms were two builds.
- **A MATCHING CAPTURE HASH IS NOT A FIXTURE CHECK, and on this fixture it will
  actively mislead you.** This round's first pass borrowed the editor binary
  from another checkout, which predated the 72 -> 75 package ceiling, so the
  fixture's generated `src/terrain_game.cpp` came out with `stripRun = 72u` and
  the roads and terrain were cut into 72-vertex runs: `cull=40175 ...
  verts=55332` instead of `38750 ... 55836`. Its garage-day capture STILL hashed
  to `415f970f...73f1e`, the value `package-ceiling-75-2026-09-16` published for
  that pose - because **both** of that round's arms hash to it, the strip run
  changing how a surface is cut into runs and not which pixels it covers. So
  check the SCENE, not the picture: `ROADSTRIP scene 0 ... packages 470` and
  `TERRAINSTRIP ... vertices 588 packages 8` in the game's `bin/log.txt`, plus
  `grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp`. And build the editor
  from the worktree you are measuring, every time - `./build.ps1` is 6 minutes
  and a wrong fixture is a day.
- **`STAPIPMISS` says WHY a bag rebuilt**, which is what turns "the cache churns"
  into something actionable: one counter per invalidation reason (`bbox`, `prim`,
  `streams`, `program`, `size`, `new`, `incomplete`) plus the biggest bag that
  moved, by vertex and package count. On the garage-day pose it reads
  `bbox=1 prim=2` per frame and zero everywhere else, and on the outer-road pose
  it reads zero everywhere - so the design converges and the garage contains
  three callers that lie to it.
- **`STAPIPMISS` splits `bbox=` from `content=` since the content-version
  contract**, and the split is the point: `bbox=N content=0` is a mesh that
  MOVED, `bbox=0 content=N` is one that was RE-SHADED. Both used to read as
  `bbox`, which is why "the cache churns" was never actionable. At garage day
  the district now reads `bbox=300 content=600` over 300 frames and names the
  2 280-vertex bag - the two cars' paint pass.
- **THE ADVERSARIAL ARM IS THE ONE THAT FINDS REAL DEFECTS, AND IT NEEDS NO
  CONTROL.** `TYRA_STAPIP_BAKED_VERIFY` rebuilds every block with the ordinary
  writers and compares, so it runs under `--keep-routes` with the traffic
  MOVING - the one fixture where a missing invalidation can fire, and the one
  where an A/B is impossible (two arms never share a frame). `failed=0` is the
  acceptance. Measured 2026-09-17: **169 843 blocks checked, `failed=0`** over
  ~12 600 frames. **Enrich its MISMATCH line before hunting**: the program
  class and first differing quadword alone cost a lot of guessing; the stream
  pointers plus the differing quadword's four floats named the caller in one
  run (three equal RGB lanes drifting by 0.012 is a camera-dependent shade and
  nothing else is).
- **`--profile quiet-debug` turns Live Debugger OFF, so `--capture-frame` does
  not work on that fixture.** The counters (`FTCLIP`, `STAPIPBAKE`,
  `STAPIPMISS`) do. For the pixel half of the gate, flip `"liveDebug": true` in
  the fixture's `.tyra` for BOTH arms rather than changing profile between them.

### Skip-when-unchanged acceptance, and the fixture that lies about it (1.100)

`benchmark-district.py` **strips every vehicle's route** (it pops
`vehicle.route` from every object JSON) so the fixture is deterministic. For
almost every change that costs nothing. For any change that **skips work when an
input did not change** it is a trap: every car is still, every frame, forever, so
a skip test that would never fire in a real district scores 100%. **A number from
the parked fixture alone is not evidence for such a change, and saying so is part
of the result.** Pass `--keep-routes` for a second fixture whose AI drivers are
driving (camera still frozen, player still pinned) and quote both — the parked
best case and the moving realistic case. The cost of the second is determinism:
with traffic moving the pixels are not repeatable, so run the `--capture-frame`
A/B on the parked fixture and take only timings and counts from the moving one.

The wheel-batch round is the worked example (`docs/wheel-rebake-skip.md`). Its
per-frame counters are `WHEELBAKE cars=N rebuilt=M wheels=W rebuilt=X batches=B
stamped=S per 300 frames` in the game's `bin/log.txt`, behind
`TYRA_WHEEL_REBUILD_REPORT`, **default 0** — it is a timed `host:` write, so it
is off for the same reason `STAPIPRET` is, and it must not be armed inside a
`benchmark-district.py` sampling window. Unlike `STAPIPRET` it lives in the
GENERATED game, so it is flipped in `src/templates.cpp` (or `-D` on the game's
own compile), not in an engine header. `stamped` against `batches` is the second
lever read directly: it is how many submits bumped `bboxVersion`, and on parked
traffic it should fall to zero once the pose settles.

**A FIXTURE GENERATED BY A STALE EDITOR BINARY MEASURES A DIFFERENT GAME, AND
NOTHING IN ANY LOG SAYS SO.** This is the morning's stale-generated-sources rule
wearing different clothes, and it is worse, because regenerating the fixture -
the documented fix for that one - does not help if the *editor doing the
regenerating* is old. Measured on this branch, same day: a control fixture
generated by an editor binary that predated the imported-model batching merge
carried `keyL=0` / `batchStatic=0` where the candidate's carried `keyL=3`, so a
wheel-code A/B silently straddled the batching boundary and reported **-2.949 ms
where the honest number was -1.838**, with a +682/+875 triangle difference that
sent two people hunting a phantom. **Rebuild the editor for the control arm too,
from the commit you mean, and diff the two generated `terrain_game.cpp` before
you measure** - the diff should contain your change and nothing else, and
`keyL=` / `batchStatic=` are cheap canaries. Separate fixture directories do not
protect you here: the arms really were two builds, they were two builds of two
different games.

### Pricing a CULLING change in PCSX2: counts, plus a batching-OFF third arm

A change to what gets *grouped* or *culled* is measurable in the emulator
without ever quoting a millisecond, and on this repo's rules that is the honest
way round: PCSX2 emulates no EE data cache, so its times mislead, while its
**counts and its pixels are exact**. The recipe that priced the static-batch
cell (1.102.1, docs/model-pipeline.md):

- **The counters are two log lines, not an instrumented loop.** Set
  `TYRA_FRAME_PROFILE` to 1 in `vendor/tyra/engine/inc/debug/frame_profile.hpp`
  (**identical in every arm, and reverted before committing** - it is a
  measurement flip, not a change) and the game prints `FTCLIP` every 50 frames:
  package and triangle counts per clipper route, `flush` (packet flushes),
  `verts` and `out`. Divide by 50 for per-frame; total VU1 packages is
  `cull + clip + guard + out`. The generated game prints `Static batching:` at
  scene load for the batched/solo split. No `instrument-frame-cost.py`, no
  patched `loop()`, and nothing to regenerate away.
- **RUN A BATCHING-OFF THIRD ARM, and make it the reference.** Two arms tell you
  a change moved; they cannot tell you which one is *right*. `staticBatching:
  false` in the `.tyra` renders the ground truth. That third arm is what turned
  "the new cell is cheaper" into the much stronger "the new cell is
  byte-identical to no batching at all, and the OLD one drew 400 pixels of props
  the unbatched scene culls" - a visible defect nobody was looking for.
- **A steady count is not a steady picture.** Check both gates separately. The
  adversarial fixture for that round had counts identical to the digit across
  four consecutive `FTCLIP` windows in both arms while its *captures* differed
  by 283-419 px **within one arm**, so its numbers were quotable and its pixels
  were not. Report which gate a fixture passes rather than assuming determinism
  travels from one to the other.
- **Watch for the DOMINATED case, because that is the one that can only lose.**
  Batching on a big map was worse on both axes at once - more triangles *and*
  more packet flushes - which needs no hardware to condemn. With the cell fixed
  the same feature became a pure win on one map and a genuine trade (+23.5%
  triangles, -43% flushes) on another. "Worse on every axis" is a verdict;
  "worse on one, better on another" is a question for the console.
- Usual hygiene, all of which this round needed: short paths outside the repo,
  one PCSX2 launched per arm by hand (never `--build --run`, which reaps other
  sessions' emulators), `frame.tga` deleted between `--capture-frame` calls or
  the next one reads the previous file, and a DAY pose on the district because
  the night ones flicker.

**HASH THE TWO ELFs BEFORE YOU BELIEVE A NULL RESULT.** The cleanest way to
A/B a codegen change is one project directory and two compiles, swapping only
the generated `src/terrain_game.cpp` between them - it removes the fixture, the
assets and the scene as variables in one move. It also has a trap that returns a
*perfect* null and looks like a clean result: **`Copy-Item` (and `cp -p`, and
`git checkout` of a file) carries the SOURCE's timestamp across**, so the
swapped-in file can be OLDER than the object `make` already built from the other
arm, `make` does nothing, and both arms run the same ELF. Measured here: two
arms whose sources differed by 332 lines produced **byte-identical ELFs**, and
every routing counter in all four poses read a delta of exactly 0.0 - which is
indistinguishable from a real null and was nearly reported as one. After
swapping, stamp the file (`(Get-Item $f).LastWriteTime = Get-Date`), delete the
matching `.o` in BOTH the project and the `-Cache` tree, and then **hash the two
ELFs and check they differ**. A null A/B is only evidence if the two arms were
two builds. (An `instrument-frame-cost.py` pass happens to rewrite the file and
bump its mtime, which is why that flow does not hit this.)

Two correctness checks this class of change needs and a timing A/B does not:

- **A skip must be a NO-OP, not a cheaper approximation.** Hold the old code as
  an oracle and compare the live buffer against a full rebuild *every* frame,
  including the frames that skipped. The failure mode is one frame of staleness,
  which a still screenshot cannot see and a frame-time table rewards.
  **Run that oracle INSIDE the game, not only in a host harness.** A host
  harness checks the inputs you thought of; an in-game one checks the inputs the
  game actually produces - a driving car, an LOD crossing, a rig entering and
  leaving the batch - and it reports a number instead of asking you to squint at
  a PNG. The wheel round's is `TYRA_WHEEL_REBUILD_VERIFY` (default 0; it costs
  more than the work it checks, so it never ships and never goes in a
  measurement): it re-derives every drawn car's wheels with the PRE-CHANGE
  arithmetic and byte-compares, printing `VERIFY checked=N STALE=M`. Do not
  reshape the hot path to share code with the checker - independent code catches
  more, and a refactor invalidates whatever the console has already measured.
- **Anything hoisted out of a loop must be BIT-identical, not equivalent.** A
  reassociated rotation differs in the last place and that is a moved pixel. Do
  the trigonometry once but keep the same operations in the same order on the
  same operands, and prove it over a wide random spread including the degenerate
  branches - a native harness that links neither the editor nor the engine does
  this in seconds and costs no emulator boot.

### Static submission batch acceptance (1.93)

Use complete baked resource hashes, four parked day/night poses, warmed
unarmed frame-cost rows, a second baseline boot and GS captures. Texture
residency must be stressed separately: evict while a batch is pending and
switch the pipeline away/back, then verify fresh frame progress, reuploads
and intact textures. Night images contain animated star twinkle and lamp
flicker; compare repeated same-build captures before calling a pixel delta
a renderer regression. A lower DMA count alone is not acceptance: the larger
bag prototype reduced sends but increased pipeline waits. See
docs/static-submission-batching.md for measurements and the evidence recipe.

### Hull and reflection proxy acceptance (1.111.0)

For a hull proxy, bake a concave multi-material OBJ through the Properties UI,
resave, refresh generated assets, and inspect the emitted OBJ: one `usemtl`,
`4n-4` faces for its convex XZ ring, and a non-billboard far assignment. Boot
near/far poses and verify collision/picking identity from generated scene data.
For material consolidation, use a fixture with repeated `usemtl` groups that
resolve to identical state and inspect `.tmdl` part counts; include a captured
4/8/16-view impostor as the negative control whose ordered parts must survive.
For a reflection box proxy, compare the per-producer frame inventory and a
console self-capture with only `reflectionProxy` changed. The env-probe object
row should fall to one bag / one package while the main-view rows and generated
collision fields remain identical.

For automatic road junctions, cross two roads with the same intersection
texture, one road with no value and one pair with different values. The first
pair alone must produce a `ROAD_JUNCTIONS` row, with 12 vertices / four triangles
on flat ground. Since 1.151.2, uneven junctions refine conformingly on the host
(up to 256 triangles), then ship baked XYZUV in `ROAD_JUNCTION_VERTS`.
The viewport and PCSX2 patch must match. `verify-road-twins.py` also exercises
terrain folds and both Market endpoints, proves the old fan regression is
triggered, sweeps clearance and checks nonempty runtime junction uploads. Move one spline
off the crossing, resave and refresh: the row must disappear. Always rerun
`verify-road-twins.py`. For the physical-PS2 stretched-road regression, inspect
the generated road STs first: each chunk's V must begin in `[0,1)` and stay
bounded by roughly that chunk's nine repeats. Then park the same camera on
hardware and confirm lane markings survive; PCSX2 is only the topology/image
regression arm because it never reproduced the original smear.
Road strips ship on since 1.117.2. `ROADSTRIP ... strips 1` is expected; use
`TYRA_STRIP_ROADS=0` only as the list control. Across an A/B, compare the
producer's surface-triangle count (it must be identical), then packages,
vertices, synchronized frame costs and several physical-console road views.

### Read live bbox counters for per-object attribution

`StaPipCore::takeTelemetry()` folds `cacher.stats` into `telemetry.attrib`
and resets it. Peeking `telemetry.attrib.bboxCache*` around an individual draw
therefore yields zero deltas even when that draw replaces cached boxes.
Instrumented per-object probes must snapshot the live `cacher.stats` as well
as the live telemetry brackets. Frame exports taken via `takeTelemetry()`
remain correct. The first three 2026-09-28 causal arms retain those deferred
per-object zeros; the cache control/candidate/repeat arms fix that reader and
identify two fresh replacements per frame in each of Ravager and Pica.
Keep each A/B group on one ELF with a boot-only switch; report instrumented
times separately from the production 23.923 ms baseline. The archived probe
patches and complete captures are in the example's `night-entry-hardware-2026-09-28/causal/`.

### Accept a bbox-cache change against fresh canonical bounds

The 2026-09-28 count-variant acceptance compares cached main boxes, every
child box, merged min/max ranges and coarse bounds to freshly constructed
`StaPipBagPackagesBBox` instances on physical PS2. It also checks repeated
full/prefix requests, content-version bumps, hundreds of changing counts,
different VU package sizes, recycled-address versions, full expiry and hash
index reconstruction after partial expiry. The oracle is a correctness arm;
its timings cannot be used as performance data. The shipped engine keeps
`TYRA_STAPIP_ATTRIB=0` and carries none of the oracle/boot-toggle/raster hooks.
See the example's `night-entry-hardware-2026-09-28/fix/README.md` for patches
and the 3,470 lifecycle cases plus 48,803 live requests.

Scenario overrides must run after the initial scene creates vehicle instances,
at the simulation boundary. A first-loop override can see an empty array and
be replaced by `bootFirstScene()`. Export traffic positions/speeds and reject
a nominal parked control if another car moves; the first acceptance fixture
caught exactly that error. Continuous driving windows are selected by actual
position, not a presumed frame count. Quality-reduction probes may change FPS
and thus physics substep cost; compare render/finish as well as total work.

## Exact first-entry HUD acceptance (1.150.1)

Use the stationary night fixture and record every frame through the exact
driver transition. Compare one ELF with a boot-only old/new HUD gate and
repeat the control. A config read must be guarded by the one-time init result,
not merely follow a helper whose internal `done` returns false on later frames:
otherwise host I/O happens every frame and the timing control is invalid.
Check single-init logs, identical ELF hashes, driver/speed/traffic state and
aligned CSVs. Keep first-entry work/HUD/uploads separate from warm medians.

For lifecycle acceptance, revisit scenes, count atlas copies and check sprite
and texture identity; evict VRAM and reload normally. Plain and unknown-token
strings must not request the icon sheet. A valid icon must load once and reuse
its sheet thereafter. Run these loading/render checks after timing, never
inside the benchmark window. Compile a non-vehicle orbit project too. Physical PS2 measurements are authoritative; emulator boots and
images are correctness evidence only.

For the HUD lifetime probe, mutate VRAM inside an active render frame after
scene submission and before frame end, then test resolved icons in ordinary
HUD frames. The between-frame forced mutation stalled at the barrier in
PCSX2; do not count that or a blocked forced scene transition as passed
lifecycle evidence. Repeated font preparation is distinct from full scene
revisit acceptance. Correctness-only -O1 harness timings are never hardware
performance evidence.

## Road editing (1.151.0)

Road loops repeat the first XZ pair at the end of `roadPoints`; `roadgen::isClosed`
and `controlCount` hide the seam sentinel from editing. The host and generated
`buildRoads` wrap Catmull-Rom neighbours and final tangents together. No format
field was added. `moveControl`/`removeControl` preserve closure and minimum counts.
Properties has collapsible Crossings/Points and a reversible Closed loop toggle;
viewport Shift-click deletes, endpoint snap closes at release, one undo each.
Outlines reuse `Viewport::RoadDraw`; full-width soft borders retain a separate
outline. During viewport drag only, both crossing caches defer work until release.
Verify loop seams and open-road parity with the vehicle-playground road twin
oracle; UI scripts can target `Road point N` handles and use `shiftclick`.

## Vehicle defaults and chase-camera acceptance (1.153.0)

Resave a legacy definition and verify its tuning stays local. In a scratch
project, change global accel, damage and nosCapacity; verify inheritance, local
accel and zero-capacity overrides plus a silent sounds override survive save/load
and reach VEHICLE_DEFS. Global defaults must work without vehicles. UI scripts
can target Global defaults, Inherit global defaults and Has nitrous. Boot a
third-person Player beside a car, press Use, verify VEH avatar 0 while seated,
then exit and check visibility/door placement. A blocker behind the chase boom
must reduce boom100 below want100; removing it must restore the distance. Include
rotated boxes, terrain, collision-none, self exclusion and far/rear views. The
clearance pass runs after shake, not only on the unshaken rig.

Vehicle Editor UX: use an isolated vehicle-playground copy. Assert three car rows, `Vehicle preview canvas`, `Spin wheels`, and `Listen to engine`; set Speed above/below the fast-wheel threshold, orbit/zoom, capture normal/fast/steered previews for all three cars, and check audition stays checked (device/decoder opened), updates with Revs, and stops on selection/close. In Cost, attempting to change a triangle slider before the advanced checkbox must leave the saved budget unchanged; unlock and verify edit/save/reload. Reflection texture choices are thumbnails with Dynamic sky and Import PNG. Host audition differs from SPU2 ADPCM; a native build verifies the new idle WAV encodes as a loop.

Vehicle inheritance: create a new car, save and assert no tuningOverrides plus
resolved global fields. Edit a single Driving/Driver/Sounds parameter and check
only its field key is added. Change a global sibling field and check it reaches
all cars while the edited field survives. Use defaults must remove only the
current section's overrides, then survive save/reload. Pre-83 local/group files
must preserve authored values, including empty sounds and zero volumes.
For tyre reduction, test @auto at 32 triangles on all three models, inspect
actual cost and the continuous outer tyre, then test authored wheel_blur at
speed and from both sides. None/@auto must still detect exactly four wheels
when the source GLB contains wheel_blur. Verify original GLB mesh/material/image
prefixes remain unchanged and repeat authoring is byte-identical.

Vehicle sound import: select an ordinary WAV without -loop.wav in idle/high-rev
and tyre-squeal dropdowns, save/reload and audition. Headlights must appear only
in Effects; its Use defaults resets the Effects override without resetting
Sounds. Verify optional vehicle_sound_loops.gen.txt is rewritten/deduplicated
and deleted when the last ordinary continuous role is removed. Check native
and Docker encoder header byte 6 through one-shot -> loop -> one-shot while WAV
mtime is unchanged; gear-shift-only sounds stay one-shots, suffix loops stay
loops. Include paths with spaces and an already-fresh encoded cache.


Sound effect conversion (1.157.0): `wavconvert::soundIssue` requires canonical
mono PCM16/22050; import and Project > Sounds > Convert downmix too. Runner calls
`wavconvert::bakeSounds` AFTER texbake and BEFORE either build backend, preparing
`.res-baked/sfx` without modifying `res/sfx`. Both encoders read this mirror,
check cached APCM byte 5 for mono as well as byte 6 for loop intent, and preserve
the source path for role matching. A conversion error must fail the build.
Test stereo PCM16, PCM24, float, extended headers and an odd metadata chunk;
verify duration, source hashes, incremental mtime stability and ADPCM mono/loop
headers in BOTH backends. Decode the actual ADPCM and compare the waveform to
the converted WAV: header checks alone missed adpenc's corrupt stereo reader
(`fread(wave+i, 2, ...)` advances by one byte). Music conversion stays stereo
unless its own mono option is chosen.

## Hybrid triple-buffer acceptance (1.153.0)

Use a frozen-camera scratch project with solid geometry and HUD. Boot hybrid
double/triple variants in the software renderer; compare the finished scene
captures and inspect GS dumps for CT32 draw at slot 0, CT16 copies to slots
1/2, DTHE on only during copies, and PSMZ32 depth. Check fresh frame progress
and absence of alternating black frames. Exercise limiter on/off, field mode,
a BLSS layout rebuild and a mode with insufficient headroom. In a mixed
project, use a scratch global script with `ctx.requestDisplayMode` to switch
field/full PAL at the normal pre-frame boundary, including while BLSS is
scene-disabled; check 3/2 buffer grants and fresh captures after both switches.
Regular 32-bit
triple buffering must still rotate all three draw targets. Verify the
Preferences checkbox can be enabled in Hybrid and persists after reopening.
Space repeated `--capture-frame` CLI calls by more than one second: their
command sequence is wall-clock seconds, so two calls in the same second are
seen as the same command. A PCSX2 run establishes correctness, not hardware
frame-pacing performance.

## Procedural placement validation (1.154.0)

Use a host harness to compare the spatial hash with an independent all-pairs
AABB rejection oracle, including rotated/off-centre assets, differently sized
models, clearance, cell boundaries and the giant-bound overflow. Check painted
islands inside a footprint, upper-layer occlusion, absent terrain, spline bends,
closed roads and road width/point edits invalidating bakeHash. Repeat evaluation
and baking, and verify serialization and the runtime capability rejection.
Drive Validate Placement's checkboxes and material picker through --ui-script.
Use vehicle-playground's procedural scene for refresh-gen and a game boot;
benchmark many candidates only in the host filter, not as a PS2 timing claim.

For road path filtering (1.155.0), compare origins against an independent
point-in-triangle oracle over straight, curved and closed road ribbons. Check
legacy roads=0/1, both road targets, no roads and missing/non-road targets;
Only must ignore canopy width but retain independent collision/material checks.

Procedural parameter combos now register their scoped labels for UI scripts.
Open Roads/Road/Material by name and assert the popup options; no coordinate
workaround is needed for these node controls.

## Frozen procedural bake acceptance (1.156.0)

Freeze a stale/unbaked scratch volume, then move roads, edit terrain/materials,
seed and graph parameters. Compare generated object JSON and mesh hashes before
and after both ordinary and forced bakeAll and refresh-gen. Save/reload and
repeat; anyStale must ignore it and explicit bakeVolume must refuse. Unfreeze,
assert staleness and rebake; output must change. Verify missing frozen defaults
off and equality notices the flag. Runtime freezing must fail without mutation.
Drive Frozen with --ui-script, save/reopen, inspect the checkbox and disabled
bake/clear/mode/instance controls. Capture the viewport too: frozen chunks must
remain visible without live instances, and Show preview must hide them.

## Scene object data iteration regression

After warming a scratch native game, move an authored object and refresh
generation. Compare the header and script timestamps: for a value-only move,
`scene_objects.gen.cpp` should be the only compiled source when other bakes
stay unchanged. Also test ordinary object additions/removals (which should keep the header
stable), scene-count changes, color edits, empty scenes,
baked scroller clones, both camera templates and a custom script that reads
`SCENE_OBJECT_TABLES`. Check old/new initializer rows for exact equivalence and
boot the resulting game. Test culling both enabled and disabled: static-box
additions must update its proxy data without changing the declaration header.
Feature and other derived-table edits may rebuild more. Profile a remaining
large compile with GCC `-ftime-report` before assuming parsing/I/O is its cost.


## TyraX2 foundations (2026-10-01)

See docs/tyrax2.md before changing cross-frame submission ownership.
TYRA_VIF1_CHAIN_CHECK is an opt-in correctness guard in Vif1Queue::submit;
StaPip and HUD callers pass full chain extents including END. It validates
linear TTE chains and VIF lengths before submission, with bounded EE RAM
reference resolution. It does not prove allocation lifetime, cache coherency,
GIF semantics or pixels. NEXT/CALL/RET, REFS and scratchpad references are
unsupported. Non-default inherited STCYCL state needs an explicit decoder
contract. Keep the guard OFF for timing; accepted/rejected VIFCHECK output is
correctness evidence only. tools/verify-vif1-chain.cpp exercises malformed
input on a host. tools/tyrax2-fixture.py creates isolated current Motor District
seated-start day/night fixtures with plain/timing/check/hold engine modes.
Refresh and build the returned game against its copied engine, keep one host
server, and use the resident-IOP marker. Preserve the authored video mode:
NTSC's 16.667 ms budget is not FRAMETIME's fixed over20 threshold.
The historical arena stages below preceded the integrated N/N-1 runtime; see the current default policy in the later section.


Experimental TyraX2 chain arena (2026-10-02): TYRA_FRAME_CHAIN_ARENA remains
OFF by default. It snapshots packet bytes and REF arrays into a bounded 1 MiB
allocation, preserving independent chains. Queue metadata grows without growing
producer buffers/copy pools. sourceCopied frees only the original source; retain
the actual DMA sequence for ordered fences (especially the HUD PATH3 fence).
Reclaim only on completed drain. Overflow drains/retries once; oversized or
unsupported input and allocation failure retain ordinary source waits. Test
with tools/verify-frame-arena.cpp and separate arena-check/arena-timing fixtures;
FRAMEARENA reports allocation/high-water/fallbacks. It is not cross-frame
execution or an accepted performance improvement yet.

R5900 arena copy uses aligned LQ/SQ quads under TYRA_FRAME_ARENA_QUAD_COPY;
with TYRA_VIF1_CHAIN_CHECK each copied span is memcmp-verified on hardware
before publication. Host tests exercise the memcpy fallback, not EE assembly.
The initial 1 MiB/libc-copy night arm regressed work from ~19.92 to ~34.02 ms;
do not ship it or infer a pipeline gain. Larger banks need explicit RAM and
full-frame-byte accounting before choosing two-frame storage.

Immutable baked VIF spans may be registered with the experimental arena to
avoid redundant frame copies. StaPipBakedStreams::retire unregisters and waits
for actual DMA readers before poison/destruction; two frame ticks alone are
not a fence. A full 512-entry registry and all unregistered/mutable arrays use
copy fallback. Validate eviction/replacement/scene teardown as well as a parked
view before accepting pinning for cross-frame recording.

Keep immutable span lookup logarithmic: the initial linear 512-slot scan
negated pinning's avoided ~2.15 MiB/frame and measured ~33.96 ms; sorted disjoint
ranges reached ~20.88 ms in the same warmed night window. The ~19.92 ms control
still wins, so ownership alone is not an accepted optimization.

frame_vif_writer.hpp is a native CNT/REF recorder with one END at finish,
bounded capacity and unsubmitted rollback. tools/verify-frame-writer.cpp
compares ordered VIF words/data with an independent oracle. It is integrated experimentally; hardware ordering and N/N-1 acceptance
is recorded in docs/tyrax2.md. Broader scene/mode coverage remains required.

Experimental TYRA_NATIVE_VIF_RECORD records owned sources as native CNT/REF
operations with one END per ordered segment. Foreign-path barriers still
flush; do not claim one full-frame chain or N/N-1 yet. Completion retires the
batch's last logical source sequence, zero is reserved across wrap, and prefix
reuse waits actual DMA. Reconstruct operations, never splice END/NEXT buffers.
Both lazy and eager paths must write back AFTER final prefix/snapshot writes.

TyraX2 ordered frame runtime: 1.171.0 / format v95 defaults new projects to
framePipeline=true and serializes the explicit bool. Missing v94-or-older
requests remain false; missing v95 requests default true. Explicit booleans win;
codegen emits setFramePipeline(true/false). The engine compile-time default stays zero.
Engine support is compiled but disabled projects do not allocate frame banks.
Ordering-only GIF/VIF waits use frame_submission.hpp; actual resource destruction,
readback and presentation retain real completion fences. Intermediate FINISH
commands become EOP-only while recording. Two banks each own a 1 MiB snapshot
arena and 128 KiB native prefix; snapshot inline data becomes REF, avoiding a
second copy. Keep logical recording target separate from the pending GPU job's
context. Complete/present the previous job before the new prefix starts, including
overflow, and protect the unsubmitted bank against that handshake's drain/reset.
Field mode and active BLSS use synchronous compatibility. Warp, mode changes and
permanent VRAM rebuilds complete pending jobs first. FRAMEPIPELINE busy-starts
prove actual overlap; ORDEREDFRAME split counts must stop increasing in an ordinary
warmed fixture before claiming one-chain frames. See docs/tyrax2.md for acceptance
and honest timing limits; early work counters included presentation pacing.

TyraX2 acceptance also covers deferred immutable-bank retirement, a borrowed
oversized first transfer, mode changes with a pending job, limiter and runtime
opt-in toggles, and between-frame synchronizeFrame readbacks. Triple buffering
without a limiter is a synchronous compatibility case. Presentation pacing is
excluded only while a beginFrame/endFrame measurement window is open; do not
subtract a between-frame readback's pacing from the next recording window.

The final pipeline-check/pipeline-timing fixture modes enable the public
settings.framePipeline API after game initialization; diagnostic arena/native/
ordered/pipeline defaults stay zero. Plain and earlier-stage fixture modes
explicitly disable the project preference, even if the copied project enabled
it. This keeps a baseline control from silently measuring the new pipeline.


Final 1.169.0 acceptance is in `docs/tyrax2-runtime-2026-10-02.json`.
Windows and Linux ASan/UBSan passed 1,074 arena cases plus 10,000 malformed
streams and 43 writer-oracle cases. The final guarded driving fixture switched
all three Motor District scenes, reached 3,360 frames/233,472 accepted source
chains with zero rejection, four split frames and no direct fallback. Public
Live Debugger readback passed with the runtime preference on. Native and Docker
PS2 builds passed. Final physical day timing is ONE boot (17.152 ms full period,
13.992 ms critical-path work); its repeat hit pre-gameplay freepad DMA Busy.
Final night hardware timing and input-latency measurement remain pending a
physical power cycle. Never substitute emulator FPS or predecessor night work
for those checks.

Morning physical follow-up (2026-10-02): the SAME final day ELF repeated
13.988 ms work / 17.152 ms period (58.3 Hz), with 0/512 above 16.667 ms.
Final night boot 1 measured 18.234 ms work / 33.403 ms period (29.94 Hz);
all 512 samples exceed the NTSC work budget. The next soft-reset launch
stalled during loading, without a usable timing window; cause undetermined.
See the runtime record for hashes. Physical night repeat and guarded driving
remain pending a physical restart; do not confuse this with a guard rejection.

The morning guarded PHYSICAL transition run FAILED at procedural loading
after dense: 2,160 frames, 98,304 accepted chains, two splits, zero fallback.
No malformed-chain rejection occurred. This is an open qualification blocker,
not a passed soak; the scratch stall probe awaits a physical restart.


1.169.1 fixes post-presentation direct-send ordering: wait the actual DMA
channel after synchronizing the previous hybrid frame, immediately before the
direct send. The quiet physical control reproduced a VIF1 DMA stall before
this correction and passed 10,920 frames after it. The repeated-transition
control without watchdogs/stage prints passed 10,440 frames, 15 transitions
(five complete scene cycles), 765,952 valid chains and zero direct fallback.
PCSX2 2.9.93 again passed field/progressive/PAL/1080i, limiter and pipeline
toggles, foreign 2D/3D handshakes and explicit synchronization. Stage stdout
can hide a timing race: the pre-fix verbose probe passed 12,000 frames while
the quiet probe hung. Preserve both controls and their hashes. Final ordinary
day/night timing passed two fresh physical boots per pose using the same ELF
within each pose: day 13.863 / 13.865 ms work and 17.152 / 17.185 ms period
(58.30 / 58.19 Hz), night 18.114 / 18.173 ms work and 33.403 ms period in both
boots (29.94 Hz). Day has 0/512 over-budget work samples per boot; night has
512/512 in each. Guarded driving FPS is not production timing. Use an immutable
log snapshot to close measurement while keeping ps2client serving the last game.
That dated acceptance record left input-to-display latency and promotion open.
The current new-project default is implemented; physical button-to-TV latency
remains unmeasured, and current post-Memo/Showcase qualification is separate.

The 2026-10-02 stationary night isolation uses seven sequential 2,200-frame
phases, one group removed per arm, with full-night controls before and after.
Warm raw windows start at phase*2200+1100 and contain 512 frames. See
docs/tyrax2-night-isolation-2026-10-02.json for preparation/analysis sources and
hashes. Control spread was 0.197 ms; do not add removal gains or extrapolate
stationary vehicle-light cost to driving. All arms retained approximately 30 Hz.
tools/tyrax2-timing.py recognizes only explicit unprefixed FRAMEARENA,
NATIVEVIF, ORDEREDFRAME, FRAMEPIPELINE and NIGHTISOLATE trailers after raw words;
tty fragmentation can glue one to word 64. Exact word count, malformed-token,
duplicate-frame and missing-frame checks remain mandatory. Eleven synthetic
boundary/strictness controls passed, with the archived baseline unchanged.

SPR finalized-prefix host experiment: compile tools/verify-spr-prefix-staging.cpp
with C++17, -O2 -Wall -Wextra -Werror (recorded Windows MinGW run uses -static).
It tests actual FrameVifWriter bytes and the current validator through 8/16 KiB
host windows, rollback before finalization, delayed copies and held bank reuse.
Forty variants passed; this is scaffolding bookkeeping, not an EE SPR owner,
DMA/cache/barrier proof or hardware speed claim. See docs/tyrax2-spr-staging.md
for the retained-flush physical prototype and separate no-flush gates.

Pool/beam split attribution: docs/tyrax2-light-split-2026-10-02.json preserves
five 2,200-frame diagnostic phases and their 512-frame raw work windows.
Per-pass COP0 totals include nested render time; do not add the two or call
their difference geometry-only. Receiver timing covers three explicit call
sites, not every terrain helper. Both full-night controls show zero beam
buffer rebuilds despite about 0.25 ms surrounding assembly/comparison work.
Candidate memoization must invalidate for moving camera/light, visibility,
runtime levels and portal views, then pass byte/pixel controls before quiet
hardware timing. All diagnostic arms still delivered approximately 30 Hz.


Current ordered-night attribution (2026-10-02): see docs/tyrax2-ee-vu0.md and
docs/tyrax2-pipeline-attribution-2026-10-02.json. Snapshot/native conversion are
nested inside submit; read-only sampled FINISH yields a combined consumer
completion upper bound, not VU utilization. Existing waits total about 0.003 ms;
cache flush is about 0.009 ms, so do not reuse the historical 1.09 ms estimate.
VU0 macro users already include vectors/matrices/bounds/skinning. Price remaining
scalar math and micro transfer/ownership before proposing asynchronous offload.


One-search immutable borrowing trial: docs/tyrax2-immutable-borrow-2026-10-02.json
records 55,163 differential operations, integrated arena/writer gates, native/
emulator success and one physical same-ELF control/candidate/control run. Work
18.468/18.435/18.556 ms gives 0.033–0.121 ms apparent savings versus 0.088 ms
control spread; all arms remain 29.94 Hz. Candidate stays private: fewer binary
searches and passing semantics are not proof of a material frame gain.


Snapshot/native breakdown: docs/tyrax2-snapshot-breakdown-2026-10-02.json retains
one physical gateoff/on/off run and the exact callback/state oracle. Enabled
preflight/fixups total 1.882 ms; mutable copy 1.056 ms; chain copy 0.266 ms.
Observer work increase is 0.252–0.431 ms; gateoff still has compiled scaffold,
so pristine/compiled-out controls remain separate. Never add snapshot children
to their parent or claim VU utilization. Existing object-cost export inserts
per-object batch fences; some STAPIP_ATTRIB counters ignore runtime telemetry.
Use ordinary batching for the next exclusive scalar-math census.

Exclusive ordinary-batching math census: docs/tyrax2-ee-math-2026-10-02.json
records 6,600 physical/emulator frames, seven scoped paths and same-ELF gate
controls. Clip transform8 is 0.377 ms with 11 duplicate guard-fallback calls/frame;
light pick is 0.457 ms with 912 candidates. Counter/clock overhead and VU0 macro
helpers are included; no micro-mode utilization or production gain follows.
Gate per-render reuse/classification and exact light-selection ordering first.

Same-call clip reuse passes host/native/emulator semantics yet the physical
control/candidate/control work is 18.248/19.137/18.411 ms, all 29.94 Hz.
Candidate remains private/rejected. Auto-interleave decision reports differ,
without proving cause; pin paths if revisiting. Archive matching symbol ELF
with stripped hardware ELF before a separate motion rebuild overwrites it.
See docs/tyrax2-clip-reuse-2026-10-02.json for source and evidence limits.

Auto interleave integration: docs/tyrax2-interleave.md proves mixed-label
sampling under N/N-1, not nonzero bias. Private pinned 0/2 controls bypass
selector clocks, preserve authored-off/split guards and reset stale marks on
mode revisions. Native/emulator/physical 13,200 frames pass with exact pinned
calls. Physical plain/interleaved/plain work 18.281/18.028/19.200 ms has 0.919 ms
plain spread, so repeat before accepting a gain; all arms remain about 30 Hz.
Discarded pick count is zero in this physical stationary pose (114 retained,
912 candidates/frame). Keep BLSS-shared sphere distinct from removable work.
Check fresh low frame counters and boot/load markers after a client launch;
old streaming frames alone do not prove the new ELF started. Preserve excluded
streams and matching symbols. Census exact inputs before implementing reuse.

Identical-ELF fresh pinned repeat passes 13,200 frames/windows: plain/IL/plain
18.392/17.988/18.346 ms, saving 0.358–0.404 ms versus 0.046 ms plain spread,
all about 30 Hz. Positive stationary order evidence, no production selector fix
or FPS promotion. Actual animated light/sky/packet equality is not proven;
scene reload retains clocks and hour pin precedes real-dt day/night advance.
Private inverse count-only observer passes 100,039 host actual-source comparisons
per macro setting; native/emulator/physical activation are separate gates.

Enabled inverse count-only census: docs/tyrax2-ee-inverse-2026-10-02.json.
Native build and 6,600 emulator frames pass three raw512/count windows under
plain order in every arm: 71 inverse calls/frame, 60 finite input repeats,
59 same-pointer/1 different-pointer repeats, 1 first/10 changed, frameEnd1.
Physical activation and key/inverse cost remain pending; never multiply the
whole local-preparation parent cost by repeat fraction as an expected saving.
Gateoff scaffold and gateon comparison/copy/count work require separate pricing.

Fresh physical inverse census completes6,600frames, same ELF as emulator:
71calls/60finite repeats/1first/10changed and frameEnd1. Work off/on/off
18.191/18.492/18.239ms, all33.403ms periods. Enabled census work increases
0.253–0.300ms vs0.048ms controlspread; not pure inverse/key cost or gain.
Physical activation is complete; next separately price minimal key/inverse work.

Core latency gate: docs/tyrax2.md and tyrax2-latency-plan-2026-10-02.json.
Pad/Game/Present scopes do not associate input edges with displayed buffers;
active presentation completes the previous job, and triple-buffer flip queues
for later vblank. Track edge/job/buffer identity, not sequence subtraction or
requested pipeline state. Display-register delay is not button-to-TV photons.
Private marker off/on/off plan is read-only preparation, not measured latency.

Private visual-clock replay passes 13,200 physical frames and six raw/count/order
windows without emulator: plain/IL/plain 17.521/17.308/17.523 ms gives about
0.214 ms benefit vs 0.0012 ms plain spread, still30Hz. Eleven sampled pinned
light/state/camera/star/sky digests agree; unsampled/pixel/query equality and
causation of earlier drift remain open. Clock/observer/layout changes prevent
cross-build production-gain attribution. See tyrax2-pinned-clock-2026-10-02.json.

Private latency fixture now passes6421actual-header host checks and nativebuild;
see docs/tyrax2-latency-fixture-2026-10-02.json. Shared synthetic1/trace1 build
is software mapping only; manualSquare default is a separate build. Match
normalProgressive480p/Bits16/two buffers/limiter/plain/noBLSS/extrapolation
across3x1800loop blocks, warm300. Ring180224B, no per-event prints/newwaits.
Target uint32 printf casts and compiler clock memory clobber are recorded.
V1 PS2/emulator completed5400loops but mapping FAILED: actual legacy off
(ORDERED/native/arena0, support1) never queueRecords; syncPresent consumed
absent/stale queued tags. Hostoracle had assumed ordered off. Validate actual
legacy1,2,7,8 and active1,2,3,4,5,6,10,7,8 paths, all39warm synthetic edges,
plus off/on/restored-off completeness. Cold extra renders/prefixes must be
accounted separately. V2 actual-header host36241/36345, native and full
5400loop emulator39/39 mapping pass; actual cold extras24renders/5prefixes
recorded. Initial-block marker screenshot checked separately. PhysicalV2
PASS5400loops39/39synthetic maps: medians33.276/66.623/33.276ms
EEevent-to-display-register, about33.35ms later on in this exact instrumented
night/two-buffer configuration. Firstoff has49.960ms outlier; means/ranges
retained. No universal penalty, observer-free cost or button/TVphoton result.
Emulator numerical delay is not hardware latency. This dated fixture did not
establish a default-policy or TV-photon result; current new-project defaults
follow docs/tyrax2.md and physical button-to-TV latency remains unmeasured.


Private inverse/key cost trial: Off/CountOnly/Scoped/CountOnly/Off,5x2200
frames, plain order and4cold scene reloads. Native+full11000 emulator pass
5raw512 windows/220records; warm71calls60repeat11miss/nf0/frameEnds1.
Full16 conservative64B key is not inverse output cache; every original inverse
runs. Emulator clocks are not physical cost. Physical11000frames pass: scoped
key0.138159ms/inverse0.076089ms, repeat inverse0.063533ms. Off/Count/Scoped/
Count/Off work18.681/18.857/18.928/18.961/18.731ms, all33.403ms periods.
Countspread0.104ms containsScoped-minus-Count: clockoverhead unresolved.
Originalparser failed interleaved hostopen; separatephysical-v2 keeps original
hashes and strips only anchoredknowncompleteopen rows (3positive/12reject).
No outputcache/framegain. INVCOST
f labels END, FRAMETIME labels START: match warmed aggregate endpoints
first+50..first+500, retain12-frame rawtail mismatch. Disjoint key/inverse scopes
are BuildSpot children; inverse-minus-key is not frame gain. See
docs/tyrax2-ee-inverse-cost-2026-10-02.json.

Portable TyraX2 source controls: `python tools/verify-tyrax2-host.py` uses a host
GCC/Clang C++17 compiler (`--compiler /path/to/g++`, optional `--only adaptive`
or `--only count`). It extracts actual selector methods/declarations and includes
the actual arena header; temporary type shims compile tracing out. It verifies
bookkeeping/counted-copy semantics, not target timing or full editor/game behavior.
Run target builds, image checks and physical controls separately.

Adaptive host acceptance now checks successful/failed synthetic warp generation, recurring interrupted epochs retaining both verdicts without clocks, exact frameYield changes, same-count portal-live identity swaps, and diagnostic HWT/serialized-cost exclusion. One dedicated ordered COP0 read preserves compiler memory ordering; no new waits. Run tools/verify-tyrax2-host.py (host only).


Engine-known memo16 controls in `tools/verify-frame-arena.cpp` exercise the
actual arena against generic counted output/cursor/borrow/counts at the
0/1/15/16/17/40-reference prefix/spill boundaries, malformed/short-capacity
failures, retry after retirement and both-bank reader release. Public generic
copy/copyCounted callback traces stay equal; no engine implementation is mirrored
in the oracle. Host output/lease proof does not establish DMA/cache timing,
native stack assembly or physical display cadence.


### Portable ordinary-clock quiet fixtures

Use `tools/tyrax2-quiet-fixture.py --project PROJECT --engine TYRA_ROOT --editor
EDITOR_EXECUTABLE --out NEW_DESTINATION --order 0` with absolute paths. It copies
the authored FPP project/current engine, refreshes generation only in that copy,
then applies uniquely anchored private sampler hooks. It performs no game build
or device launch. Unsupported templates, reused destinations and source drift
are rejected; do not regenerate an instrumented fixture.

Authored display/color/triple/pipeline request, product HUD, camera/scripts,
portals, audio, save values and ordinary clocks remain intact. Authored remote-pad/input-recorder/keyboard controls remain by default; explicit
`--disable-control-apparatus` disables only the named remotePad/inputRecorder
keys and records that apparatus control. Initial requests 0/1 and sparse request
changes are retained, including explicit false/legacy v94 synchronous ownership.
Mood/script state
is opaque: no save slot is changed. Diagnostic overlays and link/debug/logic polling are disabled
and FrameProfile/HardwareTrace compile out in the private copy. The same-ELF
Off/On/Off and reverse orders keep a 6144-byte sample buffer and 420-byte chunk
array. Two per-loop clocks are restricted to the warmed On window; common 64-loop
chunks bound Count wrap assumptions, and authoritative file exports follow all
timed windows.

Run `tools/tyrax2-quiet/analyze-quiet.py STDOUT --artifact FILE --expected-order
0 --environment ps2 -o REPORT` only on complete evidence. Preserve exact native
build/source/ELF/symbol/config/runtime-asset provenance for both boots. Missing
UDP/file records are not reconstructed. Completed rendered flip-return periods
and synthetic/zero/multiple events are separate; triple-buffer returns may mark
queue boundaries rather than TV scanout. Ordinary/adaptive state can drift, so
net sampler differences are not isolated observer instructions, pure CPU cost,
uniform corrections or 60 FPS certification. See
`tools/tyrax2-quiet/README.md` for bounds, limitations and actual-header/source
host controls; no extra fence/wait/register read is inserted.

The source identity hashes every file in engine/game source and include trees, engine resources and the optional game `vugen` framework, without an extension whitelist (including embedded `.irx-em`). Root build recipes/helpers and project data are also covered; generated `obj`/`bin` outputs are excluded. Runtime assets are recorded separately. PS2DEV/SDK/VCL/compiler binaries and SDK IRX modules named by `.irx-em` recipes remain external build dependencies and require separate native provenance; the manifest does not claim to hash those installed tools. Missing required source trees or recipes reject preparation.

Host VU dependency validation follows the native recipe exactly: only direct lowercase `.cpp` files in `game/src/vu` or `game/src/vu0` activate compilation of `game/vugen/*.cpp`. Active recipes require framework implementations and the literal quoted include closure resolved through source-local paths or `game/vugen`; unresolved or out-of-manifest local includes reject preparation. Empty, nested-only and name-only VU directories do not impose a framework requirement. Installed standard-library/compiler dependencies remain external native provenance.

Producer-origin V5 closure (2026-10-04): both PS2/emulator orders qualify384
records each, preserving all26 original V4 counters across24 warm/sample
windows. Twelve origin buckets partition Bag129360B/frame, including explicit
UnknownBag86448B; labels never grant an immutable lease. Its incremental
serialized observer costs1.526–1.533ms; common tags/reset/layout remain unpriced.
Use docs/tyrax2-producer-origin-census-2026-10-04.json. Next rank execution
intervals and price a minimal selected physical scope; another broader byte
census is not automatically the next step.

Debugger execution map (2026-10-04): Linux PCSX2 under owned WSL/Xvfb93 is
controllable via X11, unlike the failed Windows capture/input session. Exact
version/layout/text/ZIP controls and actual SP/RA close135 core calls; count
all tail calls and reject orphan returns. SaveState drains VU/GS, so decoded
cycle intervals only rank suspects, never hardware milliseconds or FPS.
See docs/tyrax2-pcsx2-debugger.md and its dated machine record before reuse.

Selected physical scopes (2026-10-04): ordinary night both-order controls now price one terrain/wheel/core invocation with four diagnostic Count reads per timed loop. See docs/tyrax2-minimal-scopes.md and its machine pins. Keep raw ps2client banner bytes; a separate byte-bijective Latin1-to-UTF8 copy is acceptable only with ASCII protocol controls and both hashes. Reject inherited periodic arena host reports inside quiet apparatus. Common1536-byte ring/counters/layout remain unpriced, and sign-changing phase contrasts do not permit uniform subtraction. Wheel whole-stage0.304–0.306ms is smaller priority than the first terrain core1.103–1.113ms; neither is pure EE or a new60FPS gain.

Frame pipeline flow nodes (1.172.0, format v95 unchanged): `SetFramePipeline`
queues a ScriptContext request; its after output continues immediately.
`GetFramePipeline` reads the engine requested state, excluding the pending
request and active fallback status. Both FPP/ORBIT apply requests before
next beginFrame. Skip an unchanged request against getFramePipeline(): even
an unchanged engine setter completes a pending frame and loses overlap.
Both nodes remain unsupported in Live Logic and require a rebuild. Verify
authored chained graphs through --apply-graph and actual generated output;
registry execThrough alone does not emit downstream actions. See
docs/frame-pipeline-flow-nodes.md.

## Actual VU output and debugger control (Pool2, 2026-10-05)

The private fixed-case workflow and retained repairs are documented in
`docs/tyrax2-pool2-colors.md`. Stock PINE reads at VU1 address 0x1100c000 returned
successful zero data; use actual 16 KiB SaveState VU1 members with decompression
and full CRC checks. Bind source/native identity, ELF text, EE-ready value and
paused halt PC after normal frame synchronization. Disable MTVU in this separate
correctness profile; do not infer hardware timing from captured emulator state.
Static repeated inputs without an epoch prove final-bank equality, not fresh
VU execution on every warm iteration.

For the tested PCSX2 build, preload execute breakpoints using ELF XOR-word CRC
and type 8, then verify loaded entry and breakpoint state before an observed Run
click. Space depends on focus and can toggle a breakpoint instead of running.
A missing host audio library can open a modal that blocks Run; a host Null audio
backend is acceptable for separate packet correctness, not an unpriced change
to a physical trial. Preserve repairs and exact-owned stop evidence rather than
claiming a clean automated run. Never stop an unrelated emulator instance.


## Lazy Pool2 producers and asset/raster gates (2026-10-05)

The private EE producer continuation in `docs/tyrax2-pool2-ee-producer.md`
uses owner-local lazy color generations. A shared global stamp can collide
with another owner's saved readiness after wrap; test local wrap explicitly.
Materialization must run outside assertion expressions: release builds may
remove assertions and their side effects. Source diagnostics that read lazy
arrays require explicit materialization or a documented restricted scope.

A source-only restored game needs authored `res` metadata as well as
`.res-baked` before native building, otherwise WAV-to-ADPCM discovery may be
skipped. Check expected runtime filenames/conversions against a qualified
asset set, not merely a self-consistent manifest of whatever was published.

Counter-based cold packet comparators can materialize eligible lazy colors
and defeat the observer's purpose. A separate diagnostic disables that oracle,
poisons backing and verifies source readiness plus actual final VU output.
Change colors each iteration and decode the requested epoch; identical final
banks alone do not establish fresh execution. Epoch changes force replay
misses and therefore do not qualify stable warm hits. Clip output needs its
own route/layout decoder, not an inside TC decoder with relaxed assertions.

An owned SDL host audio error modal can occlude the render window while the
guest protocol completes. Preserve the negative raster attempt and prove the
window owner/geometry before diagnosing shader corruption. A fresh Null host
audio profile is a correctness control, with unchanged guest ELF/resources;
it provides no hardware audio or timing acceptance.

The private Pool-lattice trial (`docs/tyrax2-pool-lattice.md`) demonstrates why
source admission and native budget are separate runtime gates. Its no-fog
prototype passed fixed probes but admitted nothing in the ordinary fog-enabled
night. Restoring original per-corner fog required actual varying-fog output
pairs before pricing. Protected qbuffer arrays do not alone protect a pending
multi-bag DMA chain: owner reuse needs a lease or a submission fence before
uniforms. Price that fence and input preparation with the candidate. Preserve
failed compiler artifacts before a shared native mirror changes; compute full
residency including billboards and draw-finish, rather than the main set alone.
Sparse cold counters prove their sampled frames, not every timed frame. The
same-ELF physical candidate was slower despite fewer transforms; no promotion
or pure VU timing follows from a reduced operation count.
The private GS SPRITE corona trial (`docs/tyrax2-gs-sprite-coronas.md`) failed
ordinary admission at Hybrid/24-bit depth and positive Bits16 output despite
valid completed quads. Count requests separately from final GIF primitive tags.
An assembled SUB followed immediately by FMAND is not a flag-dependency proof:
the preserved PCSX2 model exposes MAC results at four cycles without FMAND VF
interlocks. Preserve actual schedule/output; the exact rejection branch remains
an inference without a trace. Audit flag scheduling and full resident capacity
before any repair. Native V6 had only two spare instructions with billboards.
Validate actual signed spotlight gate and uploaded uniforms, not a nonzero word.
A black diagnostic screenshot cannot qualify visible raster. Parser controls
must include real emulator timestamp prefixes; preserve rejected analysis and
attach a separately pinned reanalysis without rewriting launch authority.

The subsequent private 24-bit corona follow-up proves only its reachable
FTOI4-to-ITOF0 domain, isolated completed SPRITE endpoints and identical textured
PSMCT32 drawing pixels. It pairs each FMAND with an upper ABS read of the SUB
scratch result, preserving MAC flags while creating a VF interlock without
adding words (TC506; full residency2040/2042). A strict source-bound TC-only
postprocessor rejects changed schedules. Do not widen an arbitrary integer
domain or patch shared compiler output/cache by hand. The window/Hybrid scanout
remained black in both isolated arms; readback qualified drawing VRAM only.
Ordinary activation, presentation and physical pricing still require their own
evidence. Preserve the original failed trial and both new observer revisions.

### Candidate roles in reverse pricing orders

Derive candidate/control means from the recorded applied variant flags, not
from phase position or an inherited report field name. In on/off/on, the
outer phases are candidates and the middle is control. Preserve adjacent
contrasts and repeated-arm spread; never turn middle-minus-outer into a
candidate sign without mapping roles. The 24-bit corona checkpoint's original
raw analyses were correct; its first prose summary reversed the reverse-order
role and is explicitly corrected by a role-based result artifact. A crashed
host client yields an incomplete attempt, not a completed console result or
proof of a console hang. Preserve raw/partial output before a fresh reset.

Core prefix kind14: validate both Off/On/Off and On/Off/On physical orders, exactly two Count reads per timed entered scope, first-stage early returns, equal second/third call counts and zero reserved stages. Keep sparse 750/1155 units separate from 320-loop totals and disable scope clocks after loop5400. See `docs/tyrax2-core-prefix-partition.md`; initial no-output launches remain excluded evidence, even if ping succeeds.

Kind15 observer validation: stages1/2 have equal calls no greater than stage0; stages3/4 have equal calls no greater than stage2, in cold and completed timed windows. Retain rejected freepad boots and close their artifact before retry. Matrix-key quad instructions need a target oracle for every512bit position; plane specialization needs host plus target cold bit comparisons, not visual appearance alone.

Core candidate kinds16/17 use the inherited producer record labels as candidate flags, not timed scopes: every scoped reads/ticks/timedCalls field is zero. Require actual cold invocation/applied/compared equality and zero mismatches; matrix kind16 also requires4104 target bit cases. Preserve completed valid=0 numerical failures as rejected, not accepted prices. Host/emulator passing arithmetic does not override real-EE mismatches.

Sky retint kind19 requires positive physical cold activation, exact full-array and retained-geometry/color-stamp checks, zero fallback/mismatch and no new scoped clocks. Inactive emulator counters must be rejected, even with normal pictures. A cold oracle leaves the original build live, so capture warm frames separately; forced-color/lifecycle qualification is not a pricing run. See `docs/tyrax2-sky-retint.md`.

Paused-clock kind20 separates actual pause/hour witnesses from sky retint mediators: inactive retint is expected when paused. Require exact requested hour, positive unchanged frame dt, both physical orders and no extra scope reads; full cycle evaluation remains enabled. Clock policy affects all day/night consumers, so prices are not sky-only. Exercise resume and active/inactive scene resets before promotion.

## Public dynamic light receiver verification (1.173)

Check legacy/invalid All defaults, project/session round-trip, independent scene
inheritance and scene undo. Exercise live player/driver entry/exit and mixed wheel
batches, scalar animated pickup and source ownership in mirrors/portals. Preview
selection must not grant vehicle reception. Preserve pools and probes in both
shading modes. Native build and emulator output are separate from physical pricing;
private historical receiver timing is not a public feature price. See
docs/dynamic-light-receivers.md.

For private main11 diagnostics, require mandatory cold WHY rows and distinguish
completion from positive activation. Both frozen attempts use original fallback
draws; changed lamp state and member light mismatch do not qualify batching gain.
See docs/tyrax2-main11-batch.md and its source/evidence archive.

Two-axis render audit: `docs/tyrax2-render-audit-2026-10-06/README.md`
archives completed hardware stage/work partitions and rejected overlap evidence.
flipBuffers can issue GIF leaf waits inside the existing pacing interval. Mark
pacing active before flip, count covered calls without new clocks, and exclude
that interval exactly once. Algebraic closure of the old top-level balance did
not prove disjointness; the nested render observer caught the overlap. Preserve
historical pins and publish a correction rather than rewriting evidence.
Dense scope clocks cost 1.40-1.75 ms in this trial and change deadline crossings;
use selective coarse scopes next, price their active cost and keep residual
buckets explicit. Stage and work type are alternative partitions, never additive.
Submission/recording elapsed includes queue preparation, not just DMA launch;
no pure EE/VU/GS price or stable-60 claim follows from that label.

Selective queue follow-up: `docs/tyrax2-queue-audit-2026-10-06/README.md`
completes 5400 physical loops with one recorded VIF1 flush/enqueue per active
row, zero extra sizing loops, and snapshot ownership/copy as the largest queue
scope (2.58 ms driving mean). Snapshot includes preflight, lease lookup and
fixup, not memcpy alone; counted sizing and quadword copies already exist.
Dense Stage/Work/count APIs are compiled no-ops in this private fixture.
Active parked net perturbations are 0.79/0.42 ms, with different active submit
counts; fixed camera witnesses do not prove identical workload. Preserve
scope/event activation equalities and residuals. A single-lookup borrow
candidate must retain all range, retirement and bank-reader semantics and be
priced independently; never remove lifetime checks based on a large copy span.
