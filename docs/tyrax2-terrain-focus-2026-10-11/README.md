# A28 Stable Terrain Focus, Visual Shake Unchanged

Before this lead, the investigation register and earlier LOD/terrain reports
were reviewed. Existing half-chunk snapping and eviction hysteresis do not
separate visual shake from the LOD focus. This is not a cache/capacity change.

Source finding: vehicle speed shake perturbs cameraLookAt before renderScene
passes its x/z into updateTerrainChunks. At driven car x=0, millimetre shake
crosses a floor-based half-chunk snapping boundary: effective focus alternates
between -16 and +16 metres. A27 physical gas CSV shows 53..55 focus switches
per trip versus six using actual car motion; baseline logs show 403..413
terrain builds during each trip. See [A27](../tyrax2-log-gate-2026-10-11/README.md).

Private A28 derives from bound A27 V2. Save cameraLookAt x/z immediately before
flow/vehicle visual shake; candidate uses this stable point for terrain only.
Baseline uses the original shaken point. Both arms retain camera shake, normal
distance LOD, neighbor edge matching, budget two, roads and live reflections.
Ordinary logs are quiet in BOTH gas arms. No new hot clocks or cache.

Run day/night 0/7/0 with the existing real-dt full-throttle/camera sweep harness.
Moving rasters may differ because corrected LOD selects different transitions;
do not require false identical candidate geometry or claim this is pure EE
cycles. Cold checkpoints use the same held stable terrain focus in all five
arms with 32 settling frames, verify every resident chunk's own/neighbor LOD
contract, then require exact all-arm main/env images, ordered inputs and SDK
header/state checks. This checks rendering under an identical focus, not
pixel equality of the original broken moving LOD selection.

Tools: `tools/tyrax2-terrain-focus.py` derives only from verified native-bound
A27 V2; `tools/tyrax2-terrain-focus-analyze.py` retains A26 travel/SDK/input/
raster gates, adds quiet-both-arm transport and all cold terrain contract
checks, plus missing-window/ordinary-output/missing-contract/missing-resident
chunk rejection tests. Require all 25 resident chunks at every held checkpoint.
After both runs qualify, preserve with terrain-focus-archive and reanalyze
both archived reports read-only with external output paths; compare report
hashes. Do not archive a failed run as accepted evidence.

V1 native build PASS; source/mirror/ELF binding covers 478 files and requires
the linked TerrainFocus::x symbol.
The build.log is an explicitly abridged command/result transcript; native
binding checks actual source/mirror/ELF bytes rather than that prose record.
ELF SHA256:
`d08255fa22677a6882e581744435aab82da0ccfa8d37794702f2db2a5acddb59`.
Fixture `F:/Projects/tyrax2-terrain-focus-20261011-v1` completes hidden PCSX2:
all six trips, all 60 exact held-focus main/env images, ordered inputs, SDK
bytes/state and all 25 resident own/neighbor LOD contracts pass. The 17 reader
rejection controls pass; ordinary transport is quiet in every gas arm.
Owned emulator PID31/Xvfb20 stopped after DRIVEDONE valid=1. Physical also
completes via editor network-reset protocol: six trips, 60 exact held-focus
images, input/SDK/25-chunk edge contracts and all 17 reader controls pass.
Owned ps2client PID38896 stopped after DRIVEDONE valid=1 and exported evidence.
No manual reset requested. Console remains powered on running completed game;
next deployment uses the editor network-reset protocol.

## Forward Physical Result

| Phase | Role | Mean work ms | p95 ms | Entry Hz |
| --- | --- | ---: | ---: | ---: |
| 0 day | Baseline | 17.354 | 23.443 | 38.36 |
| 1 day | Stable focus | 15.653 | 22.312 | 43.04 |
| 2 day | Baseline | 17.423 | 24.214 | 38.55 |
| 3 night | Baseline | 19.584 | 26.922 | 34.19 |
| 4 night | Stable focus | 17.972 | 25.631 | 37.36 |
| 5 night | Baseline | 19.359 | 26.699 | 35.00 |

Mean saved work 1.734962 ms day / 1.500039 night; equal-distance saving
1.940194 / 1.745236 ms. First-order gate PASS. This is useful avoided work,
NOT stable 60 fps, pure EE cycles or a scene performance ceiling. Correcting
the focus also changes transitional selected LOD and downstream geometry;
both arms preserve the normal LOD algorithm and rendering camera shake.
All eight equal-distance bins have positive savings in both day and night;
individual savings range 0.037..4.394 ms. Full values remain in the report.
The forward candidate still exceeds 16.667 ms on 148/377 priced daytime frames
and 197/326 night frames. Existing probeCapture labels split day into no-capture
227 frames/13.890 ms mean (48 over budget) and capture 150/18.322 ms (100 over);
night 196/15.624 ms (85 over) vs 130/21.512 ms (112 over). These are observational
cohorts with different moving views/dt, NOT a causal price of shared capture.
Even non-capture frames miss the budget, so solving only capture is insufficient.

Status: V1 native/emulator/physical first-order qualification PASS. Archive
`focus-v1/` preserves 639 files. Read-only archive reanalysis with external
output files reproduces both reports byte-for-byte. Report SHA256 emulator
`d87618285b96851914fe80d67ce128649b814105a7f6eca822c136eb56cb3b9a`;
physical `ce240f04d3c38984b75336ac08c457cffeaab2b34337aa9c83c26e51a2193d83`.
Production integration follows the independently qualified reverse series below.

If V1 passes the physical first-order gate, derive a fresh bound V2 with
`--reverse` from the same A27 V2 source. Keep numeric modes 0/7/0 but explicitly
reverse their terrain intervention roles to candidate/baseline/candidate; SDK
oracle mode labels remain diagnostic only. Reader reports phaseRoles/reverse
and recomputes causal contrasts, requiring BOTH outer candidate means/spatial
means faster and their maximum p95 no more than baseline +0.15 ms. Do not
price reverse data with the forward formula. Requalify native/emulator before
physical deployment; no production promotion from one forward series alone.
Fresh V2 reverse fixture at `F:/Projects/tyrax2-terrain-focus-20261011-v2`
passes native build/binding (478 files, TerrainFocus::x linked). ELF SHA256:
`bd7af02e8133688014b2aae14035a3ab50a010332e5439cff0b81ec4d5e051cb`.
Hidden emulator completes all six trips/60 held-focus images, input/SDK/25-chunk
contracts and all 17 reader controls. Owned PID31/Xvfb20 stopped after completion.
Physical reverse series completes via editor network reset: all six trips,
60 held-focus images, input/SDK/25-chunk contracts and 17 reader controls PASS.
Owned ps2client PID44616 stopped after DRIVEDONE and exports. Both outer
candidate means/spatial means beat the control, with lower maximum p95.
Mean savings are 1.798894 ms day / 1.602700 night; equal-distance savings
2.006395 / 1.663366 ms. Candidate cadence is about 43.0/37.3 Hz, NOT 60.
Archive `focus-v2/` preserves 639 files. External read-only reanalysis
reproduces both reports byte-for-byte. Emulator report SHA256
`936b74ae16f79c8fe9227cb2a5f5c3c88a0c4faf4b027d3fad28896a849b679f`;
physical `6b8c9c0b4c2b41d8f76a4bc9a2d06c2bbc0bdba946cfd4e9d7f469651062136f`.
Do not reuse V1 timings as V2 native binding or compare
absolute times across the two ELFs for causal cost.

## Production Integration

Production integration is enabled as 1.173.5: both template headers
declare stable terrainFocusX/Z, both main loops save them before visual shake,
and the shared terrain streaming call consumes them. The generated Vehicle
Playground counterpart is updated without regenerating unrelated files.
`tools/verify-terrain-focus.py` compiles the actual two snapshot fragments and
shared streaming call, checking jitter, fresh target, player-two focus and
second split-pass exclusion. This host test is not a PS2 timing test.
Release editor build and normal Vehicle Playground native build PASS. Fresh
FPP and orbit projects generated by that editor pass the target compiler's
syntax checks for terrain_game.cpp and game_physics.gen.cpp. No claim of
ordinary production-game 60 fps follows from these compilation checks.

## Deferred Larger EE Architecture Audit

Source-only audit following terrain-focus qualification, not another running
experiment or performance claim. Check A24 grouping/caps, A25 consumer bypass,
C06 cached strip expansion, P08 command memo, H02 direct producer and C05 fused
snapshots before reopening their mechanisms. The following changes the actual
VU input/output contract rather than adding a cache or another cap variant.

Current src/vugen.cpp clip generator accumulates ALL triangle/fan output in
one package, then patches NLOOP and issues one XGKICK at the end. The emitted
loop advances input pointers by three. StaPipCore::renderStrippedPkgs therefore
splits true clipping into four-triangle jobs (12 list vertices for the common
TC class); fillByStripExpand/fillCachedStrip supplies triangle lists on EE.
Worst-case whole-package output occupancy couples VU capacity to EE splitting.

Candidate architecture to prototype AFTER the terrain fix qualifies: bounded
VU output rings, flushing ordered clipped fans as the ring fills, then native
strip reading/parity on VU. Original immutable strip inputs could remain REF
streams instead of EE-expanded lists and many tiny dispatches. Do not omit
clipping, change the guard band or discard crossing triangles. Preserve material
order, active plane mask, all attributes and normal queue ownership/lifetime.

Capacity sanity for TC only: clipping one convex triangle against six planes
produces at most nine polygon vertices/seven fan triangles, i.e. 21 emitted
vertices. At three output registers plus nine tag qwords, a ring needs at most
72 qwords. Two rings 144 + header 2 + 72 input vertices*3 = 362 qwords fit the
current 460-qword half with 98 spare, retaining at least the existing 91 reserve.
This is NOT a DMA/XGKICK safety proof or a different program class's proof.
Before implementation prove asynchronous ring reuse, multi-triangle order,
global strip winding, no input/scratch alias, masked clipping and fallback.
Retain GS material/state updates even for a job with zero surviving triangles:
the original emits its tag block before its vertex loop and kicks NLOOP=0.
Omitting an empty job's state can disagree with the caller's state tracking.

Bound V2 nm ranges put unique built-in main-set images at 1920 VU instruction
words including billboards; draw-finish uses 6 at the top, leaving 122 words.
Counts are C 226, D 130, TC 306, TD 144, TCE 166, clipC 338, shared clipTC 404,
billboards 96/110.
C/D and TC/TD/TCE clip wrappers share ranges; count aliases once and round
each image to even words as programSetWords/createProgramsCache do. This is a
built-in-set capacity calculation, not runtime-PC attribution or permission to
overflow custom-program sets. Reassemble/recount the actual candidate before
deploying; retain resident billboards to avoid introducing MPG/drain costs.
