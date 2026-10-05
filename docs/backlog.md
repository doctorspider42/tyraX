# Backlog

This is only unfinished work that still has a clear payoff and a testable end.
Finished investigations belong in commit history; reusable facts belong in the
relevant guide or developer skill.

## Separate remaining runtime settings from compilation decisions

Game methods now compile in six parallel units with shared inline helpers/state.
Object values, counts, identity hashes and visibility proxies compile in one
separate data unit. Ordinary object iteration takes about 10-11 s; active
ambience and project preference changes take about 38-45 s on the measured
Windows/WSL fixture, because they still invalidate shared headers. See
[native build measurements](native-toolchain.md#parallel-game-compilation).

Move ordinary runtime setting tables out of headers while retaining compile-time
feature gates and ABI/layout sizes. Compare ambience edits, feature toggles and
clean builds. Verify PS2 hardware performance before accepting optimization-level
changes; `-O3` remains the default. The subsystem split was boot-checked in PCSX2,
which is not a substitute for a hardware performance comparison.

## Bring the night garage chase view back under the PAL frame budget

The [2026-09-28 continuous vehicle-entry capture](../examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/README.md)
reproduces the reported dip without acceleration: walking at the garage is
50 FPS / 13.615 ms active work; entering the stationary Ravager gives 25 FPS /
23.923 ms warm active work. All 299 warm seated frames exceed 20 ms. The car
has zero speed and unchanged position. The earlier moving-camera sweep did
not cover this normal chase-camera view.

Target the same night start, camera mode 0 and parked traffic. Render
submission is 21.561 ms; Objects (inclusive of interleaved road/static work)
is 13.375 ms, direct object-part submission 7.375 ms, terrain draw 2.295 ms,
night light/shadow effects 1.995 ms. VIF1 wait is 5.651 ms included in
submission; vehicle update is only 0.615 ms. Do not treat these overlapping
counters as additive or infer an exclusively EE/GS/VU1 limit from DMA waits.
The view submits 46,096 triangles, against 22,425 in the walking control.
Price any culling/submission candidate against this stationary view, then the
recorded first 120 driving frames after five metres (29/120 over budget),
and moving AI traffic separately. Do not use emulator milliseconds.

The [same-ELF causal probes](../examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/causal/README.md)
identify a concrete EE waste: the full body and reflective prefix share a
vertex-pointer/package-size bbox cache entry but alternate vertex counts.
Ravager and Pica each replaced that entry twice per frame, spending about
2.9 ms rebuilding package bounds. The [accepted bounded count-variant fix](../examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/fix/README.md)
preserves both variants and passes canonical-bounds, changing-count,
dynamic-version and expiry checks. With deep counters off, same-ELF work
drops 23.933–23.995 → 21.372 ms; the garage still misses the PAL budget.
Headlights alone are not the sustained slowdown's cause. Further probes
price dynamic mesh lighting and GS raster work; neither disabling lights
nor clipping drawing to one pixel is a production optimization. Retain
the exact chase-view and first-entry gates above and target the remaining
submission/shader/raster cost with quality preserved.

The [lighting investigation and zero-influence rejection](../examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/lighting/README.md)
now identifies 36 submitted bags whose selected light adds zero, and routes
them through the existing unlit path without changing selection or geometry.
The diagnostic hardware arm saves about 0.78 ms against its two controls;
production acceptance is recorded in the linked report. A full transformed
light cache regresses despite perfect warm hits and is not shipped.
The chase census remains substantial: roads and terrain together account for
about 39% of visible bag input vertices, and the nearby cars remain in their
full authored tier. Next price per-package shader eligibility, road/terrain
sampling and batch visibility granularity on this pose; do not lower quality
thresholds blindly. PAL tail/headroom and a fresh GPU-hold census remain open.

Keep first-entry latency as a separate acceptance gate. The
[HUD A/B acceptance](../examples/vehicle-playground/authoring/night-entry-hardware-2026-09-28/hud-entry/README.md)
removes 58–61 ms, but leaves about 109 ms of exact-entry work. The HUD font now
prepares during scene loading for driveable vehicles, and plain runtime text
does not request the icon sheet. The remaining target is the first chase-view
geometry/reflection preparation, package bounds and command-stream creation,
plus the first driven-vehicle update. Moving HUD reads is not a stable-FPS fix
and does not remove those cold costs. Keep an exact entry-frame measurement,
the warm-window budget and scene-revisit/VRAM-lifetime checks separate.
Full scene-revisit acceptance is still open: the follow-up hardware launch
stopped before startup, and the forced emulator transition did not complete.

The [TyraX2 applicability review](ee-submission-rearchitecture.md#stationary-night-vehicle-entry-what-tyrax2-would-and-would-not-solve-2026-09-28)
identifies the next steady-state probes: unchanged light setup and shader
eligibility, visible work/coverage, then GPU-hold repricing of frame overlap
in this actual chase view. The older sweep's GPU-only/EE ratio does not cap
this view's gain. Seven of eight main-scene lamps do not flicker but all eight
switch with the day/night script, so simply marking them static changes the
scene's behavior; caching stable data or authored two-state lighting needs
its own image and hardware gates.

## The reflection probe: what is left after the ground stand-in (2026-09-30)

*Reflect the ground as flat colour* (docs/reflective-materials.md, "The ground
stand-in") took the open-road capture from 5.5 to 2.0 ms of probe EE. What
remains, in order of size:

- The stand-in grid itself is still ~1 ms of a capture over no ground at all,
  mostly the middle bag the eye stands in, which is always clipped. A smaller
  near block (or dropping the cells behind the eye before they are built)
  would attack exactly that part. Re-measure indoors, where the split already
  cost more than it saved once.
- It is ~10% darker than the real ground on the paint: no lightmap or AO pass.
  Folding the scene's mean baked light into the host map would close that
  without any runtime cost.
- It is not on by default. Nothing measured argues against that any more; the
  decision is the look, so it wants a real-console look at a driven car first.

The scenery boxes themselves (*Reflect static scenery as boxes*) cost 1.2-1.65
ms of a capture, mostly EE clipping of big boxes near the eye. Worth trying
after the ground: drop a box's faces that point away from the probe (half the
triangles, and the clipped ones are mostly the near box's back faces), or cap
the near boxes at the probe's near distance.

**A rear-facing probe view, priced (2026-09-30).** The probe looks only
forward, so a car's tail reflects the horizon AHEAD of it and never the road
markings behind it - Burnout 3 renders three 128x128 views for that reason
(docs/reflective-materials.md). Measured with a second, reversed capture after
the normal one (same sky, ground and marked objects; the one-ELF mode fixture,
reuse budget 0, two boots per mode on a physical PS2), mean frame while
turning:

| pose | real ground | + rear view | ground stand-in | + rear view |
|---|---:|---:|---:|---:|
| garage day | 10.67 ms | 11.62 | 10.65 | 11.47 |
| garage night | 13.68 | 15.18 | 13.68 | 15.03 |
| outer road day | 10.64 | 13.79 | 8.87 | 9.85 |
| outer road night | 11.63 | 15.10 | 9.82 | 11.18 |

So a rear view is affordable only on top of the ground stand-in (+0.8 to
+1.4 ms a turning frame, nothing parked), and that is before the second
target and the warp that would merge two views into one map. It would also
still not show lane markings: the stand-in's ground map is 5 units a texel.
A cheaper route to that one detail is to paint a road band into the lower part
of the forward map from `roadSurfaceAt` under the car.

## Occlusion culling: make the visibility pass cheaper

The unmerged `ee-occlusion` rewrite hides 23/48 draw units in a dense district
view and halves its triangle count, but physical-PS2 `work` rises 1.88–2.59 ms
across all four poses (two rounds). The result and mode breakdown are in
[occlusion-culling.md](occlusion-culling.md#hardware-verdict-on-the-unmerged-rewrite-2026-09-27).
Profile the EE depth-buffer build and candidate queries before another culling
heuristic; retain the mode-70 control, pixel gate and two hardware rounds.

## 16-bit (PSMCT16) textures, and glass that also reflects

The vehicle Texture depth offers 4 bit, 8 bit and full (32 bit). A 16-bit
format needs a `bpp16` in `TextureBpp`, a 5551 conversion in the PNG loader,
PSMCT16 block/page sizes in `Texture::getTextureSize` and the psm/bpp maps, plus
a signal in the file (PNG has no 5551 mode). Price it against 8 bit on a console
before building it: at 256x256 it is twice the heap. The see-through glass
also drops the paint's env pass (docs/vehicles.md, "See-through glass").
Drawing the env pass at the tail, after the pane, would bring the reflection
back for one more submit. The editor viewport still draws the glass opaque.

## Hardware-first GIF packet harness (only before another compaction attempt)

The 2026-09-21 `cull_tc` spike combined four PACKED A+D state writes under one
giftag. It saved 3 quadwords and 3 VU instructions per textured-colour package
and rendered correctly in PCSX2, but stalled the physical console on the first
gameplay frame. The production change was reverted; the measurements and GIF
selector limits are recorded in
[vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md#gif-state-tag-compaction-spike-2026-09-21-rejected).

Do not repeat that renderer-wide experiment. If packet compaction is revisited,
first build a minimal console harness that submits one candidate packet and
waits for FINISH, then sweep exactly one encoding variable at a time. REGLIST
is not a route for TEST/TEX1/ALPHA because they are not four-bit REGLIST
selectors. Its remaining plausible target is regular RGBAQ/XYZF2 vertex output,
which first needs an explicit native-64-bit VU packing design and an instruction
budget proving that packing costs less than it saves.

## Packet-reduction article: filtered follow-ups (2026-09-21)

The article's useful idea is not "make one enormous packet". It is to measure
the command stream as a program: separate EE construction, DMA control flow,
VIF input, VU work and GS output, then remove repetition at the layer that owns
it. The following is the shortlist after checking it against the renderer we
actually ship, in priority order.

### P1: Count the stream before changing it — DONE 2026-09-22

The opt-in static-pipeline walker now counts the exact DMA/VIF chain and derives
GIFtags, A+D writes, GS payload and XGKICKs per render producer. The four-pose
physical-console CSV and summarizer live in
`examples/vehicle-playground/authoring/packet-structure-2026-09-22/`; every
accepted row had `bad=0`, and the entire walker compiles out with frame
profiling. Garage day/night submit 197/213 XGKICKs and 765/826 A+D state writes
per frame. Use this instrument before attempting any item below.

### P2: Let VIF synthesize repeated vertex fields

Prototype one **offline, per-package baked** static textured-colour stream using
V2/V3 UNPACK plus `STCYCL`/`STROW`/`STCOL` where a field really is constant.
The 2026-09-22 structure audit found that current arrays are Vec4, clip/dynamic
paths require Vec4 interpolation, and tightly packed V2/V3 package starts need
their own padding. A runtime repack is rejected: it moves work onto the already
busy EE. Measure input QW, VU-memory/package count and physical `work`/VIF1 wait;
the experiment fails if decoding or padding erases the saved traffic.

### P3: Price 128-byte alignment for long REF payloads

**Rejected on physical hardware, 2026-09-22.** Aligning each `BagArray` base to
128 bytes moved garage-day aligned REFs only from 102/666 (15.3%) to 124/666
(18.6%), because a 75-vertex Vec4 package advances 1,200 bytes — 48 modulo 128.
Garage-day work regressed about 0.18 ms, garage-night 0.09 ms and outer-road
night 0.05 ms. The allocator arm was reverted. Do not repeat it unless P2 owns
per-package stream layout and can align every slice rather than just its base.

### P4: State-diff packets, scoped to one ordered PATH1 pass — DONE 2026-09-22

Cull/as-is textured-colour and textured-directional packages now establish
TEST/TEX1/TEX0/ALPHA on package zero of a material bag and emit only the
one-loop primitive tag on its later packages. Bit 15 of the non-clip count word
is the explicit VU ABI flag; clip packets remain unchanged because their six
high bits belong to the plane mask. `clearLastProgramName()` at every bag is
the conservative invalidation boundary, so no texture upload, external GS
writer or material transition can leak into a reuse decision.

The physical PAL console completed the automated day/night/outer-road cycle
with `bad=0`. In stable garage windows it reused state 233 times/frame by day
and 248 times/frame by night, removing respectively 932/992 GIFtags, 932/992
A+D writes and 1,864/1,984 GS payload quadwords per frame. The real frame-time
gain is deliberately reported as small: about 0.04 ms in garage day, 0.20 ms
in garage night and 0.12 ms in the outer-road view against the immediately
preceding profiled build. This is useful structural cleanup, not the missing
60-FPS lever. The tempting four-loop packed A+D encoding remains rejected; the
accepted path keeps every GIFtag one-loop.

### P5: Feasibility study for a VU1 multi-object job

The 2026-09-22 feasibility pass says **not next**. A textured-colour package
already uses 75 vertices against an 81-vertex VU-memory ceiling, leaving no
useful resident object table. Streaming records through the existing double
buffer would be a microcode/ABI rewrite and each output still needs XGKICK.
Revisit only after P4 or P2, with an explicit record/matrix/microcode budget and
a success metric of fewer submissions/MSCAL boundaries plus lower hardware
`work`, not fewer C++ calls.

### Gated, not queued as standalone work

- **PATH3 intermittent texture streaming** is interesting only when P1 shows a
  real PATH3 dependency. The measured garage GIF wait is about **0.017 ms**, so
  it is currently noise, not the dragon guarding 60 fps.
- **MFIFO** is justified only if traces show the EE producer repeatedly waiting
  for a DMA channel and enough independent work exists to overlap it. A large
  VIF1 wait alone is not that evidence; it can mean the consumer is the limit.
  **The software form of this now ships** (`Vif1Queue`, 1.125.3,
  docs/ee-submission-rearchitecture.md, "The VIF1 submission queue"). Four
  packets in flight measured -0.50..-0.72 ms of hardware `work`. So the EE was
  waiting, and some of that wait was recoverable.
- **Scratchpad staging** is already covered by S1 in
  [ee-submission-rearchitecture.md](ee-submission-rearchitecture.md).
  [The post-TyraX2 audit](tyrax2-spr-staging.md) retains its host finalized-prefix
  byte-preservation control. The [October 4 private runtime experiments](tyrax2-spr-call-runtime-2026-10-04.md)
  now close direct SPR construction and CALL/RET trials: both physical orders
  regress, by +0.178–0.389 ms for SPR and +3.332–3.532 ms for corrected CALL.
  Preserve the prototypes as source snapshots; do not promote these policies.
  Finalized-prefix copy-only staging has no physical price from these trials.
  Cache-flush removal remains gated by dirty-source inventory and barrier proof.

Do not re-add the article's already-done or already-refuted suggestions as new
tasks: primitive state already rides GIFtag PRE; VU1 clip programs already patch
variable NLOOP and emit through XGKICK; strips and degenerate joins have measured
coverage; retained static commands and bounded submission batching already own
the safe part of packet preinstantiation; uncached packets were +7.55 ms and
UCAB is unsafe with the stock patching builder; arbitrary CALL/RET/REF flyweights
violate the current retained-command lifetime/TTE contract; and the REGLIST
state shortcut is rejected immediately above.

## VIF1 submission queue: what is left (2026-09-23)

The queue (docs/ee-submission-rearchitecture.md, "The VIF1 submission queue")
recovered 0.50-0.72 ms of hardware `work`. What it did not do, ranked:

- **The 2D/HUD region - DONE in 1.126.1, what is left of it.** The HUD now
  rides VIF1 as `DIRECT` data behind the 3D (`TYRA_2D_VIF1_DIRECT`,
  docs/ee-submission-rearchitecture.md, "The HUD on VIF1"): work -0.37..-0.50
  ms on a PS2. The wait did not vanish: `finish` rose 0.06-0.18 ms, because
  `endFrame` still waits for the queue before the vsync. Left: (1) a texture a
  sprite binds for the first time still goes out over PATH3 and fences the
  chain; sending uploads through the chain as PATH2 `IMAGE` data would remove
  that wait on a HUD that pages its textures (the parked fixture never does);
  (2) consecutive sprites repeat XYOFFSET/TEX1/ALPHA/TEX0 per sprite (~16 qw
  each), so a run of glyphs sharing a texture could share one state block -
  cheaper chains, not a shorter EE wait; (3) the rest of the old 2.7 ms is
  the `endFrame` fence, i.e. the EE waiting for VU1/GS to finish the frame -
  only a lighter 3D frame, or work the EE could do before it, shrinks it.
  1.126.2 then cached the sprite texture lookup (-0.81..-1.07 ms); the 2D
  pass is 0.60-0.63 ms now, 0.45 of it building each sprite's ~16 qw of GIF
  data through libdraw calls, which is item (2)'s target.
- **Garage night's queue wait (+0.75 ms of `vif_wait` since 1.126.3).** With
  the wrap switches out of the EE, the pose runs into a full queue and waits
  for VU1 there. The GPU-only frame (7.73 ms) is well under the EE's, so
  that wait is an ordering problem: a GPU-heavy run of bags (the lamp pools)
  arriving while the EE has nothing else queued. Try interleaving cheap-GPU,
  EE-heavy bags with it, or deferring the pool run, before a deeper queue
  (depth 8 measured worse).

- **One chain for the whole scene / a frame-pipelined engine — integrated;
  new-project default enabled in 1.171.0 (format v95).**
  New projects request TyraX2; missing v94-or-older fields preserve false,
  missing v95 fields default true and explicit booleans win. Every save writes
  the effective bool and generated initialization emits true/false explicitly.
  Engine compile-time defaults remain zero; compatibility modes still apply.
  The [current release qualification](tyrax2-release-2026-10-03.json) separates
  host/default controls, post-Memo motion, 44 native builds, five emulator
  fallback controls and the shipping portable probe from physical performance
  gates. All 44 example boots, both log channels and actual local images passed
  independent review. The examples explicitly request the pipeline and their
  regenerated sources match qualified inputs: 42 original references, a
  separately built corrected Portal release and a separately built Cube
  signed-zero reference. Built-in font/HUD resources were refreshed alongside
  generated sources; authored quality was not reduced. Showcase cellar/return
  and Grand Tour handback, and the corrected sphere-carry portal circuit,
  passed actual pad driving and same-ELF replay with zero divergences. This is
  not exhaustive gameplay or hardware FPS acceptance for all 44 maps.
  The dated measurements below remain historical.
  [TyraX2](tyrax2.md) records one native VIF1 chain per ordinary frame, including
  ordered uploads, state, auxiliary passes and the final FINISH. Two owned banks
  let the EE prepare N while VU1/GS execute N-1. Preferences > Display enables
  the saved runtime request; compatibility modes and bounded overflow use real
  synchronous fences. Host/sanitizer checks, native/Docker builds and emulator
  driving, scene transitions, display changes and GPU readback passed.
  Corrected 1.169.1 physical timing passed two fresh boots per pose, using
  the SAME ELF within each pose. Day reduced full period from approximately
  28.1 to 17.152 / 17.185 ms (35.6 to 58.30 / 58.19 Hz), with
  13.863 / 13.865 ms critical-path work and 0/512 over-budget samples per boot.
  Night measured 18.114 / 18.173 ms work but retained 33.403 ms periods
  (29.94 Hz), with 512/512 samples above 16.667 ms in both boots.
  Preserve earlier pre-gameplay `freepad: DMA Busy` launches and the pre-fix
  procedural loading failure (2,160 frames / 98,304 valid chains) as separate
  historical evidence.
  **Loading-race fix in 1.169.1:** the quiet physical control exposed a stuck
  VIF1 DMA. Waiting the direct DMA channel after prior hybrid presentation
  prevents a new send from overwriting the copy started by that callback.
  The corrected control passed 10,920 frames; a subsequent build without
  watchdogs or loading-stage prints passed 10,440 frames and five complete
  scene cycles (15 transitions), with 765,952 valid chains and zero fallback.
  Physical timing and repeated transition acceptance are complete for the
  historical 1.169.1 runtime. The current default policy above is implemented;
  physical button-to-TV latency and current
  post-Memo quiet/Showcase reverse qualification remain separate checks.
  The [private latency fixture](tyrax2-latency-fixture-2026-10-02.json) passes
  initial host checks and native compilation; PS2 and PCSX2 completed 5,400
  loops, but V1 mapping was rejected due to absent/stale legacy-off observer
  tags. V2 passes actual-header host checks, native compilation and all39
  software mappings in full emulator and fresh PS2 runs. Physical synthetic
  event-to-display-register medians33.276/66.623/33.276ms show about33.35ms
  extra in this controlled configuration;
  manual physical input/TV-image latency remains unmeasured; these dated
  diagnostic results do not establish universal latency or 60 FPS.
  The [runtime record](tyrax2-runtime-2026-10-02.json) distinguishes final results
  from historical arms. The older GPU-only sweep is not a current gain ceiling.
  Reference-title SPR staging and its approximately 16-qword-plus-CALL object
  scheme remain separate future work; this implementation does not add them.
- **TyraX2 authoring integration — audited; PAL fix shipped in 1.169.2.**
  Standard flow nodes/generated loops already use engine ordering and resource
  fences; custom DMA/readback guidance is now linked from authoring/AI guides.
  Legal Set Display Mode 4 now survives codegen; FPP/ORBIT native builds and
  emulator mode4/timeout rollback pass. Pipeline setter/query nodes
  are implemented in 1.172.0: deferred boundary application, unchanged-request
  no-op, and requested-state getter. Both require rebuild; requested state is
  not active overlap. See [flow nodes](frame-pipeline-flow-nodes.md).
  See [integration audit](tyrax2.md#authoring-integration-audit-2026-10-02).
- **Automatic interleave selection under TyraX2 — settled-block correction
  integrated in 1.171.0; historical attribution limits retained.**
  The old alternating scorer can label current EE plus pending previous GPU
  cost as one current-order sample; split/mode/scene interruptions can leave
  stale marks. Source mismatch does not establish bias or clip-regression cause.
  [Pinned controls and discarded-pick census](tyrax2-interleave.md) pass native
  build and 13,200 emulator/physical frames. Physical picks are all retained;
  pinned work 18.281/18.028/19.200 ms has 0.919 ms plain-control spread and all
  arms remain approximately 30 Hz. An identical-ELF fresh repeat measures
  18.392/17.988/18.346 ms, a positive 0.358–0.404 ms interleave indication
  against 0.046 ms plain spread. Full animated-input equality remains open;
  these historical trials did not accept a production selector fix. The current
  selector uses reversed homogeneous blocks with two transition intervals
  discarded, exact output/view/recording-generation guards and diagnostic
  exclusions. Actual-source host controls pass; this correction does not price
  pure CPU/GPU work or establish a night FPS gain.
  A [private replayed visual-clock control](tyrax2-pinned-clock-2026-10-02.json)
  passes 13,200 physical frames with all 11 pinned sampled inputs matching:
  plain/IL/plain 17.521/17.308/17.523 ms, about 0.214 ms benefit vs 0.0012 ms
  plain spread, still 30 Hz. No emulator, full pixel/query identity or production
  clock change follows from that historical control; the integrated selector
  preserves ordinary game clocks and its current target evidence is separate.
- **Remaining EE work / VU0 offload — audited, experiments open.**
  The [scene preparation isolation](tyrax2-scene-isolation.md) now passes both
  private Full→Full and Full→terminal-sink emulator protocols plus three physical
  boots with matching cross-boot guards: full controls near 33.33 ms, terminal
  preparation near 15.48 ms. Observer cost remains unpriced; isolate consumer
  completion/backpressure and dominant preparation groups next. The original
  empty-log launch/retry and subsequent power-reset recovery remain archived.
  The [owned frame capture precursor](tyrax2-frame-capture-2026-10-03.json)
  passes emulator and PS2 with all 1160 REF payloads owned and actual program
  bytes stable. Next qualify an explicit VIF/VU/GS startup capsule and fixed
  target before replay service measurements; bare draw-finish MSCAL is unsafe
  without its required GIF payload.
  [Terminal replay](tyrax2-frame-replay-2026-10-03.json) now passes 256
  repetitions and exact VU startup state/data checks in both PCSX2 and PS2.
  Direct GS readback resolves conflicting window images: each ordinary/replay
  pair has identical 448-by-448 rasters. Full per-submit EE validation is
  included in its interval. The same-ELF [pricing control](tyrax2-replay-pricing-2026-10-03.json)
  now passes both physical stage orders: Full near 23.33 ms, Sealed near
  10.73 ms, exact raster pairs and real completion. The roughly 12.60 ms
  difference includes scanner policy/cache effects, not isolated instruction
  or GPU time. Next qualify actual direct-producer activation, fallback and
  ordinary raster equivalence on the current engine before pricing any gain.
  [Direct-producer activation](tyrax2-direct-producer-activation-2026-10-03.json)
  now passes 5400 ordinary-clock loops on PS2 and PCSX2 after a diagnosed
  reserved DMA-padding mismatch; positive commits and zero invalid packets
  prove real use. The [fixed raster comparison](tyrax2-direct-producer-raster-2026-10-03.json)
  now passes both physical and emulator orders with exact GS image pairs.
  It rejects a speed gain: On adds 0.828–0.841 ms nonpacing in all four physical
  brackets. Keep the candidate private/Off; audit remaining passes/encoding
  before broader motion/resource checks, native profiles and ordinary FPS trials.
  The [ordinary overlap control](tyrax2-direct-producer-overlap-2026-10-03.json)
  also completes both physical orders with common sampler 0: On nonpacing
  regresses 0.676–0.780 ms, rendered completion stays about 29.97 Hz. Keep it
  Off; next audit mutable REF copy ownership/volume and genuine direct emission.
  The [private mutable REF census](tyrax2-mutable-ref-census-2026-10-03.json)
  now qualifies both orders on PS2/PCSX2: 171840 mutable bytes/frame, including
  Pool39840, Bag29952 and Unknown102048; observer adds 0.841–0.847 ms in the
  serialized fixed Scene. Its historical Unknown category included retained hits.
  The [retained-hit census](tyrax2-mutable-ref-retained-census-2026-10-03.json)
  now qualifies both orders on PS2/PCSX2: Pool39840, Bag129360, Unknown2640
  bytes/frame, unchanged total171840. This matches98.46% of copied bytes;
  its own observer adds0.945–0.950 ms. The [road-owner census](tyrax2-road-owner-census-2026-10-03.json)
  now completes both PS2/PCSX2 orders:66 road chunks/198 ranges/1288080 used
  bytes, but zero copied-original-road bytes in every physical On sample.
  Its own observer adds1.073–1.076 ms; common untimed guards are not priced.
  Do not implement original-road borrowing for this view. The [selected static-base census](tyrax2-static-owner-census-2026-10-04.json)
  qualifies both PS2/emulator orders: all seven parts admitted, 21 ranges and
  22176 used bytes, but zero copied selected-base bytes. Its observer adds
  0.978–1.044 ms; common off-span guard/snapshot cost remains unknown. This
  is a seven-allocation subset result, not absence of static rendering. The
  [producer partition](tyrax2-producer-origin-census-2026-10-04.json) now completes
  both PS2/emulator orders: UnknownBag86448, LightReceiver34272, Terrain2304,
  Beam1728, Corona2304 and Particle2304 bytes partition Bag129360. Original
  counters match V4; its own observer adds1.526–1.533ms, common costs unpriced.
  Unknown producer coverage remains; no seal/retirement or borrowing is granted.
  The [minimal physical scopes](tyrax2-minimal-scopes.md) now qualify both orders: terrain2.858–2.866ms, wheels0.304–0.306ms and first terrain core1.103–1.113ms inclusive. Wheel preparation is a smaller priority. Eight further boots qualify [Scene/completion/flush and the same-frame Core aggregate](tyrax2-frame-core-scopes-2026-10-04.json): about14ms lies inside Core, with3.28–3.54ms inclusive Scene residual. Next narrow the inner Core route; Renderer begin/end remain unmeasured by these pairs. Common observer work remains unpriced and phase contrasts change sign; no uniform subtraction or60FPS gain is accepted.

  The [inner-work scopes](tyrax2-minimal-scopes.md#selected-inner-work-stripped-packages) and [dated record](tyrax2-inner-work-scopes-2026-10-04.json) qualify V5 stripped-package Work (1.126–1.149 ms) and V6 list-package Work (0.827–0.831 ms), plus the V7 dispatch tail. V7 dispatch-tail Work averages 7.167–7.329 ms; same-frame Scene minus that tail is 10.643–10.698 ms inclusive. Narrow the remaining routes using their own source/caller and dynamic clock controls before selecting an optimization. These are different intervals and apparatus versions: no cross-version gain, pure EE/GPU bill, uniform observer fee or ordinary 60 FPS acceptance.

  The [joint night ablation](tyrax2-night-ablation.md) passed source/native,
  emulator restoration and both physical sampler/joint orders. The simplified
  five-family cut reduces elapsed-minus-pacing from approximately 18.8–18.9 to
  13.37–13.39 ms and engine flip-return periods from 33.37 to 16.68 ms.
  The private V3 gates separately qualify receiver pools, beams/coronas and glow.
  First-order add-backs measured lighting/extras at +3.94–3.99 ms versus
  shadows/particles/post at +1.39–1.49 ms against their own controls;
  these are not additive bills. Reverse add-back orders remain open.
  All three private V3 subsets passed both emulator and physical orders:
  pool cuts save 0.875–0.968 ms, beams 0.681–0.782 ms, glow 0.296–0.530 ms
  against own controls; all individual cuts retain approximately 33.37 ms
  presentation periods. The private pool color-only candidate passed source/host review
  and EE translation-unit compilation. Its later same-ELF kind4 trial passed
  native/emulator/both physical protocols with positive color-only target
  activation, but small mixed timing responses do not qualify a stable gain
  or promotion. Keep it private; investigate a larger measured route before
  further optimization work. Full-window hit rates remain unmeasured.
  Production full-night quality/performance and physical
  visual restoration remain unqualified; no observer fee is subtracted.

  The [private unconventional experiments](tyrax2-wild-experiments.md) add
  an independent fifteen-idea review. Four frustum planes through VU0 passed
  both emulator and physical orders with positive sparse action but no gain;
  keep it private. The squared cone predicate is unreachable in the selected
  scene, so it received no physical pricing. The private [Pool2 color table
  experiment](tyrax2-pool2-colors.md) completed both physical orders and twelve
  fixed-case emulator captures; six decoded VU payload pairs match. Its small
  forward improvement did not repeat in reverse order, so it stays private.
  Original EE color expansion remains. Promotion still needs repeatable gain
  and broader precision/clipping/replay coverage; unique-grid representation
  remains unimplemented and needs its own qualification.

  Storage classification alone grants no immutable borrowing rights.
  The [resumed candidate audit](tyrax2-ee-vu0.md#next-60-hz-candidates-host-controls-2026-10-03)
  rejects the broad beam cache as a demonstrated driving gain, identifies
  saturated-flicker over-invalidation, and records a private direct-recording
  host smoke for inline uniforms plus immutable whole-baked references.
  Next qualify producer coverage/ownership and native-prefix pressure before
  target integration. The [current paired quiet baseline](tyrax2-postmemo-quiet-2026-10-03.json)
  now completes both physical orders with 384 samples and about 29.97 FPS;
  observer cost remains unresolved. Both [front/tail physical orders](tyrax2-front-tail-2026-10-03.json)
  also complete. The [RemotePad-free control](tyrax2-front-tail-no-remotepad-2026-10-03.json)
  now completes another 10,800 physical loops: front means 0.724–0.744 ms,
  tail means 36.376–36.384 ms. Boundary price remains unresolved because
  phase deltas change sign and controls drift. Next qualify the private direct
  producer's native macro matrix, lifecycle/fallback coverage and prefix
  pressure, then price its net delta in matched physical orders. Host checks
  pass 547 ledger and 13 queue controls; EE layout is compile-only verified.
  No additional FPS gain is accepted.
  The [native-sizing reuse candidate](tyrax2-ee-vu0.md#native-sizing-reuse-paired-physical-candidate-controls-2026-10-03)
  now passes two same-ELF physical orders, 10,800 loops and 768 accepted samples.
  Reusing successful snapshot tag counts removes 106 sizing scans per sampled
  frame, with 0.147–0.158 ms less observed work versus outer-arm spreads of
  0.004–0.009 ms. Periods remain approximately 33.367 ms. Count reuse is integrated
  with successful-copy metadata and retry/eligibility guards; post-Memo actual
  production motion passed native/emulator/physical chain controls. Ordinary-clock
  production timing remains separate; no 60 FPS gain is claimed. Memo16 is also
  integrated after paired private controls showing 0.296–0.304 ms less diagnostic
  work, including its 128-byte EE scratch; this is not a quiet production delta.
  [Current physical attribution](tyrax2-ee-vu0.md) finds 2.724 ms snapshots
  and 0.743 ms conversion nested in 3.665 ms submission, but only about
  0.003 ms existing completion waits. VU0 already performs macro-mode
  vector/matrix/bounds/skin work. First price one-search immutable borrowing
  and subdivide snapshot/native passes; then census scalar plane transforms,
  AABB planes visited and live-light selection. A micro-mode rewrite needs
  shared program/register ownership and setup/transfer pricing. Neither fewer
  searches nor this single diagnostic boot establishes a new FPS gain.
  The [one-search trial](tyrax2-ee-vu0.md#one-search-borrowing-trial-physical-ps2)
  passed host/native/emulator and one physical baseline/candidate/baseline boot:
  18.468 / 18.435 / 18.556 ms work, all 29.94 Hz. The apparent saving overlaps
  control variation; keep the candidate private and prioritize snapshot-stage
  attribution. No production optimization is accepted.
  [Bounded snapshot attribution](tyrax2-ee-vu0.md#bounded-snapshotnative-breakdown-physical-ps2)
  is now complete: preflight/fixups 1.882 ms, mutable copy 1.056 ms and chain
  copy 0.266 ms in the enabled diagnostic, with 0.252–0.431 ms observer work
  increase against gate-off controls. Next price fewer validation/copy passes
  and use the completed exclusive math census to rank candidates.
  Keep pristine/compiled-out controls and normal batching; these counters do
  not establish a new production timing or a VU0 gain.
  [Exclusive math census](tyrax2-ee-vu0.md#exclusive-ee-math-census-physical-ps2)
  completed 6,600 frames: eight-plane transforms 0.377 ms with 11 duplicate calls,
  selection 0.457 ms with 912 candidates/frame, local preparation 0.230 ms.
  Private per-render clip reuse passes 240,328 host comparisons, native build,
  6,600 quiet emulator frames and separate movement/camera/scene cycles.
  [Candidate physical timing](tyrax2-clip-reuse-2026-10-02.json) is complete:
  18.248 /19.137 /18.411 ms, all 29.94 Hz; do not integrate the observed regression.
  Cause remains unresolved. Physical discarded light-pick activation is zero in
  this stationary pose (114 retained picks, 912 candidates/frame). Next count
  adjacent enabled affine-inverse inputs and exact picker query repeats, then
  price key/observer overhead before a small cache. Public mutable light state
  prevents a setters-only epoch or once-per-frame frozen picker key. All
  diagnostic arms still deliver approximately 30 Hz.
  [Enabled inverse repeat census](tyrax2-ee-vu0.md#enabled-affine-inverse-repeat-census-physical-ps2)
  passes host controls, native build and 6,600 emulator/physical frames:
  60 finite repeats among 71 enabled calls/frame. Physical work off/on/off
  18.191/18.492/18.239 ms gives 0.253–0.300 ms observed census increase;
  all arms remain about 30 Hz. Separately price inverse/key work before reuse;
  the whole preparation bucket is not an inverse-only saving estimate.
  The [separate inverse/key cost census](tyrax2-ee-inverse-cost-2026-10-02.json)
  passes native compilation and all11,000 emulator frames across five blocks,
  with exact warm activation and complete windows. Physical11000frames also
  pass: conservative key0.138159ms versus inverse0.076089ms/frame in scoped
  diagnostic. Repeat inverse is only0.063533ms; no output cache or frame gain
  accepted. Prioritize complete timeline attribution over this key candidate.
- **TyraX2 timeline attribution and observer controls — physical controls measured, attribution open.**
  [Hierarchical capture and controls](hardware-profiler.md#hierarchy-and-observer-controls-1170)
  reserve parents on entry, separate exclusive EE wall time from waits/pacing,
  retain actual CPU recording/presentation source jobs and reject dropped or
  inconsistent captures. Compiled-out, runtime-off, coarse, detailed and optional
  register-snapshot controls remain distinct. The private seven-arm fixture
  holds the ring outside the timed windows and excludes export-contaminated
  samples from every arm. Eight-frame emulator captures pass strict hierarchy
  validation (104/20,048/23,424 events); the earlier overflowing 32-frame detail
  captures are rejected. See [validation evidence](hardware-timeline-v2-2026-10-02.json).
  First physical 15,400-frame calibration passes all arms and strict exports:
  observed work increase versus adjacent runtime-off controls is 0.212–0.216 ms
  coarse, 3.124–3.197 ms detail and 3.614–3.637 ms detail with states. Whole-engine
  wall stays around 33.36 ms while work consumes pacing headroom. Compiled-out
  physical comparison now passes 15,400 frames: runtime-off work is
  0.887–0.966 ms higher at matching offsets. This includes code layout, held ring
  and separate boot order; it is not isolated hook cost. The identical
  instrumented return completes another 15,400 frames: matching controls remain
  0.916–0.931 ms above compiled-out, with 0.004–0.036 ms instrumented control
  spread. This brackets a repeatable combined difference, not the ring/hook
  split. Pre-asset ring reservation is now measured separately; refreshed
  coarse Scene coverage is now measured separately; broader controls remain open;
  [Same-ELF ring control](hardware-timeline-ring-control-2026-10-02.json) is
  prepared: native build and both complete 5,400-frame emulator runs pass,
  384 independent samples each with trace inactive. Declared state digests
  match, real physics remains live. First physical reservation0 boot passes all
  5,400 frames and 384 samples: work means 17.556/17.664/17.682 ms. Reservation1
  and reservation0 return also pass all samples. In one same-ELF 0/1/0 bracket,
  reserved work is 0.069–0.101 ms lower than both controls, whose spreads are
  0.0006–0.0193 ms. All 45 observed addresses move and restore, declared states
  and size/capacity topology match. No universal ring/hook bill or subtraction
  from older ELF gaps: layout interactions, live physics and one bracket limit
  transfer. Mode1 original cfg bytes were not archived before replacement;
  runtime mode/reservation records remain valid.
  [Selected inactive dispatch](hardware-timeline-dispatch-control-2026-10-02.json)
  retains two rejected live-dt ABA boots with camera drift and one passing BAB;
  this is not an accepted two-order cost.
  [Fixed game dt](hardware-timeline-dispatch-fixed-control-2026-10-02.json)
  passes both orders but reverses the work-delta sign: the middle phase is
  heavier in both boots, so stable routing cost remains unresolved.
  [Continuous scene and a common sample buffer](hardware-timeline-dispatch-continuous-control-2026-10-02.json)
  pass both orders (5400 loops/384 samples each), with exact state/dt/address and
  export-before-reuse checks. Bypass lowers measured work by 0.130–0.134 ms;
  outer-control spreads are 0.003855/0.001551 ms. This is a local net routing
  effect from one boot per order, retaining common observer work. Removing two
  variables together does not identify the earlier phase effect's cause.
  [Fresh coarse Scene attribution](hardware-timeline-scene-coarse-2026-10-02.json)
  now passes both physical orders: 192 raw/186 selected samples, three strict
  captures (32 frames/448 events each; first31 selected). Scene is
  17.680–17.705 ms, Update 1.439–1.452 ms and remaining Game 0.386–0.399 ms.
  Coarse activation adds 0.069–0.097 ms renderer work with outer-control spreads
  0.022251/0.020429 ms. Exact named state/camera/dt and accessible addresses
  match within/across boots. This is a fixed-workload elapsed EE envelope, not
  pure arithmetic or VU/GS utilization.
  [Aggregate bag census](hardware-bag-dispatch-census-2026-10-03.json) now passes
  both 5400-loop physical orders with all 768 raw/metrics pairs. Three active
  windows have 135 calls each (one culled, 123 direct, 11 partial; 122 replayed),
  exact exclusive coverage and matching state/camera/dt/accessible addresses.
  Cache opening plus actual replay is 3.526–3.534 ms, dispatch 2.848–2.859 ms,
  object data 2.180–2.192 ms and bounds 2.044–2.051 ms. Selected bags total
  15.432–15.450 ms inside 18.514–18.518 ms Scene; the residual includes other
  work and observer aggregation. Net activation adds 0.779–0.784 ms renderer
  work in both orders, retaining common instrumentation. Two incomplete UDP
  attempts are rejected; reliable file export occurs after measurement.
  [Replay split](hardware-replay-census-2026-10-03.json) now passes both 5400-loop
  physical orders with 768 complete pairs and matching selected state/address
  controls. Baked opening is 1.053–1.075 ms; WholeReplay 2.746–2.758 ms contains
  a 2.249–2.253 ms conditional flush child (81.8% pooled), leaving 0.497–0.506 ms
  outside it, including observer seams. Retained opening is 0.125–0.131 ms.
  Counts remain 122 successful replays and 86 actual child flushes per frame;
  flush submits a shared packet, not only the trigger bag's geometry. Net
  activation adds 1.401–1.408 ms renderer work in both orders, retaining common
  instrumentation; no cross-ELF subtraction or saving is inferred.
  [Selected send census](hardware-send-census-2026-10-03.json) removes broad bag
  clocks and accepts both complete physical orders with 768 raw/metric pairs,
  matching selected state/address/buffer controls and 86 owned successful
  flush/send/snapshot/native calls per active frame. Submit is 2.320–2.326 ms;
  its snapshot child 1.166–1.170 ms, native sizing 0.168–0.170 ms, capacity
  0.083–0.084 ms and emission 0.440–0.452 ms. Selected packets copy 30,000 bytes
  and borrow 2,118,944 bytes/frame; observer activation still adds 0.784–0.805 ms.
  Explicit emulator85/PS286 contracts preserve the initial emulator rejection;
  parser record-count bounds were independently corrected. No pure arithmetic,
  cross-device workload equality or performance gain follows from that census.
  Subsequent same-ELF native-sizing reuse controls included candidate counting
  and ABI costs and are recorded above; integrated ordinary-clock cadence is
  qualified separately.
  Broader repeats remain open. Earlier seven
  warm samples per arm in a single boot are exploratory only. No constant
  overhead correction, VU/GS utilization or EE arithmetic-only cost is inferred.
- **Night light-effect cost after TyraX2 — measured, optimization open.**
  A [physical one-group isolation run](tyrax2.md#night-workload-isolation-physical-ps2-2026-10-02)
  brackets seven stationary garage variants with full-night controls of
  18.377 / 18.574 ms work. Removing scene pools/beams saves 1.870–2.067 ms;
  registered live lighting saves 1.059–1.256 ms; eleven night-dressing boxes
  save 0.742–0.939 ms. These single-boot ranges are non-additive. Projected
  shadows and vehicle headlight ground pools show no saving beyond control
  spread in this view. Every individual arm still delivers approximately
  30 Hz. The [subsequent split](tyrax2.md#separate-pool-and-beam-costs-physical-ps2-2026-10-02)
  is complete: pool removal saves 1.017–1.131 ms, beam/corona removal
  0.770–0.884 ms. Nested submission dominates both diagnostic function brackets;
  beam arrays do not rebuild, but surrounding assembly/comparison still costs
  about 0.25 ms/frame. Next prototype a correctly invalidated unchanged-state
  beam shortcut and inspect pool-batch key changes; separately price submission
  and overdraw, then live-light selection. These are still open experiments. Keep
  the image and measure full period, not just renderer work, before calling
  this a night performance fix.
- ~~**The 0.5 ms sleeps in `RendererCore::beginFrame`/`endFrame`**~~ **SHIPPED
  1.128.3 behind a runtime gate** (`RendererCore::setFrameYield`, off; the
  generated game turns it on only while the Live Debugger is attached):
  forcing the old sleep back measured +1.03..+1.05 ms of `work` in all four
  poses, and the gated build ran 10 min clean with the debugger attached and
  music streaming. Still open: WHY the sleepless loop hangs with the debugger
  attached - the history: `Threading::switchThread()` is
  `nanosleep(500 us)`, called twice a frame, a leftover from upstream. Since
  2026-07-11 the game runs at priority 0x40, below the audio threads (0x5,
  0x6) and ps2link's command thread (20). Removing both sleeps measured
  **-1.03..-1.07 ms of `work` in all four Motor District poses** on a
  physical PS2, two boots agreeing to 0.03. But it hangs the console (IOP
  alive, `freepad: DMA Busy`, SIF stuck, ps2link reset useless) about a
  minute into `showcase` when the Live Debugger is attached and music
  streams over ps2link:
  - new engine, `livedbg.cmd` present: hang, 2 of 2 runs;
  - new engine, no `livedbg.cmd`: 10 min clean;
  - old engine, `livedbg.cmd` present: 150 s clean.

  The attached debugger writes `livedbg.bin` from the main thread while the
  streamer reads the song. The fio client is locked
  (`_fio_io_sema`), and no EE thread sits below the game, so the mechanism
  is NOT found. The patch is in the working notes
  (`C:/tyra-vq/nosleep.patch`). Worth the hunt: it is the largest single
  EE saving measured this round.
- ~~**Interleave the GPU-heavy passes with the object loop**~~ **SHIPPED
  1.128.0** as Preferences > Rendering > Interleave batches and roads with
  objects (Auto / Always / Off, docs/interleaved-passes.md): Auto measured
  -0.48..-0.50 ms of `work` in the garage and +0.01..+0.04 outside. Left
  open: the +0.20 ms of `prepare` it costs (D-cache, two alternating working
  sets). The prototype history: Terrain, batches and roads are where the EE waits (0.89 /
  0.66 / 0.43 ms of garage day's 2.19); objects wait 0. Deferring their bags
  into the object loop measured -0.29..-0.41 ms of `work` in the garage and
  +0.22..+0.40 outside, with an unexplained +0.42..+0.49 ms of `prepare` in
  every pose (docs/ee-submission-rearchitecture.md, "Where the EE waits, pass
  by pass"). Next: roads + batches only, the `prepare` increase priced in the
  attribution build, and a translucency gate (flush before the first
  possibly-alpha-blended object) before anything ships. Do NOT drive
  `Vif1Queue` from game code: that probe hung a console beyond ps2link reset.
- **The per-bag `prepare` bracket (2.0-3.0 ms).** Partly done in 1.127.2:
  uniform blocks, and since 1.127.3 the options block, are a cached header
  plus whole-qword copies (docs/ee-submission-rearchitecture.md, "Round
  three"). The clip block is REF'd from one shared copy since 1.127.5 and
  skipped when no VU1 clip path can run since 1.149.1. Inactive spot uploads
  are skipped too. The in-chain wrap write still uses packet2. Retaining each
  bag's uniform groups was tested on a PS2 and **rejected**: despite lower
  dispatch and VIF wait, `prepare` rose 0.3–1.0 ms and total `work` rose
  0.30–0.80 ms ("Round six"). Target new work at a measured sub-bracket.
- **The DMAC-interrupt variant** (`TYRA_VIF1_QUEUE_ISR 1`) crashes a real PS2
  and is off. Only worth reopening with the kernel's DMAC handler chain
  inspected on hardware, under ps2link, since PCSX2 ran it clean. It would only
  close the gap between one chain's end and the EE's next submit or wait.

## ~~Guard-band bags take the slow package route~~ DONE 1.124.2 (2026-09-23)

Whole bags inside the guard band now take the direct route: -0.83 ms of `work`
at the 25 FPS spot (docs/vu1-clipping.md, "Whole bags inside the guard band").
The original entry:


A bag that is not wholly inside the view but whose out-of-view packages stay
inside the guard band is dispatched through the generic `renderPkgs` route
(`Obj_ds_render` + `Obj_ds_flush` ~0.07 ms an object on a physical PS2), while a
wholly visible bag takes the direct whole-IN submission (~0.012 ms). Those
packages are culled whole and by pointer either way - no clipper runs - so the
route difference looks like overhead, not work. At the Motor District start pose
three objects pay it for ~0.3 ms of EE (apron, loft block, wall). Find what the
direct route requires that a guard-band-only bag lacks, and whether an
"every package is IN or guard-band" bag can take it. Measure with the
`Obj_*` capture rows (docs/profiling.md, "The game side of the object loop").

## Vehicle lamp glow: next (2026-09-26)

- The editor viewport does not draw the halo yet (the light beams' corona has
  a viewport twin, drawLightBeams; the vehicle one should join it).
- Brightness does not know day from night: a day scene could scale it by the
  ambient level.
- Occlusion is the depth test alone - a halo whose lamp is hidden behind
  another car still shows around it.

## Vehicle damage: what the first version left out (2026-09-26)

Damage shipped in 1.138.0 (docs/vehicles.md, "Damage"). Queued, roughly in
order of what a player would notice first:

- **Physical-PS2 price of a hit.** 1.2-2.3 ms per dent is a PCSX2 number, and
  PCSX2 has no EE data cache; the rewrite reads a cold rest copy. Measure the
  hit frame on a console (`VEHDMG ... us`) before spreading a dent over frames.
- **Re-shade the dent.** Baked vertex colours keep the undented normals, so a
  dent reads through shape and scuff only. A per-part flat re-shade of the moved
  triangles at hit time is one-shot EE work in the same place.
- **Dent the far tier too**, or fall back to tier 0 for a damaged car inside
  twice its far distance - an obviously wrecked rival pops back to pristine on
  its traffic tier.
- **Loose parts, the rest** (bonnet/boot/doors/windows came off in 1.139.0;
  debris kicked by cars, walls and a 60-unit cull in 1.140.0): debris against
  debris and pushing back on a car, bumpers, a wheel that wobbles or
  comes off (camber on the struck corner - the wheel batch already composes a
  per-wheel transform), and a repair that picks the debris up again.
- **Author-named pieces.** The classifier is geometric; a model whose
  materials or nodes say `hood`/`door_l` should be obeyed first (the lamp
  materials already work that way).
- **A crash sound**: a one-shot on the VehicleDef, volume from the impact speed,
  like the shift blip.
- **Flow-graph reads**: a Vehicle Damage number node and an On Vehicle Wrecked
  trigger, so a race can end on a wreck. Live Logic cannot patch Repair Vehicle
  yet.
- **AI reaction**: a rival at 100% with power loss 1 sits dead on the racing
  line; it should pull over or be removed.

## More than one car in view: what is left (2026-09-25)

The shine budget shipped in 1.132.0 (docs/vehicles.md, "The shine budget").
The same console breakdown of a second car (a parked Ravager 9 units away)
leaves these, dearest first:
- **The matte car still costs ~1.4 ms** (2.1-2.6 ms whole, minus the shine),
  mostly VU1/GS: body 1938 triangles, blob, lamps. BUILT in 1.134.0, NOT PRICED:
  the Ravager now has an authored far model (708 triangles with wheels, 2
  submits) that every car nobody drives shows from 12 units (docs/vehicles.md,
  "An authored far model"). Owed: the orbit-fixture A/B on the physical PS2
  (`make_mc_arm.py` in the working notes) with `trafficDistance` 5 vs 0 at the
  9-unit parked Ravager - the number that says whether 12 is the right default,
  and whether the CC96 wants an authored far model too (its decimated tier is
  1574 triangles). The district now parks two more authored-far cars at the
  spawn (Pica Turbo 652 / Strix V12 668 far-tier triangles, 4.5 units either
  side of the driven Ravager), so the spawn pose itself is a second fixture for
  that A/B; the CC96 is no longer placed.
- **Wheels, 0.27-0.39 ms a car**: NOT an EE rebuild for a parked car - the
  rebake signature skips it (`WHEELBAKE` on the orbit rig: 300 of 900
  wheel-sets rebuilt per 300 frames, all of them the driven car's). The cost
  is VU1/GS on 4 x 300 strip vertices, and the `@auto` fast wheel pads to the
  same 300. Left: a coarse traffic wheel asset of at most 75 strip vertices,
  and the driven car's signature, which never settles to the bit while parked
  (docs/vehicles.md, "Per-car EE cuts").
- **Paint colour rebuilds, 0.38-0.44 ms a shining car** while the camera
  turns (measured). Moving them to VU1 was tried and is SLOWER (docs/vehicles.md,
  "Where the shine's cost is, and one dead end"). BUILT 2026-09-26, console
  price owed: a distance-grown step for cars nobody drives and a
  one-rebuild-a-frame cap (modes 52/53 of `make_cc_arm.py`). PCSX2: 393 -> 360
  rebuilds per 600 orbit frames on the rig, where only one shining car is not
  driven.
- **Per-car small parts cost more than their triangles** (docs/shadows.md,
  "Blob cost"): the blob is 0.35 ms by day for 18 triangles, and the lamps and
  lights 0.25 ms. Caching the blob patch and the lamp colours bought
  0.02-0.09 ms and ~0.07 ms. BUILT 2026-09-26, console price owed: headlight
  pools and lamp halos write on change (they re-staged every frame; now 0 of
  300 re-stamps parked, mode 54). The blob has no EE work left to cut: its
  remaining cost is `vif_wait`, and one shared bag would need one texture for
  every car's mask.
- **Sub-step reuse at 25 fps**: the second `stepVehicles` sub-step cost
  +0.48..+0.52 ms of `work` (physical PS2, district fixture, forced). BUILT
  2026-09-26, console price owed: the collider list and a per-car candidate
  subset are reused, and the body matrix, chase camera and engine note are
  written once a frame (mode 43 vs 56 of `make_cc_arm.py`); exact, with an
  in-game oracle (docs/vehicles.md, "Per-car EE cuts").
- **Night lighting passes** (garage night, physical PS2, all lamps removed per
  pass): the scene spot pools still cost ~0.74 ms for eight lamps after
  1.134.4's cache, which is one additive bag per lamp. Merging the static
  lamps' pools into one bag, with the per-lamp FIX folded into vertex colours,
  is the candidate. (1.134.5 then merged the still pools into one bag:
  -0.15 more.) The light beams cost 0.54 ms in garage night, measured by
  removing each half (median, two boots):
  - the coronas are 0.31 ms: 8 camera-facing sprites rebuilt on the EE every
    frame, so the batch re-stages every frame. BUILT 2026-09-26, console price
    owed (mode 55): corona and cone batches write on change, so a still camera
    replays both streams (PCSX2: 0-1 re-stamps of 29-241 submits) and a moving
    one keeps the cones'. A moving camera still rewrites the coronas; the
    StaPip billboard family would expand static centres on VU1 instead, but
    its programs are not resident, so price the program-set swap first.
  - the cones are 0.23 ms: 8-triangle fans drawn with no back-face cull, so
    each silhouette is filled twice. A one-sided cone at double colour would
    halve the fill but changes the look.
  - outer night: the beams cost 0.10 ms in total.
  The district fixture itself draws the debug profiler overlay (showProfiler,
  showFps, showMemory), which is a constant in every A/B.
- **Interleave tuner decision rule** (docs/interleaved-passes.md): one run
  saw garage night alternate between ~13.9 and ~14.8 ms frames. The tuner then
  picked plain on a 4/8 tie while interleaving averaged ~2% faster. A
  median-of-pairs rule with 0.5% hysteresis was built and A/B'd over 8 boots
  on a regenerated fixture. Both rules read identical: garage night 13.79,
  and no bimodality in either. Not shipped. Revisit only if the two-population
  frames come back.
- **Driving model, after 1.135.0's grip-limited yaw** (the review's order):
  - ~~walls redirect instead of scrub, and the twins' wall shapes agree~~
    done 1.135.1 (the host's boxes are still world AABBs of rotated objects,
    so a rotated prop is fatter in the test drive);
  - ~~a fixed 1/50 s step~~ done: the test drive in 1.135.4, the console in
    1.135.6 (whole updateVehicles sub-steps: a 25 fps frame pays the vehicle
    update twice);
  - ~~a swept wall test~~ done 1.135.3 (the car-car push after the wall pass
    is still not wall-checked);
  - ~~a handbrake that loosens the rear and blends grip back; a friction
    circle~~ done 1.135.2 (its constants are code: make them definition
    fields once the feel is settled);
  - ~~a rescaled deadzone and an expo curve on the stick~~ done 1.135.3;
  - air control and landings;
  - ~~per-surface grip~~ done 1.136.0 (paved or not), per road 1.137.0
    (`roadGrip`), per painted terrain layer 1.142.0 (`grip`);
  - ~~soft road edges~~ done 1.144.0 (Edge fade);
  - ~~road crossings you can control~~ road rank + spill done 1.143.0
    (docs/roads.md "Crossings"); ~~a per-junction override~~ done 1.145.0
    (docs/roads.md "Junction overrides": material, grip, which road wins,
    keyed by the road pair + position, clickable viewport diamonds). Still
    open: the override is not dragged along when a road is re-drawn far
    enough to move its crossing past the narrower road's width (it orphans);
  - ~~car-car spin~~ done 1.135.7.
  ~~The AI steers by heading error with no speed planning~~ done 1.136.1
  (path pursuit + planned corner speed; 25-31 s laps with no unsticks). Left:
  the traffic rule can steer an overtaking car wide into the outside wall.
- **Stale entries:** the two "vehicle BODIES are still triangle lists" entries
  further down predate 1.117.4, which strips the body parts (the Ravager's
  paint part is 5490 list vertices -> 2649 strip vertices, 0.483x).

## Motor District follow-up after the integrated frozen-camera pass

### What else was `FlushCache` writing back? (2026-09-16, BLOCKING S1)

The EE-submission Probe B removed `dma_channel_send_packet2`'s `FlushCache(0)`
with the static pipeline's two packets allocated `P2_TYPE_UNCACHED` and with
every send whose chain references a qbuffer copy pool still flushing — and the
**picture came back corrupt** (1 271 of 262 144 pixels, a 14-row band at the
horizon where the road and far buildings tear into slices). The same uncached
build that still calls `FlushCache` is **byte-identical** to the control, which
localises it to the flush rather than to the allocation.

Two hypotheses, not separated:

1. another EE-written, DMA-read buffer reached by a `REF` tag that is not one of
   the copy pools (the horizon band points at the road or terrain strips);
2. `FlushCache(0)` is also supplying an ordering barrier, and removing it lets
   the DMAC start before the EE's stores have landed.

**S1 cache-flush removal stays gated until this is answered.** The 1.09 ms
measurement belongs to this older architecture, not the current single-chain
runtime. The 2026-10-02 [SPR audit and host experiment](tyrax2-spr-staging.md)
allow a narrower finalized-prefix staging control with flush retained. The
[October 4 private direct-SPR runtime](tyrax2-spr-call-runtime-2026-10-04.md)
passed its byte guard and visual check but regressed on hardware; no SPR gain
or production promotion is accepted. Re-run with `--keep-routes`:
a parked fixture cannot see a per-frame rebake writing the same bytes each frame.

Evidence, arms and recipe:
[ee-probes-2026-09-16](../examples/vehicle-playground/authoring/ee-probes-2026-09-16/README.md).

**Closed by the same round, do not re-open:** S3 (classify per 1/3-bbox part) is
**refuted** — per-package rejection buys 2.60 ms against a 1.79 ms classification
bracket, so it pays for itself, and the arm that actually coarsens the
classification measured **+4.59 ms**.

The September 14 asset pass and physical PS2 attribution are recorded in the
[example README](../examples/vehicle-playground/README.md#lean-vehicles-2026-09-14).
Lean CC96/Tristar geometry and configurable devkit cadence are complete. Next
experiments should separately reduce repeated bounds/preparation and submission,
using the saved native-raster hardware CSVs as the reference. Do not treat
disabled debug channels as a renderer improvement or mix adaptive BLSS into
this asset comparison. The indexed-bounds-cache step is complete: the hardware
bounds bucket fell about 30%, with whole-frame improvement in the day views
but roughly neutral night results. See the work plan's raw evidence and limits.
The multi-entry transform experiment was rejected; see
[its measurements](performance-transform-reuse.md). Bounded resident static
submission batching is the next completed step; see
[the physical comparison](static-submission-batching.md). **Persistent static
command data is the next completed step** — see
[retained-static-commands.md](retained-static-commands.md): a wholly visible
bag's VU1 command block and the fifteen-quadword clipping chain are captured
once and replayed with a memcpy, with the packet byte-identical by construction
and no new DMA-lifetime exposure (the retained storage is copied, never
referenced). Partially clipped geometry stayed on the current path exactly as
this entry asked. Measured in PCSX2, three boots per arm: 18 `--capture-frame`
images hash to one value, and garage day — the only pose not sitting on a vsync
division — goes 27.889 → 30.769 median FPS (35.86 → 32.50 ms, **−3.36 ms**)
against a 0.222 FPS control spread and a 0.001 FPS same-build repeatability,
with 76 % of the frame's package command blocks replayed. What it does NOT
cover, in order of what would pay:

- **The per-bag UNIFORM tail is still rebuilt**, but retaining copies was
  slower on hardware ("Round six"). Revisit only after a new attribution
  identifies a smaller block that can be omitted without cold-cache traffic.
- **`buildSpotForBag` still runs an affine inverse per bag**, but a keyed
  model/light cache was slower on hardware (+0.006..+0.125 ms). Do not reopen
  without a different cache layout and a physical-console A/B.
- **The hardware number.** Everything measured for this change is PCSX2 and
  counts. PCSX2 emulates no EE data cache, and this change trades computing
  bytes for reading them out of a cold 128 KB arena, so the emulator sees the
  removed work and none of the added misses: treat its delta as an upper bound.
- **The lifetime stress on a console.** The structural argument (copied, never
  referenced) is strong and is exactly the kind of argument the slot-pool race
  also had. Run the submission-batching stress harness — forced evictions with a
  batch pending, pipeline switches, LOD crossings, a scene reload — on hardware.

Road changes remain deferred; 50 FPS still requires a much larger reduction in
whole-frame work than submission batching and retained commands together.

### The package count is at its ceiling — one costed way past it left (2026-09-16)

Almost every term left in `dispatch` is per-package and the garage frame is cut
into 572.5 of them, so "make the package bigger" is the obvious attack. The
cheap half of it has been taken: the rounding step went from a multiple of 9 to
a multiple of 3 and the baked run with it, so the ceiling is **75**, not 72.
What is left is closed as far as rearranging memory goes — 75 is 93% of the 81
that the whole of VU1 data memory allows at six quadwords per vertex, and the
144 that would halve the count wants 1.73x that memory. The full derivation,
the per-class table, the `DBUFFER_END` sweep and the measured baseline are in
[render-submission-attribution.md](render-submission-attribution.md), "Round
three" (the bound) and "Round four" (the change). **Do not re-open "pin the
package size per class"**: every class this frame uses derives exactly 75, by
two independent routes, so a per-class run would buy nothing. What is left:

- **Reclaim the clipping scratch: 75 → 81 vertices, −8.0% of the packages.**
  Needs `VU1_STAPIP_DBUFFER_END` at 1014 or above, i.e. both Sutherland–Hodgman
  polygons *and* the six-plane table out of absolute VU1 addresses. There is
  room for them inside the clip programs' own double-buffer half, and this is
  the non-obvious part: a clip buffer's layout is **dynamic**, computed from the
  real vertex count (`stqData = vertexData + vertexCount`), not reserved at
  `maxVertCount` — so a 12-vertex clip package uses 47 of its 460 quadwords and
  leaves 161 spare against a 252-quadword worst-case fan-out. **Re-do that
  margin at 81 before starting**: round four's harness prices the clip
  footprint per class properly (uploaded streams, the real per-output-vertex
  store count, the real tag block) and the number round three quoted is not the
  one that binds — with the scratch moved INTO the half it also has to be paid
  for out of the same 500 quadwords. The cost is the objection: three clip
  images rewritten to xtop-relative scratch addressing plus their
  `src/vugen.cpp` twins (or `--vu-check` fails), the plane upload moved from
  per-mesh to per-clip-package in the packet writer **and** in the
  retained-command key, VI register pressure in `clip_tc` (269 cycles per
  triangle, the hottest loop in the pipeline), `meshstrip::kRun` re-baked to 81
  with the road/terrain run constants, and a frozen-camera console A/B. Note
  that moving the plane table DOWN into the per-mesh constants instead is worth
  exactly zero — the double buffer pays twice below it and gains twice above it.
  Note also that at 81 the rounding step is irrelevant (both /9 and /3 give
  81), so this lever and round four's do **not** add up.
- ~~**Relax `getMaxVertCount`'s multiple-of-9 rounding to a multiple of 3: 72 →
  75, −4.0% of the packages, no memory change.**~~ **DONE (2026-09-16, "Round
  four").** It cost one thing round three did not price: the relaxation also
  moves `clipPackageSize()`, and it took the untextured single-colour class to
  a **one-quadword** clip-buffer margin. `clipDivisor` went 5 → 6 in the same
  commit, which puts every reachable class back above 91 quadwords — better
  than the 35 the textured single-colour class was already shipping on — and
  leaves the clip package size of the three classes a textured scene actually
  uses (`cull_tc`, `cull_tce`, `cull_td`) **unchanged at 12**. The margins are
  derived per class by
  `examples/vehicle-playground/authoring/package-ceiling-75-2026-09-16`.
- **The only other lever is the six quadwords per vertex** — three uploaded
  (position, ST, colour) and three written (the GS reglist for a textured,
  per-vertex-coloured primitive). Whatever is attempted there, the claim to
  measure is the package count, not the payload: the September 15 probe that
  added 16 bytes per vertex moved garage-day VIF1 wait by 0.067 ms, so bandwidth
  is not the limiter and a compression pass that does not raise `maxVertCount`
  buys nothing.

### The vehicle BODIES are still triangle lists, and `meshstrip` is right to refuse them (2026-09-17)

**Superseded:** bodies strip at bake since 1.117.4, and since 1.134.0 the bake
logs it per part (`[vehicle] X: part K name L list verts -> S strip`): the
Ravager's paint is 0.483x, the CC96's 0.381x. Kept for the measurements.

The worst-packed-producer round attacked the two producers the frame inventory
named — the wheel batch and the projected-shadow receiver patch — and took
**23 of the garage frame's 711 VU1 packages**, additively, **worth a measured
0.449 ms of `work` on the physical PS2** (0.699 at night) against a two-ELF
floor of 0.064. The patch's captures are byte-identical; the wheels' differ in
54 pixels of 512x512 at one channel step, which is the GS's per-triangle setup
and not a geometry change
(`examples/vehicle-playground/authoring/wheel-strip-2026-09-17/`,
[vehicles.md](vehicles.md) "The wheel batch is a strip",
[shadows.md](shadows.md) "Almost all of it is the CASTER's geometry").

What it found on the way is the bigger item, and it is not what the inventory
predicted. `proj_shadows` is **87% the caster's own model bags**, re-submitted
from the light's point of view — 60 packages of 69 — and those bags are lists
because **no vehicle model in the district carries a strip at all**. The cars
are also 3 446 triangles of `object_submit`, and the probe pass draws them
again.

### A WELD KEY IS A PROPERTY OF THE BAG, NOT OF THE MESH

That is the general statement, and it is the transferable half of this round.
Whether a mesh strips at all is decided by **which attributes the GS actually
receives for it**, which is a fact about the bag that draws it — not about the
geometry. The same Motor District wheel mesh, unchanged:

| weld key | strips to | verdict |
| --- | ---: | --- |
| position + normal + UV (`Weld::kFull`) | **1.605x** the list | `meshstrip` REFUSES it |
| position + UV (`Weld::kNoNormal`) | **0.764x** | 10 VU1 packages a wheel against 13 |

A 2.1x swing, on one mesh, from changing nothing but the definition of "the
same vertex". **This generalises to every unlit, single-colour bag in any
project** — anything submitted with `lighting == nullptr` and a flat
modulate-identity colour has position and UV for its whole vertex, and a
flat-shaded mesh that the ordinary weld refuses may strip well under the other
key. Today the only caller is `vehbake`'s wheel; the rule is worth applying
wherever a producer is found submitting a list.

The converse is the safety rule and it is absolute: **using `kNoNormal` for a
bag that IS lit is a rendering bug, not a slower render.** Neighbouring faces
would take one face's normal across a crease.

### The projected silhouette wants a PER-MATERIAL alpha gate (2026-09-17)

`TYRA_CHEAP_PROJ_CASTER` ships at **0** and takes 60 VU1 packages to 32 on the
garage frame when it is 1 (docs/shadows.md, "Halving that, without touching the
geometry"). The only reason it is not on by default is one unanswered question:
**does this caster's texture carry alpha?**

The colour half of the change is exact and needs no gate — `pushVert` writes
alpha 128 for every model vertex, so per-vertex colour carries nothing the
coverage reads, and the runtime check already in `casterBag` refuses a part whose
vertex alphas are not all 128 (a mirror is `opacity * 128`, a portal is 70).
The TEXTURE half is what needs one: the GS modulates alpha as well as RGB, so a
caster whose texture is an alpha-tested cutout — foliage, a chain-link fence —
gets its silhouette holes from that texture and would cast a solid blob without
it.

`Texture` exposes no alpha predicate at runtime, and `.tmdl` carries no opaque
flag, so this cannot be decided where the bag is built. It has to come from the
bake, which DOES read the pixels: the natural shape is a per-part `opaque` bit
written by `bakeStaticModels`/`vehbake` and read here, at which point the knob
can default to 1 and the check becomes per-material rather than per-project.
Note `blsscorpus.cpp` already has a `cutout` notion for the BLSS bestiary — it is
the same question asked in a different place, and worth reading before inventing
a second answer.

Nothing in the Motor District needs it: its only two `shadowMode 3` objects are
vehicles with opaque palette bakes, which is why the measurement was possible at
all.

### A projected shadow on a ROAD is currently invisible (2026-09-17)

Found while building the picture gate for the item above, and it is the more
valuable half of that round. In the Motor District's garage pose the slots are
held, the silhouettes are rendered and the receiver patches are submitted — and
deleting the caster submit **entirely** leaves the capture byte-identical. The
patch is depth-tested and lands under the road its caster is parked on, so 60 VU1
packages a frame (about 19.5 us each in garage day, by the console's own
measurement) buy a shadow that is not on screen.

This is not a regression from anything; it is how `projSurfaceAt` and the patch
placement have always interacted when a caster stands on GEOMETRY rather than on
the terrain. Two things to decide: whether the patch should be placed on the
geometry it actually rests on, and whether a caster whose patch is fully occluded
should release its slot rather than hold one of four. The second is the cheaper
win and needs no new surface query.

Until one of them lands, **no projected-shadow change can be accepted on a
capture from that pose**, and any round that tries must build a known-bad arm
first — see docs/shadows.md, "A caster standing on GEOMETRY can pay for a shadow
nobody sees".

### The vehicle BODIES: 0.757x is on the table and cannot be taken yet

An additional authored CC96 source is now available for a separate importer
experiment: CC96 strip study (study files retained in Git history; the current fixture contains Ravager, Pica and Strix).
Its main body accepts the existing full-attribute stripifier (11,058 list
vertices → 4,212 strip vertices, unchanged triangles/normals/UVs). It is not
active in the scene and has not passed through the vehicle importer or console
renderer. The next step is body-strip import with lamp-order preservation and
lighting/reflection/shadow/LOD checks; the historical measurements below refer
to the older assets and do not price this new source.

`vehbake` now calls `meshstrip` for the WHEEL. It cannot for the BODY, and the
refusal is correct rather than a gap: the body is lit, so its key is the full
one, and on the full key 2 242 of its 2 280 corners are unique.

**The arithmetic, so nobody has to re-derive it.** Measured by running
`meshstrip::build` over the baked `.tmdl` parts under both keys
(`wheel-strip-2026-09-17/stripcheck-wheels.cpp` is the harness shape; point it
at `*-body.tmdl` and pass `Weld::kFull` as well):

| baked part | list verts | unique, kFull | strip, kFull | unique, kNoNormal | strip, kNoNormal |
| --- | ---: | ---: | ---: | ---: | ---: |
| `veh-cc96playground01-body` part 0 | 2 280 | 2 242 | 3 762 (1.650x) | 776 | **1 725 (0.757x)** |
| `veh-cc96playground01-body` part 1 | 888 | 886 | 1 479 (1.666x) | 324 | **645 (0.726x)** |
| `veh-cc96playground01-body` part 2 (`lamps`) | 180 | 72 | 126 (0.700x) | 72 | 126 (0.700x) |

Note the third row: the untextured `lamps` part already has shared normals and
strips under the ORDINARY key at 0.700x — so some of this is available with no
shading decision at all, and `bakeStaticModels`-style stripping of the parts
that qualify is the cheapest thing on this list. (It is also the part the bake
must never reorder: the rear/front lamp split is a corner index range.)

What the body is worth if it can be taken: the three car bodies are 3 446
triangles of `object_submit`, they are drawn AGAIN by the reflection probe, and
AGAIN as the projected-shadow silhouette (60 of that producer's 69 packages).
At roughly 0.75x the vertices that is the same reduction three times over.

Two routes, neither measured:

- **Smooth-normal welding at import, with a crease angle.** Turns the body into
  a mesh that strips on the ordinary key and carries all three passes with it.
  Concretely: weld in `vehbake`'s `collect()` before `decimateTo`, averaging
  normals across faces whose angle is under the threshold, leaving a hard edge
  above it — and the tiers must be regenerated after, because `meshlod` runs on
  the welded mesh. The objection is that it changes what the car LOOKS like,
  which is a picture decision and not a packing one, and this repo's
  crease-smoothing attempt is already PARKED for exactly that reason (the
  static-model A/B: quad-diagonal stripes on bare solids). **A picture gate
  comes first and the packing number second**, on the two day poses, and note
  that a lit re-shade will NOT be byte-identical, so the gate has to be a stated
  budget rather than zero.
- **A second, unlit vertex array for the silhouette pass only.** The shadow map
  renders a solid silhouette and reads no normal, so `renderProjShadows` could
  submit a `kNoNormal` strip of the body and leave the main view alone — worth
  the 60 packages directly, with no picture risk at all, because a silhouette
  is a coverage mask. The cost is a second RESIDENT bag set per caster: at
  0.757x of 3 348 vertices that is ~2 535 x 32 bytes ≈ **81 KB per distinct
  caster model**, plus its `GeoPart`/bag overhead, in a 32 MB machine. That is
  the same trade that stopped the probe-LOD option in
  [reflective-materials.md](reflective-materials.md) ("per-bag cost is the term
  that does not shrink"), and it should be priced the same way — RAM first, then
  a count, then hardware — before anyone builds it. Note it does NOT need a
  crease decision, which is what makes it the safer of the two.

Also still a list: the **torch's wall copy** in `renderProjShadows`, built per
frame from arbitrary receiver geometry. No sunlit pose reaches it, so this
round could not price it at all; a flashlight fixture would have to.

**And the follow-up the hardware run opened**, which is cheap and worth doing
before the next packing decision: `submit` fell 0.434 ms and the three
StaPipCore brackets (`bounds`, `prepare`, `dispatch`) explain only **0.072** of
it, so about **0.36 ms is in the unbracketed part of `submit`** —
post-process, the 2D HUD, the game-side per-object tests, and
`renderVehicleWheels`' own bake loop. Re-run `t-ctl` and `t-cand` with
`instrument-frame-cost.py --attribute` (and `TYRA_STAPIP_ATTRIB=1` for the
engine half) and find out where. It needs no new code and no new fixture — the
arms are archived — and it is what turns "a stripped producer is worth ~19.5 µs
a package" from an effective rate into an attributed one. Note also that
**packet construction did not move at all** in this run, which a per-package
model says it should have: whatever explains that is the same investigation.

### Where the remaining frame time is, measured (2026-09-15)

Two physical-PS2 experiments closed the DMA-cache question and opened the VU1
one; see [VU1 arithmetic and DMA cache-flush cost](vu1-and-dma-cache-cost.md).

- **Do not fork ps2sdk for the cache flush.** It is AFL-2.0 so a fork is allowed,
  and neither `FlushCache(0)` (a syscall that invalidates the whole 8 KiB data
  cache) nor `SyncDCache` (which walks all 128 indices in both ways) is the range
  write-back the pipeline wants. But the whole bill is bounded by 2.36 ms of a
  50 ms frame and is proportional to the number of submissions, so retained
  command chains remove most of it as a side effect. Suppressing the flush
  outright hangs the console: the memory that needs coherency is the packet, so
  an explicit-ownership design must give the packet buffers and the qbuffer copy
  pools an owner (uncached/UCAB allocation, or a hit-based write-back by address)
  rather than simply dropping the call.
- **VU1 arithmetic is worth 0.0768 ms of frame time per cycle per triangle** in
  the garage view, and it lands almost entirely in VIF1 wait, so reductions pay
  immediately without waiting for the EE redesign. The shipped `openvcl` loops
  cost 133 cycles per triangle (`cull_tc`, what this scene's static geometry
  actually runs), 130 (`cull_c`), 107 (`cull_td`) and 269 (`clip_tc`). Two
  reductions were visible in the source. **The spot-light one is DONE (1.94.0)**
  — the colour programs skip `CalculateTyraSpotLight` when the mesh's light is
  inert, taking `cull_c` 130 → 72, `cull_tc` 133 → 73, `clip_c` 230 → 173 and
  `clip_tc` 241 → 184 cycles per triangle, for **+0 / +0 / +2 / +2** on a mesh
  that IS lit and +178 words of micro memory
  ([flashlight.md](flashlight.md), "The cone costs nothing when nothing is
  lit"). The other is untouched: the static pipeline emits **no triangle strips
  at all**, so every shared vertex is transformed once per triangle that uses
  it. **That half is now DONE for baked static models** - see
  [model-pipeline.md](model-pipeline.md), "Triangle strips": zero VU1
  instructions (the cull programs' ADC judgement was already strip-correct),
  micro memory unchanged at 1862/2042, the district's models 13 176 -> 9 648
  vertices, and the garage view 76 951 -> 68 235 submitted vertices per frame
  in 56 625 -> 50 525 VU1 packages. What is left of it is listed below.
- **A BRANCH IS A SCHEDULING BARRIER, and the console said so in milliseconds.**
  The first shipped shape of the gate branched inside the loop. Four parked
  Motor District poses: garage day **−1.059 ms**, outer day −0.620, outer night
  −0.701 — and **garage night, the heaviest pose, +1.264 ms**, because the lamps
  are on at night, nearly every mesh picks a light, and the branch cost the LIT
  path 11 cycles a triangle of lost pairing. Solving the cycle table against the
  deltas puts the unlit fraction of colour-program triangles at ~39% in garage
  day and ~0% in garage night. The fix is two whole loops picked once per batch,
  the lit one byte-for-byte the original. **Whenever a gate is added to a hot
  VU1 loop, price the path that does NOT take it**, and do it on the pose where
  that path is the common one.
- **Triangle strips: what is left.** In order of what the garage view would pay
  for it. The first item is done; the rest are not.
  1. ~~**Roads and terrain.**~~ **DONE in 1.96.0** - see [roads.md](roads.md)
     and [terrain.md](terrain.md), both "Triangle strips". They were the rest
     of the frame (93 150 road vertices in 90 chunks, plus the terrain) and
     being grids they reach **0.355-0.374x** on the host fixtures rather than
     the flat-shaded models' 0.732x. No general stripifier: a ribbon's and a
     heightfield's rows ARE the strip, and the road half runs on the EE at
     scene load where `meshstrip` could not. The runtime contract is the
     models' (`StaPipBag::stripped`, `packageSize` pinned to the 72-vertex
     run). Two things the job turned out to hide. The road's own planar-span
     reduction changes which axis is long, so **collapsed spans have to strip
     ALONG the road** - taken laterally a collapsed span is exactly break-even
     and triples the GS primitives - and the **terrain checker is a per-QUAD
     colour**, so an untextured terrain (no material) keeps its list. What is
     still owed on this item: a **console** measurement, and the
     millisecond conversion that goes with it.
  2. **Distance LOD tiers.** The `.tmdl` carries the slot (`tmdl::Lod::
     stripVerts`) and the bake leaves it empty; `applyGeoLod` drops the bag back
     to `PRIM_TRIANGLE` while a tier is shown. A tier is a small fraction of any
     frame by definition, which is why it was left.
  3. **Hardware.** Everything above is measured in PCSX2 and in counts. The
     millisecond conversion is the console's, and this change has never been on
     one.
  4. **The pipeline's own triangle counters are not a geometry-equality
     check**, and two of them are outright wrong for a strip. The `triangles*`
     fields are GS PRIMITIVES, degenerate joins and padding included, so one
     surface reports 25 650 as a list and 38 427 as strips - a real 1.498x, not
     a miscount (docs/model-pipeline.md, "What the triangle counters count").
     The producers now log their own surface counts (`ROADSTRIP`,
     `TERRAINSTRIP`) and that is what an A/B should compare. Still unfixed, in
     `StaPipCore` and left alone deliberately because the static submission
     path was being edited in parallel: `recordGuardBandPackage` charges
     `package.size / 3` with no strip branch, so `guard=` is computed on a
     different rule from the `cull=` it is a subset of; and
     `recordOutsideBag` charges a whole bag `count - 2` when the bag is sliced
     into `ceil(count / maxVertCount)` runs that are each their own strip,
     over-counting by `2 * (packages - 1)`. Both are two-line fixes.
  5. **The examples' committed `.tmdl` files are still version 3**, i.e. they
     carry no strip and every example renders its models as lists until someone
     rebuilds them. That is correct rather than broken - the loader reads 1..4 -
     and it is deliberately not in the same commit: regenerating thirty baked
     binaries belongs in the periodic example-regeneration pass, not in a
     feature diff.
- **The two-loop spot gate wants its own console arm.** The shape that was
  measured is not the shape that shipped: the current one is host-verified only
  (`--vu-check` plus the `.o.vsm` cycle counts). Re-run the same four-pose A/B —
  garage night is the row that matters, and the claim to falsify is that it is
  now neutral rather than +1.264.
- **Aim a VU1 experiment at the program the scene runs.** The first arm
  instrumented `cull_td`, measured exactly zero, and looked like a null result
  about VU1; the generated game attaches a lighting bag only to dynamically lit
  objects.
- **The ~7.5 ms that was in no bucket is ATTRIBUTED** — see
  [render-submission-attribution.md](render-submission-attribution.md). The
  short version, because it changes how every earlier row on this page should be
  read: `submit_ms` is the whole `beginFrame()`..`endFrame()` block and
  `bounds`/`prepare`/`dispatch` only ever covered `StaPipCore::render`, so the
  two were never the same quantity and the "gap" was never unmeasured pipeline
  overhead. In the emulator, **97% of it is outside `StaPipCore::render`
  entirely**, and the renderScene level closes with a residual of 0.000 ms.

### Where the next three rounds should go, from the attribution

Everything below is PCSX2, garage day, against a 14.595 ms render submission,
and none of it has been on a console. Sizes are what the numbers support, not
estimates of what a fix would save.

1. ~~**`renderVehicleWheels`, 2.962 ms — 20% of render submission, and 1.970 of
   it is not submission at all.**~~ **DONE**, see
   [wheel-rebake-skip.md](wheel-rebake-skip.md). Both levers were taken — a
   slot-addressed batch with an exact per-car signature so an unchanged rig is
   not re-baked, and a sticky `bboxVersion` that is only bumped when the buffer
   really did change — plus one the entry did not name and which turned out to
   matter more for moving traffic: the body attitude, its six sines and cosines
   and the steer basis were being recomputed **per wheel**, so a car paid 176
   transcendental calls a frame where 22 suffice. `benchmark-district.py`
   grew `--keep-routes` for the second fixture the entry demanded, and the
   page quotes the parked best case and the moving realistic case side by side.
   The engine side of the `bboxVersion` half was priced independently at
   **0.618 ms of `bounds` on garage day**, 10.5 recalculations at 58.8 µs each
   (render-submission-attribution.md, "Round two"). What is still owed is a
   console repeat; see that page's Limits.
2. ~~**Package creation and classification, 3.346 ms — 56% of `dispatch`.** The
   largest single unopened box left.~~ **DONE**, together with a split of
   `bounds` — render-submission-attribution.md, "Round two". Both close. Three
   results to carry forward:
   - The box's NAME is half wrong: about half the submitted bags (53.5 against
     59.0 partial) take the wholly-visible route and are never classified, so
     for those the residual is the fill-and-cull loop.
   - The classification that does run is **1.401 ms over 572.5 packages**,
     honest 6-plus-8-plane arithmetic with a **2.31-part** merge walk. No
     obvious redundancy is left in it — which is why compacting that walk's
     stride bought 0.5%.
   - **The bbox cacher is exonerated**: 226.5 lookups cost 0.118 ms, the hash
     runs 1.44 probes per lookup, the expiry scan is 0.011 ms and NOTHING
     allocates (0 fresh entries in every pose). What cost 0.618 ms was 10.5
     forced `recalculate()` calls, which item 1 has now removed.
3. ~~**The clamped-wrap double drain, 1.730 ms of garage night** (0.299 in the
   day).~~ **DONE 2026-09-22, and the console pays a sixth of what PCSX2
   promised.** The bracket is lazy on both sides now (docs/texture-feeds.md):
   the register write and its drain are skipped when the wrap already is what
   the bag wants, and the restore is deferred to the next bag that needs
   REPEAT, to `Renderer2D`'s first sprite (which already drains once a frame)
   or to `RendererCore::endFrame` before the post-fx blits. A RUN of bags
   sampling one clamped target therefore costs one barrier instead of two per
   bag. Measured on a physical PS2 at a frozen night vantage on the Motor
   District: `Light_pools` **1.605 -> 1.342 ms**, six samples per arm with no
   overlap between the ranges (1.590-1.628 against 1.306-1.375); whole render
   20.900 -> 20.819, inside that row's own noise. Day and night captures are
   visually clean. **The entry's own number was a PCSX2 one and did not
   travel** - which is the standing rule about that emulator, restated by a
   change that was implemented exactly as the entry described it.
4. **The shared reflection probe is the whole of renderScene's head**, 0.820 ms
   on a 2-frame cadence, i.e. ~1.64 on the frames it runs. Everything else in
   that head — split band, sky retint, env basis, camera feed — is 0.001.
5. **A console repeat of all of it.** PCSX2 models no EE data cache, so the
   shares travel and the milliseconds do not; the hardware gap is 25% of
   submission where the emulator's is 38%, and the difference is expected to sit
   in the pipeline brackets rather than in the terms named above.
6. ~~**Two release-build log lines that should not exist**~~ **DONE
   2026-09-22, in two halves, and the entry was half stale when it was read.**
   The TIMED lines were already gated by the time this was picked up:
   `STAPIPRET` behind `TYRA_STAPIP_RETAINED_REPORT` and the 120-frame
   `VRAMSTAT` summary behind `TYRA_VRAM_PERIODIC_STAT`, both default 0. What
   is left of `VRAMSTAT` in a release build is the EVENT line, which prints
   only on a frame that actually evicted - the district reports zero
   evictions in all four poses - and an eviction is a real condition worth a
   line, so it stays.
   The half that was genuinely missing is the detection, and that is now
   built: `--audit-release` scans `.rodata` for the tags the opt-in
   measurement macros own (`FTCLIP`, `FTPKT`, `STAPIPRET`, `STAPIPMISS`,
   `STAPIPBAKE`, `VRAMRES`, `VRAMEVICT`, `ROADINDEXVERIFY`, `WHEELBAKE`) and
   reports them as `measurement build - .rodata` (docs/devkit.md). Falsified
   both ways on examples/vehicle-playground: the ordinary build reports five
   devkit strings and no measurement finding, `TYRA_WHEEL_REBUILD_REPORT=1`
   adds `WHEELBAKE` and `TYRA_FRAME_PROFILE=1` adds `FTCLIP` and `FTPKT`.
   **Add the tag when you add a gate** - a macro whose log line is not in that
   list still ships silently.

### ~~The baked stream needs a caller contract for NON-POSITION data~~ DONE

**DONE, 2026-09-17 - and it is a TYPE rather than a rule**
([bag-content-version.md](bag-content-version.md)). The trade this entry
priced - "1.29 ms for roughly 110 obligations that nothing enforces at compile
time" - was refused on its own terms and then dissolved, because the census
underneath it was wrong in the direction that mattered. The exact count is **280
write sites in 42 generated functions behind 108 array declarations**, and
**every one is generator-emitted**: fixed template text in `src/templates.cpp`,
zero in other editor sources, zero in checked-in example game sources, and zero
reachable from user-authored code. A closed population in one file is a type
problem. The arrays are now `BagArray<T>` - `data()` is const, every mutation
stamps `contentVersion`, and a raw write DOES NOT COMPILE, with a negative test
(`tools/bag-array-enforcement.sh`) that was falsified before it was believed.
The 280 write sites did not change.

**Acceptance met**: `--keep-routes` with the traffic MOVING, **169 843 blocks
checked, `failed=0`** over ~12 600 frames; against the control, every count that
describes what is drawn identical to the digit and the captures byte-identical,
with packet flushes -300/50 frames and `chainQw` -26.0%.
`TYRA_STAPIP_BAKED_STREAM` defaults to 1. The missing hardware price for the
content-version contract was supplied on 2026-09-20: on a physical PAL PS2,
the 83-chunk road-only district measured 18.776 -> 17.012 ms median total and
8.856 -> 7.769 ms procedural across three synchronized captures per arm, while
EE memory rose 14.3 -> 15.7 MB and 187,904 non-HUD pixels stayed identical.

The original diagnosis is kept below because the shape of the defect is the
argument for the shape of the fix.

**This WAS the blocker that stopped TYRA_STAPIP_BAKED_STREAM shipping above 0**, and
it was found by the adversarial verify mode rather than by any gate leg -
docs/baked-stream-acceptance-gate.md, and
examples/vehicle-playground/authoring/ee-rearchitecture-2026-09-16.

`TYRA_STAPIP_BAKED_VERIFY` rebuilds every block with the ordinary writers and
compares it against what the cache holds. It mismatches **1438 times** on the
Motor District, and all 1438 are the same shape: the colour-only program class,
identical block length, and the first differing quadword landing on the FIRST
COLOUR QUADWORD. The positions match; the per-vertex colours do not.

**Why.** The key holds the colour array's POINTER and `bboxVersion`, and
`bboxVersion` is a statement about the bounding box - `StapipBagBBoxesCacher` is
its only other consumer. A caller that re-shades per-vertex colours IN PLACE,
which this district does for its dynamic lights, changes what the block must
contain without touching anything the key can see.

**The retained cache is immune and that is the whole trade.** It stores the chain
- tags and `REF`s that still name the bag's own arrays - so a re-shaded colour
array is followed at DMA time and is always fresh. The exposure belongs to the
baked stream BECAUSE it inlines the payload.

**Why no gate can catch it here.** Once the fixture's camera freezes, the colours
freeze too, so the picture and both hashes agree. It fires during the warm-up
sweep and stops - 1438 on the parked fixture, 1451 on the moving one. This is
the class benchmark-district.py's docstring warns about.

**The fix is a contract, and it is on the generated game's side of the
boundary**: either bump a version whenever ANY of a bag's arrays is rewritten
(not just its positions), or add a second version field for contents that are not
positions, so `bboxVersion` keeps meaning what the bbox cacher needs it to mean.
Whoever takes it re-runs the verify arm; `failed=0` is the acceptance.

**AND HERE IS THE DECISION, PRICED ON BOTH SIDES, so it is not taken on the
prize alone.** The architecture is now measured on the physical PS2: garage-day
`work` **−1.287 ms** against a 0.012 ms repeatability floor, `total_ms`
unchanged in garage day (the saving is absorbed by `present`, because the vsync
rung is 10.42 ms away), and garage night's judder removed. The contract it needs
costs:

| | |
| --- | ---: |
| sites that bump `bboxVersion` today | **26** |
| sites that write into a bag-backing array (conservative grep of `templates.cpp`) | **~110** |

So the proposition is **1.29 ms for roughly 110 obligations that nothing
enforces at compile time**, whose failure mode is stale lighting visible only
while the camera moves — the hardest class to catch, and one the benchmark
fixture is structurally blind to. That is a trade to accept or refuse, not a bug
to route around. See `docs/ee-submission-rearchitecture.md`, order of work
item 3.

### ~~Name the bag that is rewritten every frame on a frozen scene~~ NAMED

**NAMED, 2026-09-17, and it was not in the family this entry suspected.** The
26 `bag->bboxVersion = ++g_bboxStamp` sites in the lamp/beam/flashlight family
were the theory; the answer is the **vehicle PAINT PASS**
(docs/vehicles.md, "A shiny body"), which recomputes a per-vertex fresnel rim
and a Blinn-Phong specular from the camera EVERY FRAME for ~1 100 vertices of
each visible car. It never bumped anything - it wrote the colours by
`const_cast`-ing the bag's own `many` pointer, which bypassed the owning array
altogether, so no instrument keyed on `bboxVersion` could ever have seen it.

**It was found by the adversarial verify arm, not by the bisection procedure
below**, and the shape of the evidence is worth copying: the arm named the
program class (`Cull - TCE`), the vertex count (2 280 in 31 packages) and the
first differing quadword, and enriching its report with the four stream
pointers and the differing quadword's floats turned it into three equal RGB
lanes drifting by 0.012 with an alpha drifting by 0.002 - which is what a
camera-dependent shade looks like and nothing else does.

**The instrument that would have named it now exists**: `STAPIPMISS` splits
`bbox=` from `content=` (docs/bag-content-version.md), so "a mesh moved" and "a
mesh was re-shaded" no longer read as the same counter. At the garage-day pose
it reads `bbox=300 content=600` over 300 frames, and the 600 are the two cars.

**What is still open** is the `prepare` +0.315 ms hypothesis this entry fed:
that the rise is data-cache pressure from rebakes, and that fixing the caller
would recover most of it. That is now testable rather than theoretical - the
caller is named and the counter separates it - and it wants a hardware round,
not an emulator one.

The original procedure is kept below; it remains the right shape for the next
caller of this class.

### The original entry: name the bag that is rewritten every frame on a frozen scene

Two instruments now point at one submitter in the generated game, and neither
can name it because the fix is a change to a CALLER's contract.

**The symptom, measured** (docs/baked-stream-acceptance-gate.md, and
examples/vehicle-playground/authoring/ee-rearchitecture-2026-09-16): at the
Motor District garage-day pose, held, one ELF, two boots, the picture
byte-identical and the DMA chain identical to the quadword - the VIFcodes and
the absolute-address uniforms hash the same, the geometry that comes out of the
qbuffer COPY POOLS hashes the same, and the geometry that comes out of the
BAGS' OWN ARRAYS is different on every frame. `STAPIPMISS` reads `bbox=1` a
frame at that pose and names the bag as **96 vertices in 2 packages**. The
outer-road pose, which reads `bbox=0`, is correspondingly cleaner.

**The suspect.** The generated game has 26 unconditional
`bag->bboxVersion = ++g_bboxStamp` sites in `src/templates.cpp`, most of them in
the lamp, beam and flashlight family, and several of those rebuild their vertex
or colour arrays from wall-clock-driven fade terms (`angleFade`,
`DynLightRt::lastLevel`). A term driven by real time is not frame-deterministic
under an emulator, which is exactly why the same frame number gives different
bytes on a second boot.

**The experiment**, so whoever takes this starts with a procedure rather than a
theory: build with `TYRA_STAPIP_VIFHASH` and `TYRA_STAPIP_BAKED_REPORT` at 1,
hold garage day, and bisect the 26 bump sites by making each one conditional on
the array actually having changed - docs/wheel-rebake-skip.md is the worked
example of that fix for a different caller. The bag is named the moment
`STAPIPMISS bbox` reaches 0 and the bag-sourced geometry hash starts repeating
across two boots. Both are one grep of the log.

**Why it is worth doing.** Three things at once: the acceptance gate gets its
third leg back, so a future change touching vertex data need not rest its whole
argument on pixels; one bag a frame stops invalidating the bake cache and the
bbox cacher; and a scene that claims to be frozen actually is, which every
future A/B on this fixture depends on.

### Wire the SAMPLED verify to the project devkit profile

`TYRA_STAPIP_BAKED_SAMPLE_VERIFY` (docs/bag-content-version.md) is the cheap
half of the adversarial arm: it verifies ONE baked block per frame, round-robin,
and replays everything else - about 1/360 of the exhaustive arm on the
garage-day frame - so a missing content invalidation surfaces within seconds of
play instead of only in a dedicated ELF. It is built, it prints the same
`STAPIPVERIFY checked= failed=` line, and it is **default 0 behind an engine
header flip** rather than following a project's devkit profile.

**Why it is not wired, which is the part worth knowing before trying.**
`libtyra.a` is archived once per CHECKOUT from engine sources with no
per-project flags (`tools/toolchain/native-build.sh` rebuilds it only when
engine sources change), so an engine macro cannot follow a project setting
without giving the engine build its own stamp file. Doing that carelessly
recreates the trap the toolchain-image round already paid for: a flag swap that
touches no source rebuilds NOTHING, the previous objects are relinked, and three
consecutive probes measure the same ELF (tyra-testing, "An image swap used to
rebuild NOTHING"). The `.vcl-stamp` mechanism is the worked precedent - hash the
resolved flag set into a stamp file the engine build depends on.

**The check that it works** is the same one that found the paint pass: turn it
on, drive the district with `--keep-routes`, and confirm `failed=0` with a
`checked` around one per frame rather than ~400.

### The baked VIF stream: format proven, memory priced, the prize still unbuilt

[baked-vif-stream.md](baked-vif-stream.md) spiked the central change of
[ee-submission-rearchitecture.md](ee-submission-rearchitecture.md) � a wholly
visible mesh's whole per-frame VIF1 command stream emitted once and replayed by
one DMA `REF` tag � behind `TYRA_STAPIP_BAKED_STREAM`, **default 0**. The format
works and the block layout is written down; what is left is everything the spike
deliberately did not touch.

1. **Decide whether the memory is affordable at all.** Inlining the payload
   stores every static vertex twice � ~49 bytes per vertex for the textured
   per-vertex-colour class, three to four megabytes for the Motor District � and
   nothing can free the originals (the bbox cacher, the clip route and the
   generated game all read them). That is the number to argue about before any
   more of this is built.
2. **The prize is not in the spike.** The spike keeps the per-package
   classification and the 16-group qbuffer flush cadence, because `packetFlushes`
   is one of the counters the acceptance gate pins. What the plan predicts �
   ~0.4 ms against 20.44 � needs those removed too, and removing them changes
   what the gate can compare. **The replacement gate is now designed** �
   [baked-stream-acceptance-gate.md](baked-stream-acceptance-gate.md): a
   canonical hash of the word stream VIF1 actually receives, with texture
   mutations interleaved, plus byte-identical pixels over a pose sweep. It
   constrains the GS's input without constraining the chain that built it, so
   the flush cadence is free to move. Its one hole is DMA lifetime on a frozen
   fixture, which PCSX2 cannot see at all.
3. **Then the console.** Nothing here is a millisecond on either machine, by
   construction.
4. **The editor-side bake** (four named requirements at the end of
   baked-vif-stream.md), of which the awkward one is that the district's models
   are shade-baked per placed object, so two instances of one model share no
   vertex array and would share no baked stream either.

### GS VRAM in the Motor District garage — ATTRIBUTED, and the reading was stale

The garage poses were reported at 12.17 texture re-uploads per frame (day) and
5.75 (night) with `freeMB=0.048`, `largestKB=33` and four evictions per frame.
**Two findings, and the first retires the question.**

**The re-upload figure was a stale fixture.** `benchmark-district.py` copies the
example's *committed* generated sources, and those drift; every fixture since
regenerated with the editor under test records **0.000 re-uploads and 0
evictions in all four poses**, on the console. The stale build also showed 3.6 ms
more submission than the regenerated one. A performance fixture built from
committed generated sources is measuring a different game — now written down in
[gs-vram.md](gs-vram.md), [vu1-and-dma-cache-cost.md](vu1-and-dma-cache-cost.md)
and the script's own docstring.

**The cliff underneath it is real.** The texture heap is **196 608 words
(0.75 MB)** — `Pal576i` at 32-bit colour spends three quarters of the 4 MB on
the frame and z buffers before a texture is loaded — and the garage holds
**165 440 words of it in 27 allocations**, 84% full with a 121 KB largest free
block. Of that, **83 392 words are three vehicle textures** and 15 232 are the
entire city. Nothing is allocated per frame (28 uploads, zero re-uploads over
4 440 frames) and nothing is evicted parked, so neither the AO atlas nor the
eviction policy was ever involved. But opening the pause menu binds 40 960 words
against 31 168 free and the reading drops to `freeMB=0.0483` / `largestKB=25`
with eight evictions — the `0.048` the console reported, reached by one button
press.

Levers, all of them **authoring** decisions rather than engine ones, and none of
them urgent while the scene is not actually thrashing:

- **Vehicle body textures bypass the project's `textureQuant` entirely**
  (`vehbake` writes into a directory texbake's sweep skips), so a 4-bit project
  ships a 32-bit car — one 256×256 RGBA32 image holding a third of the heap.
  A real bug, and **a VRAM-for-GS-time trade rather than a free win**: measured
  on hardware it buys 280 KB of heap and costs ~0.5-0.7 ms of work per pose on
  a scene with no thrash to relieve. See the dedicated commit and
  [vehicles.md](vehicles.md).
- **`menus/` follows its stylesheet's `quant`, which defaults to none.** The
  district's pause menu is 40 960 words at 32-bit against 5 248 at 4-bit, and it
  is what walks the parked scene off the cliff. One line of a stylesheet — but
  price it against the same GS-time trade before taking it.
- **`res/hud/` is never palettized** (57 344 words resident in the garage; a
  save adds another 65 536). Deliberate — palettes are worst at smooth alpha.
- **`palFullHeight` costs 98 304 words (384 KB) of texture heap** for 64 scan
  lines, and costs no GS sampling time at all. On this evidence it is the
  cheapest 384 KB available. Worth a deliberate decision rather than a default.

The `1ce38d2b` baseline and integrated `af8e6762` were measured in PAL
software-renderer frozen parked views with no competing builds or benchmark
emulators. Garage day/night moved 25.00 / 20.37 FPS to 25.00 / 25.00; outer
day/night remained 50.00 / 50.00. Quiet-debug and release repeated the
integrated 25 / 25 / 50 / 50 samples. These are ordinary-FPS measurements, not
serialized profile times and not a 60 FPS, hardware, Linux, traffic-drive or
complete reflection-state claim.

The shared `@sky` target was already every-second-frame before this pass. Its
accepted change is capture-basis and scene-reload correctness plus a dedicated
serialized `Reflections_shared_probe` row, not a claimed cadence saving. Keep
the conservative cadence until a content-reuse proposal has a bounded visual
error test across day/night, teleports, reflected-object movement and alternate
views.

The district itself remains at 93,150 road vertices in 90 chunks. Generic
planar-slope fixtures improved, and both environment LOD trials (model 64; model 64 plus terrain 96)
were rejected for this map: neither improved the four-view median FPS. Authored
distances remain zero; discarded variants have no full driving acceptance.

**All three of those were re-measured on a physical PS2, 2026-09-16, against
frame `work` rather than displayed FPS** ([evidence](../examples/vehicle-playground/authoring/road-lod-2026-09-16/README.md)).
What is now settled, and what is left:

- ~~**The road's lateral reduction.**~~ **DONE in 1.105** — it was all-or-nothing
  and therefore never fired on a curved street over a heightfield. Merging
  maximal coplanar runs: 31 050 road triangles in 470 packages -> 21 252 in 337,
  surface and seams exactly unchanged, −0.358 / −0.591 ms on the console.
  ([roads.md](roads.md), "The lateral budget".)
- **Road longitudinal spacing is implemented and host-bounded in 1.119.** Five
  gentle Motor District roads use 2 m while the looping ring and eastern crest
  stay at 1 m: 21 286 -> 18 750 triangles and 338 -> 297 packages, with 0.0351
  maximum height error and sub-texel UV error. A physical-PS2, four-pose
  `quiet-debug` A/B retained the same 25 / 16.67 / 50 / 25 median FPS; it is a
  safe geometry reduction, not a demonstrated vsync-rung win. A driven
  crest/bend inspection remains.
- ~~**Mesh LOD distance.**~~ **REFUTED with a mechanism**: 64 removes 592
  triangles, takes 0.32 ms out of `dispatch` and 0.31 out of the VU1 wait, and
  makes the frame **0.19 ms slower**. Do not retry it at this object count
  without attacking the per-object tier selection first.
- **Terrain LOD distance 160 is a real −0.38 / −0.45 ms and owes exactly one
  check.** Both quality risks are bounded in world units (the coarse ground
  rises at most 0.0175 above a road lifted 0.12; the band transition subtends
  0.57 pixels). What a parked fixture cannot see is the tile rebuild while the
  player moves, so one drive across the 160-unit band in both directions is the
  whole remaining gate. ([terrain-lod.md](terrain-lod.md).)
- **The shared reflection probe is the biggest item left on the garage frame**,
  and it is no longer an estimate: a bounding probe halving its cadence buys
  1.033 / 1.278 ms, so the whole probe costs **2.07 / 2.56 ms**. That is more
  than the road reduction and the terrain LOD together. Item 4 above priced it
  at 0.820 ms in PCSX2; the console says 2.5x that. The three options — fewer
  objects, a coarser LOD for the probe pass, a longer cadence — now deserve a
  design rather than a place behind the triangle budget.
  **DESIGNED AND SHIPPED as a reuse budget, 1.106.0**
  ([reflective-materials.md](reflective-materials.md), "The reuse budget"): the
  probe skips its cadence beat while nothing that feeds the capture has moved,
  with the staleness bounded in pixels of its own 128-pixel target rather than
  in frames. Of the other two options, "fewer objects" is worth ~0 triangles in
  the pose that is slow (four near buildings are everything the probe draws
  there), and "a coarser LOD for the probe pass" needs the models re-baked with
  tiers AND a second resident bag set per reflected part, because swapping the
  live bag's tier twice a frame bumps `bboxVersion` and throws away the bbox
  and retained-command caches. **What is left open is the hardware
  millisecond.** The Motor District example now takes the already-shipped
  reflection-only box-proxy path for all seven reflected building models and a
  four-pixel reuse budget. Generated workload for a garage-night refresh falls
  from **10 704 to 375 triangles**, **196 to 47 packages** and **48 to 20 bags**
  without changing main-view geometry, collision or picking. Native PS2 build
  and PCSX2 boot/capture pass, but the physical-console A/B remains open because
  that test boot lost the resident IOP before entering the scene. Reboot the
  console, launch through `--run-ps2` (or a verified absolute `ps2link.run`
  marker), and price a forced-refresh turn plus steady driving.
- **A coarser LOD for the reflection probe is priced and not taken.** It is the
  only option that removes triangles unconditionally - up to ~70% of 10 413 a
  hit - and it needs three things this round did not build: a bake gate that
  emits `.tmdl` tiers for `reflected` objects without turning main-view mesh
  LOD on (which is refuted), a second resident bag set per reflected part, and
  the RAM for both in a 32 MB machine. Worth doing only after the reuse budget
  has been priced on hardware, because in a scene that is mostly still the two
  overlap.
- ~~**World visibility is untried and is now the largest lever on the garage.**~~
  **PROBED, 2026-09-17, and it is NOT an occlusion problem.** Each solo object
  was hidden at runtime and the frame photographed and diffed against the
  control. **The garage is genuinely visible**: twelve of the fourteen objects
  that submit triangles paint pixels, and only **3 509 triangles / 51 packages /
  6 bags** (one tower plus its pavement slab) are strictly occluded — two
  objects out of 142. A PVS or portal scheme buys that, in one pose, for an
  authoring concept, a baker and a per-object test paid by all 142 objects every
  frame; in the outer-road poses `object_submit` is only 502 triangles in total,
  so it buys almost nothing there.
  **The live number is triangles per visible pixel.** The two near towers pay
  ~0.10; four distant objects pay 2.4 to infinity, and together **6 532
  triangles — 18.6% of the frame — buy 327 pixels, 0.12% of the screen**. That
  is what impostors are for, and impostors already ship, are distance-driven so
  they survive driving, and do not multiply bags.
  ([Evidence](../examples/vehicle-playground/authoring/world-visibility-2026-09-17/README.md).)

  **THE THRESHOLD IS BUILT, 2026-09-17**, and priced in packages because the
  console has since measured one at 19.5 us (day) / 31.8 us (night): eight-view
  impostors on the two building models at **100 units** take garage day from
  **750 to 670 packages (−10.7%)** and garage night from 803 to 723, with the
  strictly-occluded tower falling 50 packages → 1. The outer poses do not move
  at all, correctly — the same buildings are 32 and 44 units away there.
  Collisions and picking are proved unchanged in the generated scene data
  rather than by capture.
  ([Evidence](../examples/vehicle-playground/authoring/impostor-threshold-2026-09-17/README.md).)
  **What it still owes**: a HARDWARE arm. Every figure is a PCSX2 count or
  pixel, and the milliseconds quoted are conversions through somebody else's
  measured rate — whose own caveat is that the bracket split does not support a
  pure per-package model.
  **Headroom left on the table**: the two distant streetlights and the far park
  tree are another ~9 packages the same rule would take at the same threshold;
  they were left out because thin geometry on a card is a different quality
  risk from a building and deserves its own look.
- **Nothing has ever attacked the frame's worst PACKING.** Projected shadows
  and the wheels are triangle LISTS — 25 triangles per VU1 package against a
  strip's 73 — so together they take 130 of garage day's 803.5 packages for
  3 050 of its 40 347 triangles: 16% of the packages for 7.5% of the triangles,
  on a page whose whole thesis is that the EE pays per package. Stripping
  either would be the cheapest package reduction on the list.
  ([The inventory](../examples/vehicle-playground/authoring/reflection-probe-2026-09-16/README.md).)


The retired `PROGRESS.md` is still available when old implementation history is
actually needed:

```bash
git log --diff-filter=D -- PROGRESS.md
git show <retirement-commit>^:PROGRESS.md
git log -p --follow -- PROGRESS.md
```

### Palettized shadow-atlas tiles

A baked shadow tile is one alpha gradient over a single colour, and it ships as
**RGBA32** because that is what the lightmaps ship as — 256 KB a page, 23 % of
the 32-bit texture heap ([shadows.md](shadows.md), [gs-vram.md](gs-vram.md)).
That is the one number that decides how many baked shadows a project can have,
and there is a strong reason to think it can be a quarter of itself: the
engine's PNG loader **does** carry per-entry alpha on the palettized path
(`png_loader.cpp`, `clut[i].a = trans[i] >> 1`), so a PSMT8 tile with a black
palette and a 128-step alpha ramp would be one byte a texel plus a 1 KB CLUT —
roughly 60 shadows a page instead of 16.

What stops it being obvious is the note in `texbake.cpp` beside the lightmap
writer: a quantized bake of the same shape "rendered as nothing" in PCSX2. That
was **pngquant's** output, not a hand-built CLUT, so the two are not the same
experiment. Before promising anything: build the PLTE+tRNS by hand (256 entries,
RGB fixed at the tint, alpha 0..255), put it on a console or PCSX2 and look —
and be ready for the answer that the alpha test plus a CLUT lookup is the part
that breaks, which would settle it for the lightmaps too.

### A baked shadow from a placed light

The direction is the scene's sun at the baked hour. A street lamp throwing a
crisp static shadow on a textured wall is the obvious next want, and the
machinery is already per-caster — but each extra source is its own atlas tile
and its own projected mesh, so the VRAM and ELF budgets scale with the number
of lights, not with the number of casters. Worth doing after the tile size
question above, not before.
### Re-verify the whole VU1 set on real hardware, now that the race is fixed

The console-only corruption that was filed here as "openvcl's `stapip_clip_c`
renders wrong on a real PS2" turned out to be an engine DMA race - the StaPip
slot pool handed to the next bag while the previous packet still read it -
fixed in 1.81.1 by double-buffering the pool (docs/vu1-clipping.md, "Real
hardware: the slot-pool race"). Both assemblers were failing; openvcl's
schedule only moved the rate. The EE-wait probe that located it rendered the
production openvcl set pixel-identically to Sony's for 24 of 24 frames.

Measured with the fix on the console: `vu1` 30 of 30 frames at 0 pixels
against Sony's reference, `precise` 24 of 24 identical. What is still owed to
hardware is the broad pass this entry originally asked for: every microprogram of the resident set, on more than one map and more
than one pose, with the failure-RATE fixture rather than single frames (parked
pose + `--capture-frame` x30 against one reference). The `as_is_*` family and
the EE clipper deserve the same pass - the EE clipper was catastrophically
broken by the same race and has never been looked at on hardware since.

### Keep Sony's vcl building the engine without a human remembering to

The Docker fallback was broken for five days (docs/toolchain-image.md, "Sony's vcl
is a build target too") because the default build never runs Sony's `vcl`. Two
pieces would catch the next one: make `vugen::vfPressure` extend a live range
across a backward branch (today a preamble constant last read in the per-buffer
header counts as dead inside the batch loop, so the TC clip image read 30 while
Sony refused it - recalibrate the table in docs/vu-authoring.md afterwards), and a
scripted both-assemblers pass over the engine's `.vclpp` set that a VU change runs
before it is called done.

### Judge openvcl against the ps2gl fixtures

Twelve of upstream's own `test/fixtures` are real third-party VU code and no
oracle has read them yet. The immediates are handled now (operators are tokens,
division truncates toward zero as both assemblers do — measured); what remains is
nested-`MUL` modelling in `pb-dag.py`. At the first point the oracle disputes, the
compiler was verified correct by hand, so this is about the instrument's reach and
not a suspected defect. Positive control as always: the same pass over Sony's
output for the same sources.

### Send the openvcl defect reports upstream

`docs/upstream-openvcl.md` carries the defects this work found, each with a
mechanism and a reproducer, several firing on the stock commit with no flags.
Nothing has been submitted and no pull request is open.

### Regenerate the 70-program assembler snapshot

The corpus used as a stability anchor predates the clip-path rewrite, so it is
valid for "identical inputs must give the same md5" and wrong for absolute sizes.
Regenerate it from a real engine build, in its own commit, and re-anchor the md5.

### Align the ABI metadata of embedded IOP IRX blobs

The native PS2DEV linker emits one `linking abicalls files with non-abicalls
files` warning for every IRX embedded in `libtyra.a` (`audsrv`, `padman`,
`fileXio`, USB and related modules). They are IOP binaries converted to EE data
objects by `bin2s`, not game code, so builds and PCSX2 launches are currently
unaffected. Make the generated assembly object's ABI mode explicitly match the
EE build rather than suppressing the linker warning. Verify a clean link and a
native boot that exercises audio, pad input, host filesystem and USB input.

### Static batching is a net LOSS on the Motor District, by every count (2026-09-23)

Measured in PCSX2 on `examples/vehicle-playground` with one knob - the
project's `staticBatching` flag - at a parked street vantage, day and night,
counts identical across every 50-frame window in both scenes:

| per 50 frames | batching ON | OFF | delta |
|---|---:|---:|---|
| cull triangles | 1 066 450 | 1 036 750 | **-29 700** |
| submitted vertices | 24 090 | 23 496 | **-594** |
| packet flushes | 2 950 | 2 700 | **-250 (-8.5%)** |
| packages rejected | 28 850 | 27 350 | -1 500 |

The night scene gives the SAME deltas to the unit (-29 700 / -594 / -250).
That is the shape the skill calls a verdict: worse on every axis at once.

It also draws MORE than the unbatched reference. 1 066 pixels differ below the
HUD, 980 of them brighter in the batched arm, all inside a 47-row band at the
horizon - distant props the unbatched scene culls. The mechanism is inherent:
a batch's draw-distance test runs once against the MERGED box, so a batch with
members spread up to 78 units drags a far member into view behind a near one.
Draw distance is already part of the group key, so this is not a key bug.

**What is NOT measured, and why this is not a decision yet.** Batching's whole
benefit is fewer BAGS - 58 members into 42 batches here - and FTCLIP counts no
bags. Per-bag EE overhead is exactly the term PCSX2 cannot price. On hardware
`Static_batches` was 2.02 ms against `Objects` 7.41 at the same vantage, and
turning batching off moves those objects into the second row. So the console
has to settle it: build both arms, read `Static_batches` + `Objects` + `Total`
from `--profile-frame`, and remember the emulator says the GS and VU1 side is
already losing.

If it turns out to be a loss there too, the fix is not necessarily "turn it
off": `--batch-report` shows 21 of the 42 batches (49 of the 58 members) save
ZERO VU1 packages, so refusing a batch that saves no packages would keep the
wins and drop the losses. That refusal is untested.
## Visual showcase directions

See [Rendering directions](rendering-directions.md) for the assessed roadmap:
offline foliage impostors, local vegetation interaction, better probe lighting
and source assets first; crowds and texture paging only with measured budgets.
The eight-view cylindrical impostor implementation is documented in
[Distant foliage impostors](impostors.md). Follow-up candidates are transition blending, elevated
captures, screen-size thresholds, per-view rendering and grouped distant draws.

## Small

### Make headless render-cost capture accept the report it requested

During the 1.87 static-model batching A/B, `--profile-frame` caused the running
PCSX2 game to write a complete sequence/footer-matched `rendercost.txt`, but the
client still reported a sequence mismatch and waited the full 45 seconds. The
UI capture path remains usable. Reproduce the headless sequence comparison and
fix it separately from renderer work so profiler changes cannot contaminate a
performance A/B.

### An input replay cannot reproduce a memory-card save

The input recorder (`docs/input-replay.md`) reproduces a run by performing the
same input against the same build. Everything the game's own behaviour depends
on is recorded - both pads, the keyboard and mouse, `dt`, the procedural seeds -
with one hole: the **PCSX2 memory card persists between runs**, so a game that
reads a save starts from wherever the last session left it, and the replay
quietly describes a different world from frame one.

`--clear-saves` covers the host-side fallback (`bin/save<N>.sav`,
`bin/profile.sav`) and is enough for most projects, because those are what a
`host:` boot writes. The card is not cheap to reach: the editor would have to
know which card image the emulator is configured to use, and either swap it or
write a blank one, per launch. Two options worth measuring before choosing:

- point PCSX2 at a per-project card in the launch arguments and delete it on a
  `--clear-saves` run - simple, but it changes what the emulator does for every
  run of that project, not just a recorded one;
- record the save FILE into the recording as an opening block. That makes a
  recording self-contained and would also fix "the recording works on my
  machine", at the price of the format no longer being input-only.

### The recorder's fingerprint stops at the player

The per-frame divergence check records the player's position and the yaw/pitch
of the view - twenty bytes, and enough to catch every divergence seen so far,
because almost everything that can go different eventually moves the player. It
will not catch a run that goes wrong somewhere the player never reaches: an NPC
taking a different path, a flow variable landing on a different value, a spawned
object appearing in the wrong place.

The cheap extension is a rolling hash over a handful of `RuntimeObject`
transforms rather than a second fingerprint kind - the time machine's capture
walk (`liveTimeSource`) already knows how to enumerate exactly that state, so
the two could share the walk. It was left out because it would have to be
bounded (a 1000-object scene cannot hash every object every frame) and picking
that bound is a measurement, not a guess.

### The guard band, on the other two routes

`docs/vu1-clipping.md` moved screen-edge packages off the clipper and onto the
cull path; two smaller cases were left alone deliberately and are worth
measuring before touching:

- **The small-bag branch.** `StaPipCore::render` sends a bag with fewer than
  `maxVertCount * 2` vertices straight to `renderSubpkgs` at 1/3 package size,
  so a guard-band-only bag is batched back together by `fillByCopyMax` - a COPY
  where the full-package path hands VU1 a pointer. Routing that branch through
  `renderPkgs` instead would give it the pointer path; the reason it exists is
  to skip a double classification, so the question is which costs more.
- **A bag-level guard-band test.** The main bbox is classified before any
  package is created. A bag entirely inside the guard band could skip package
  classification altogether, at the price of no longer dropping its OUTSIDE
  packages - the same trade the routing already makes one level down, but over
  a much bigger box. Measure on a scene of large objects, not on a terrain.

### Let a cutscene hide the HUD without hiding the USE prompt, and vice versa

`Sequence::hideHud` is one switch covering the HUD stack, the live bars, the
baked texts, the USE prompt AND the USE interaction (docs/cutscenes.md). That is
the right default and it is what was asked for, but they are four separable
questions and somebody will eventually want three of them — a cutscene that
keeps a subtitle bar up, or one that keeps the HUD and only refuses the
interaction. The shape is already there (`ScriptContext::hudSuppressed` is a
flag beside `hudVisible`, not a write to it), so the change is a small bitmask
plus one combo; do it when a real project asks, not before.

### Close the one-frame USE prompt at a cutscene's first frame

`updateUseTarget` runs BEFORE the scripts, so on the frame a flow graph calls
**Play Sequence** the prompt was already decided against the previous frame's
`hudSuppressed`. The render gate catches it in the same frame the director
updates; what is left is the frame where `play()` happens after the director's
own update, i.e. one frame of prompt at a cutscene's very start, under the
fade-from-black. `hidePlayer` has had the identical one-frame lag since it
existed, so the fix should cover both or neither, and it must not end up with
two sources of truth for one flag.

### Ship the baked HUD sprites of the nine repaired examples

Nine example projects (`custom-nodes`, `cutscene-demo`, `large-terrain`,
`layer-streaming`, `mirror-room`, `nav-ai`, `object-spawning`,
`physics-playground`, `script-demo`) carried a `res/.gitignore` of `*` and so
tracked no assets at all. The rule is fixed and their authored assets are in,
but their baked `res/hud/*.png` and `res/fonts/atlas-default.png` are still
untracked, because the four font-derived ones (`icons.png`, `use-text.png`,
`pick-text.png`, the atlas) bake from a **system** font and a Windows box
produces Consolas where the committed copies in the other examples are DejaVu —
committing them here would disagree with the `font_data.gen.hpp` metrics beside
them. Add them from a Linux `--refresh-gen`, where the bake matches the rest of
the tree, and confirm with `git status` on a fresh clone that a built example
leaves nothing untracked.

### Ignore baked models in nested asset folders

The generated `res/.gitignore` covers `/models/*.tmdl`, `*.tskl` and
`*.tanm`, but not models below `res/models/<folder>/`. Use recursive rules in
both new-project generation and `--refresh-gen`, then verify a nested imported
model leaves no derived files in `git status`.

### Search the log panels

Add a text filter plus next/previous-error actions to Output and Debug. Reuse the
existing parsed entries and severity filters; continuation lines must stay with
their parent entry. See [log panels](log-panels.md).

### Select tabs explicitly in UI scripts

Add a `tab <window>/<name>` step. It should select the tab, wait until its body
is submitted, and fail clearly when either the window or tab does not exist.
See [UI scripting](ui-scripting.md).

### Add aliases to documentation search

Give the AI Assistant's exact-substring docs search a small hand-written alias
table, for example `lag -> frame time` and `collision box -> collision mode`.
Keep the mapping visible and deterministic rather than introducing fuzzy search.
See [AI chat](ai-chat.md).

### Report triple-buffer fallback after a display-mode change

A runtime display-mode switch may correctly fall back from three buffers to two
when VRAM is tight. Expose that result so an options menu can tell the player
what actually happened. Keep the answer tied to the engine allocation result,
not a second host-side guess. See [frame pacing](frame-pacing.md).

### Stop polling interval-zero sound emitters every frame

An emitter with `interval = 0` currently retries `audsrv` every frame. Schedule
the next play from the sample length instead: one call per loop, still seamless,
including after a dropped frame. See [sound](sound.md).

### Audit the remaining audsrv RPC calls for blocking work

Review the fork's RPC handlers and their callers for waits that can stall the
single audsrv path. Record the cost beside each caller and replace blocking
polls where the API allows it. The music-streaming stall is the known control.

### Let the Menu Editor's fit check know the real heap

`menulayout.cpp` measures a menu against a hardcoded 282 000-word texture heap,
which is only the 32-bit, both-targets-reserved case — a 16-bit project has
nearly twice that and still gets warned as if it did not. Take the number from
the project's settings the way the engine does. See [GS VRAM](gs-vram.md).

### Report ps2sdk's `GS_SET_DIMX` bitmask upstream

Each DIMX entry is a **3-bit signed** value (-4..+3) and ps2sdk masks all
sixteen with `0x00000003`, so the negative half of any dither matrix collapses
onto 0..3, the offsets stop cancelling and the dither biases the image upward.
The patch is `0x00000003` -> `0x00000007` in `common/include/gs_gp.h`, and the
regression risk is nil: a caller passing 0..3 gets bit-identical output. Still
present on master with no open issue when checked (2026-08-12); PCSX2's own
`s32 DM00 : 3` is the evidence for the width. `tyraxDitherMatrix()` in
`renderer_core_gs.cpp` packs the register by hand until it lands, and says so.

### Bound terrain UVs on very large maps

Terrain UVs grow with world position and can outrun the GS fixed-point range.
Fold each chunk by whole texture repeats during generation, preserving the
picture under REPEAT while bounding coordinates by chunk size.

### Ship one example with sculpted terrain

DONE — `examples/ambient-occlusion`. Left here only for the fact that produced
it: every heightmap in `examples/` used to be flat, relief 0.00, all of them,
which is how a bare 30° slope came to darken itself by 16% for several releases
with nobody seeing it. Keep at least one example sculpted.

### Let imported models receive the scene occlusion

The occlusion model is now good enough for it, and that was the blocker rather
than the plumbing. Re-measured on the console with `examples/ambient-occlusion`
(real kit props): with models receiving, a crate under another crate reads 0.98
of its uncovered neighbour's brightness against 0.87 for the AO-off scene, and
nothing reads as a lump - where the old distance-based response gave 0.78 and a
visibly darker box. Model AO (per texel, in the shipped texture) answers a
model's SELF occlusion and is transform-invariant, so it can never answer for a
neighbour; this is the other half.

What it needs, and why it is its own commit: `g_aoOff` off for type 5 in BOTH
the solo and the static-batch paths, model part bags switched to Gouraud (a
per-vertex value is invisible under flat shading - the lesson from the block
work), and a re-verification pass over the examples, because it changes how
every textured prop in every project is shaded.

### Give scene occluders more than one box each

An occluder is a single oriented box or sphere per object
(`aobake::collectOccluders`), so a chair, an L-shaped wall and a doorway arch
are all one rectangle to the bake. Splitting a model's triangles into 2–4 boxes
is what would lift that ceiling. Do it after the solid-angle change above, not
before — a better response over one box may be enough for most of them.

### Model AO for animated models and for shared textures

[Model AO](ambient-occlusion.md#model-ao) covers static `.obj` assets only.
Two deliberate gaps are worth revisiting once it has been used in anger. An
animated `.glb`/`.fbx` bakes a bind-pose AO map perfectly well, but its
textures ship through `animBakedTextureRel`, so the multiply needs a second
hook and the map is a lie for anything that deforms much. And a texture shared
by several model assets is skipped outright, because two UV layouts over one
image make a single multiply wrong for both — the honest fix is a per-asset
COPY of the texture in the bake, which trades the feature's zero-VRAM property
for coverage and should only be done where the author asks for it.

### Pre-lit bake parameters are editor state, not project state

`litbake::Params` (size, rays, padding, strength, floor, seed) lives on the App
and is not serialized, but it IS part of every object's staleness signature
(docs/prelit-models.md). So `--bake-prelit` uses the defaults and considers an
object baked at 256 px from the panel stale, and a second editor session starts
from the defaults too. The honest fix is per-object bake parameters stored
beside `prelitSig`, which is also what would let one hero wall be 256 while the
rest of the scene is 128 — worth doing the first time somebody mixes sizes.

### Linux packaging: the two things it did not do

Done in 1.52.0 — `installer/build-package.sh` stages the repo-shaped tree once
and emits a `.tar.gz`, a `.deb` and an `.rpm`; the tarball self-updates and the
two packages are told to use the package manager (docs/updates.md). Two pieces
were deliberately left out and are worth doing when somebody asks for them.

**No AppImage**, and not for effort reasons: an AppImage's payload is a
user-private FUSE mount under `/tmp/.mount_*`, and the game build bind-mounts
`vendor/tyra` into a container whose daemon runs as root — which cannot traverse
that mount. So the one format that looks tailor-made for this would ship an
editor that cannot build a game, unless it first copied the engine out to a real
directory, at which point the tarball is simpler and honest. Revisit only with
that copy-out step designed.

**x86_64 only.** An `aarch64` package is a runner and a second matrix row (plus
`platformAssetSuffix` learning the architecture, which is why the suffix already
carries it) — but the PS2 toolchain image would have to run under emulation on
that host, so measure a game build there before promising anything.

### Make the packagers prove they shipped what git tracks

1.55.3 fixed one tracked file going missing from every package — an exclusion
written as `*.a` took `vendor/tyra/audsrv/bin/libaudsrv.a` with the build
leftovers, and every installed editor then failed every game build
(docs/updates.md). Nothing would have caught it: both packagers describe what to
LEAVE OUT, so a new exclusion is only ever tested by somebody installing the
result and trying to build a game — which is the slowest feedback loop in this
repo and the one a developer never runs. A cheap assertion closes it: take
`git ls-files vendor/tyra tools`, drop the ignored directories, and fail the
release job if any of it is absent from the staged tree. The awkward half is
Windows, where the staged tree only exists inside the compiled Setup — either
parse ISCC's `Compressing:` lines (it lists every file it packs, which is how
the 1.55.3 fix was verified) or run the installer into a temp directory in CI
and diff that.

### Sign the Windows installer

The released `TyraX-Setup-<version>.exe` is unsigned, so Windows SmartScreen
warns on first run and the in-editor updater installs a binary whose only
provenance is the URL it came from. A code-signing certificate plus a signing
step in the release workflow fixes both; until then, the honest mitigation would
be publishing the installer's SHA-256 with the release and having
`update::download` check it (the release JSON already carries the asset's size,
but not its digest).

### Find the corona's missing 1.3x on the console

Measured while bringing the beams into the viewport: a PCSX2 frame's beam
corona adds **1.26-1.31x** what its own sprite implies, while the editor's twin
adds 0.97-1.00x of it. The instrument is beam-on minus beam-off in each
renderer, sampled straight up from the light and fitted against the bake's own
alpha curve (`t^2 (0.3 + 0.7 t)` from `menubake::bakeFlareRGBA` kind 2) times
the light colour. It is a pure AMPLITUDE factor, not a size one: fitting a free
radius instead gives rms 12.5 against 2.4, and the fitted radius scale would
have to be 1.18 while the glow demonstrably dies at the same radius on both
sides. It is also independent of everything tried - the same factor at
`lightBright` 1.3 and 0.4 (so not the `min(k, 1)` FIX clamp), in interlaced and
progressive display modes (so not field rendering), at every radius from 20 to
55 % of the sprite (so not a texel offset), and the shipped
`res/hud/flare-corona.png` is byte-for-byte the bake. The sky's authored colour
reads the same in both captures, so it is not a global capture gain either.
Candidates left: the GS texture function or the `GS_SET_ALPHA(0,2,2,1,FIX)`
path in `StaPipQBufferRenderer` doing something other than `Cs*FIX/128 + Cd`,
PCSX2's software blending of a 16-bit target, or a second draw of the same
quad. Settle it before making either side match the other - the viewport
currently reproduces the sprite exactly, which is the defensible half.

### Run the shadow A/B rig against the spot runtime

`.claude/skills/tyra-testing/scripts/make-shadow-fixture.ps1` +
`shadow-ab.ps1` exist and are proven on `flashShadowVolumes` (see the
tyra-testing skill, "The shadow A/B rig"), but the switch they were built for -
`spotShadowVolumes` - has only the data model, format, UI and codegen behind it
so far. When the runtime lands, run

```powershell
powershell -File .claude\skills\tyra-testing\scripts\shadow-ab.ps1 `
    -Editor build-dev\tyrax-editor.exe -Project $env:TEMP\tyra-editor-test\spotab `
    -Vantages $env:TEMP\tyra-editor-test\spotab\vantages.json `
    -Toggle spotShadowVolumes -Values true,false -OutDir <scratch>\spotab
```

and quote the deltas. Two things the fixture is already shaped for and nobody
has read a number off yet: the `between` vantage sees both lamp groups at once,
which is where "only the nearest spot is active" should be visible as one group
having a shadow and the other not; and setting `"shadowVolumes": 1` or `2` on
ONE lamp's `objects/<id>.json` turns the same run into a test of the per-light
override, where only the other lamp may move between the arms. A real-PS2 pass
is separate again - the rig is PCSX2 only.

### A projected shadow's reach does not know how big its caster is

1.70.1 stopped the four silhouette slots from blinking, but it deliberately did
not touch WHICH four win: it is still the four casters nearest the camera, and
DONE 1.71.1 for the ranking (view cone + distance over radius, shadows.md "Which four win"); the far cull (Preferences > Projected shadow distance since 1.71.0, dissolving over its last 30 %) is the same number for a
crate and for a building. Both are wrong in the same direction — screen area,
not distance, is what makes a shadow worth a slot — and on
`examples/night-walk` the question does not arise, because every caster there
is within a factor of 1.7 of the same bounding radius (2.50 to 4.23), which is
why the flicker was the whole of the reported defect. Two shapes were
considered and rejected without measurement, so neither is settled: ranking by
`d / r` (inverse angular size), which lets a facade 45 units off outrank a prop
at 10 while the distance fade has already dimmed it to a third; and `d - r`,
distance to the caster's surface, which is milder and principled but still
untested. A fixture with deliberately mixed caster sizes is what this needs
first — the shadow A/B rig above, run as a vantage LINE, reads it straight off.

### `physObstacle` misses two marker types `objectCollides` skips

`TerrainGame::objectCollides` is the one list of "types with no geometry in the
game", and `physObstacle` - what a falling rigid body bounces off - keeps its
own copy by number. The two have drifted: the copy is missing **18** (a
procedural volume) and **19** (a scroller belt marker), so a physics body
deflects off an invisible authoring region that nothing else in the game
collides with. Type 20 (a comment) was added to it when comments landed, which
is what made the gap visible.

The fix is almost certainly `return objectCollides(d);` - both already start
with the same `collision == 2` opt-out and ask the same question - but it is a
behaviour change for existing projects with a procedural volume or a belt in
them, so it wants a PCSX2 check with a body dropped beside one, not just a
compile. Left out of the comments change deliberately: it is somebody else's
bug and it deserves its own before/after.

### Only the player and rigid bodies have mesh collision

Per-triangle collision against an imported model exists in two places in the
generated game: the `o.data.collision == 1` branch of `collidePlayer`, and
(since 1.83.0) the static-solid pass of `updateObjectPhysics` - which had to
follow, because a body INSIDE a merged building's box was ejected through its
floor. `sweepSphere` (the camera boom, the carried object, the carry whisker
and the hand-rolled arc of a thrown non-physics pickable) still collides
against the whole-mesh box - `objectCollisionBox`. So an arch or a doorway a
player walks through freely is still a solid block to a carried object and to
the third-person camera, and there is no authoring signal that says so.

**It bit again in 1.84.2, from the other end.** A sweep that BEGINS inside
geometry returns 0, and a room modelled as one collision mesh does exactly that
to a probe standing in it - so the carry whisker read "the object does not fit"
every frame and pushed the walker back by its full `need` (0.55 + the object's
radius). Picking a weight up in the showcase's cellar slid the player 0.75 of a
unit per frame, 45 a second, with the stick centred, until it was pinned in a
corner; the owner reported it as "some unknown force moves the player", and the
`portal-ball-new.tyrarep` recording reproduces it at frames 391-407. The
whisker now takes back at most the step that was taken, which is what it always
meant (it BLOCKS a step, it does not shove), but the underlying gap - a sweep
against a box where the walker gets triangles - is still here.

This surfaced while fixing the portal doorway rule (1.81.0): the reported
symptom was "the player crosses the portal and a thrown object bounces off the
wall it is cut into". The doorway rule now opens that particular obstacle -
narrowly, only while the body's motion pierces a linked opening - but the
general case is untouched, and a mesh doorway with no portal in it still
stops everything except the player.

The physics half is done; the sweep is the remaining half. It means giving
`sweepSphere` access to `GameModel::collider` the way the physics pass now has
(a swept sphere against the grid, rather than a point-in-time resolve), which
is an EE cost per sweep per frame rather than a box test. The physics version
has not been measured on hardware either - PCSX2 only, three bodies; a scene
with many awake bodies inside a large mesh is the case to time first.

## Medium

### Rigid bodies: what the 1.123 solver leaves out (docs/physics.md, "Limits")

The hull solver shipped with corner-only contacts, no warm starting and no
editor preview. In order of what a player would notice: (1) **measure it on a
physical PS2** - the only numbers are PCSX2's, and the EE data cache changes
the per-contact cost; (2) **warm-start** the world contacts keyed by hull
corner (a per-body array of 24 accumulated impulses) so a stack of awake
bodies stops jittering before it sleeps; (3) **edge-edge contacts** for two
hulls crossing like an X (a SAT pass on the pair's edge cross products, run
only when the corner tests found nothing and the bounding spheres overlap);
(4) a **baked hull for animated models** from the bind pose instead of the
AABB box; (5) an **editor "simulate" preview** - `physhull` is host-only, the
solver is not yet, and a host twin would need the oracle treatment the static
batcher got (`verify-batch-twins.py`).

### Particle library: what 1.124 / 1.133 leave out (docs/particles.md, "Limits")

(1) **Per-particle flipbook phase** - an effect with N frames in one atlas, the
frame picked from the particle's age (the billboard program's fixed corner UVs
are the blocker: the frame offset needs a per-particle UV channel or a program
variant; the shipped flipbook swaps the whole bag's texture); (2) ~~one smoke
texture per car~~ DONE in the 1.133 merge - the vehicles branch's pool per
definition gives every car its own effect texture and blend; (3) the editor's 2D preview
approximates the preset motions - a shared host function for the per-kind
formulas would let the viewport, the window and the generated game read one
answer (the scrollsim twin arrangement); (4) a hardware check of additive
emitters' GS cost - every measurement so far is PCSX2's.

**The vehicle backfire as a library glow** (docs/vehicles.md, "Skid marks and
smoke"). The upshift flash is two untextured orange quads in the glow bag it
shares with the fallback tail lamps, and that bag is untextured on purpose -
one submit for every car's lamps. A library glow (particletex kind 3) needs a
TEXTURED bag, so the clean version is a small per-definition backfire bag
(`VehFx`, lazy, submitted only in the ~0.1 s a backfire lives, additive like
the effect) plus a `VehicleDef::backfireEffect` picker next to the tyre smoke -
one more submit per popping car, which is why it waits for a PS2 measurement
rather than landing blind. Giving the whole glow bag a glow texture instead
would round the measured-lamp quads too. An effect's extra layers on the tyre
smoke (a flame layer over the smoke, say) would be the same shape: a bag per
layer per definition.

### Pixel-exact viewport picking (an ID buffer)

Picking is a CPU ray test (`Viewport::pickAll`, viewport.cpp): `pickBounds`
boxes for primitives and markers, `pickModelSurface` triangles for static
models, a grab margin tier and the wire boxes last, then `App::viewportPick`
cycles the stack and the right-click `##pickmenu` lists it (1.82.0). What it
still gets wrong is everything the picture knows and the ray does not: a
sphere/cylinder/cone is picked by its box corners, an animated model by its
baked all-clips box, the terrain is not an occluder at all (a prop behind a
hill takes the click aimed at the hill), a texture cutout is honoured only for
static models, and the first pick is "nearest box entry", not "the pixel you
see". The fix with the right shape is an **object-id buffer**: a second colour
attachment on the scene FBO written by every scene draw from a per-draw
`uObjId` uniform (both scene programs - `useSceneProgram` re-queries uniform
locations - plus the terrain-layer particle shader and the marker/line draws;
`glColorMaski(1, ...)` off around draws that must not write, the sky dome and
the wire boxes), so the alpha test, the depth test, impostor cards and posed
skins all come for free because the id rides the very fragment that is on
screen. The click reads a small neighbourhood under the cursor - through a
tiny blit-and-readback FBO like `grabPreviewRgb`, never a `glReadPixels` of
the full target - and its centre pixel becomes the FIRST entry of the stack,
with the ray list supplying the rest for cycling and the menu (an id buffer
knows only the front-most object per pixel; the neighbourhood is the grab
margin). Terrain writes a sentinel so it occludes; comments stay screen-space
icons at the head of `viewportPick`. Mind `Ps2Output` (the scene renders at
the GS size while `camRay` takes panel coordinates - read the id at the
letterboxed, scaled position) and the AMD rule (allocate attachments with
`nullptr`, fill textures with `glUploadTexRgba`). Verify with `--ui-script`:
`click "Viewport/Viewport canvas" dx,dy` then `expect-checked "Project/<name>
 (<type>)"` or a `rightclick` + `dump` of the stack menu (docs/ui-scripting.md);
fixtures: showcase's lens spheres on pedestals and the crossing at the pool, a
terrain example with a prop behind a hill, the animated keeper. Docs:
object-selection.md, the tyra-editor-dev viewport row (both skill twins),
MINOR bump.

### DONE 1.70.0: a spot light's shadow on a WALL

Shipped: `PipelineInfoBag::dynLightSkipSlot` is the engine lever this entry
asked for, and the receiver pass is the torch's with the lamp substituted
([shadows.md](shadows.md)). The original note follows for the reasoning.

Spot-light shadow volumes ship in 1.67.0 ([shadows.md](shadows.md)) and carve
the lamp's ground pool only. The torch also draws its light on the solid
geometry in its beam - a second, additive pass over the receiver's own
triangles with the gobo's projective STQ, `wBag`/`wTexBag`/`wColorBag` and a
shared 3997-vertex budget - and that is what gives a torch shadow on a wall.
The machinery is already shared for the volumes themselves (`pickVolCasters`,
`buildVolMask` in `updateAndRenderLightPools`), so the fill is a third lambda
away.

**What blocks it is double lighting, not the fill.** The torch turns its own
cone off on each receiver first (`setFlashSpotOff` -> `PipelineInfoBag::
spotLit`); there is no equivalent for one SCENE light, and `dynLightPick =
false` removes every dynamic light from the bag. A wall drawn by both paths
reads twice as bright and its carved shadow darkens only half of it, which
looks like a bug.

The likely shape of an answer: the engine picks ONE light per bag
(`RendererCore::pickDynLight`), so an opt-out that names a light index - "this
bag skips light N, keeps the rest" - would be the exact analogue of `spotLit`
and is a small engine change. Then the wall pass is the torch's, with the
lamp's origin/aim/cone/reach substituted, inside the bracket the lamp already
opens. Verify with the `spotvol` fixture recipe in the 1.67.0 commit: a wall
3 u behind the caster, one capture with the override on and one with it off.


### ANSWERED: the guard does run under ps2link, and guards nothing

```
SIF RPC guard: seen 306,   guarded 0
SIF RPC guard: seen 14329, guarded 0     <- ~150 completions/second
SIF RPC guard: seen 29483, guarded 0
```

Fresh boot verified by the protocol below (two boot lines in the capture, first
`VRAMSTAT` at `f=120`). So the handler **is** on the dispatch path on hardware -
~29 500 completions in ~200 s - and none of them needed guarding. Every earlier
zero was therefore a real negative, not a handler that was never asked. It also
works in PCSX2 (429 completions), so both targets are covered.

Getting to that took three retracted conclusions, and the protocol that survives
is the useful residue:

- **A deploy is only fresh if the capture proves it.** Require a boot line
  (`Clut set` / `Pad initialized`) or a low first `VRAMSTAT f=`. `bin/livedbg.bin`
  appearing proves nothing: a game still running from an earlier deploy resumes
  polling the instant a file server returns and writes exactly that file, so a
  refused deploy is indistinguishable from a successful one. One capture read
  `f=136800` - a 45-minute-old ELF being measured as if it were the new one.
- **`--run-ps2` does not capture the boot output**; a manual `ps2client execee`
  redirected by bash does. Any once-only log line will be missed by the former,
  so make announcements periodic.
- **Never mirror another library's private struct.** A diagnostic that printed
  ps2sdk's dispatch slot inferred `struct cmd_data`'s layout from the
  ASSIGNMENT order in `sceSifInitCmd()` instead of the declaration - `iopbuf` is
  declared third and assigned last - so it indexed the IOP receive-buffer
  address as an EE array and faulted the game: `TLB load, BadAddr 0x00019640,
  EPC` inside `SifRpcGuard::report()`. That crash was mistaken for the bug being
  reproduced. The read is gone; the counters stay.

**The measurement below was invalid and the conclusion it produced is
withdrawn.** Recorded in full because the flaw is the reusable part.

What was claimed: the guard handled 429 completions in PCSX2 and zero across
several ps2link sessions, therefore it is inert under ps2link.

**The PCSX2 half stands** - 429 completions, so the guard works and the counter
works. The ps2link half does not, for two compounding reasons:

1. **`--run-ps2` never captured the boot output.** A manual `ps2client execee`
   capture contains `Pad initialized`, `Clut set`, `Hello from TyraX`; four
   `--run-ps2` captures contain none of them. The liveness line fires ONCE, on
   the first frame, so it sat outside every capture window.
2. **Worse: "the game came up" was never actually verified.** The check was "did
   `bin/livedbg.bin` appear", and a game **already running from an earlier
   deploy** resumes polling the moment a file server reappears and writes that
   file. So a refused deploy looks exactly like a successful one. Caught by the
   frame counter in a late capture reading **f=136800** - about 45 minutes of
   uptime, i.e. an old ELF. Several late measurements, including the
   `eeCrashHandler` on/off comparison, were made against a binary that was not
   the one just built.

**Protocol fix, mandatory for any future run on this:** a deploy counts as fresh
only if the capture contains a boot line (`Clut set` or `Pad initialized`) **or**
the first `VRAMSTAT` reports a low `f=`. Never trust `livedbg.bin` appearing.
And make any liveness announcement PERIODIC, not once, so a late-starting capture
cannot miss it.

So the question is open exactly as before: does the game's `SIF_CMD_RPC_END`
handler run under ps2link? The evidence that it CAN is unchanged and still the
strongest thing here - the original crash reported `EPC 0x00271C78`, which is
inside the game's image (games load at `0x00100000`, the low ps2link sits at
`0x00094000`), so the game's own `_request_end` was executing when it faulted.

The withdrawal also **restores** the two conclusions the invalid measurement had
knocked out: the v1-vs-v2 hang table and the `g_noPacket = 1` reading are back to
what they were - suggestive, unproven, and not contradicted.

Same ELF, two targets, one counter (`SifRpcGuard::seen()` + a one-time log line):

| target | completions the guard handled |
|---|---|
| PCSX2 | **429** in ~25 s (`SIF RPC guard: live, 429 completion(s) handled`) |
| ps2link, 3 sessions, ~20 000 `[ps2]` lines total | **0** - the line never appeared |

So the guard is functional and does not run where the crash happened. **Every
zero it reported on hardware means "never asked", not "nothing wrong".** Checked
against the obvious mistakes first: the string is in the deployed ELF, other
engine `TYRA_LOG` lines arrive in the same captures, `engine.o` references both
`install()` and `report()`, the generated game goes through `Engine::run` ->
`realLoop` -> `report()`, `install()` position (first vs last in `initAll`) makes
no difference, and `eeCrashHandler` on vs off makes no difference either - that
correlation is dead.

**Two earlier conclusions on this branch are withdrawn because of it.** If the
guard does not run under ps2link then v1 and v2 are functionally identical there,
so the guard cannot have caused the v1 hangs: the 2-of-4 against 0-of-6 table is
back to being noise, and "v1 was harmful" is not supported. (v2's shape is still
the better design on its own merits - completing the client is correct whether or
not anything is guarded.) And the single `g_noPacket = 1` read cannot have come
from a handler that does not run, so treat it as an artefact.

**The sharp open question, and it is a good one.** The original crash reported
`EPC 0x00271C78`. A game loads at `0x00100000` and the low ps2link sits at
`0x00094000`, so that EPC is in the **game's** image - the game's own
`_request_end` was executing when it faulted. So the game's handler *can* be on
the dispatch path under ps2link; it simply was not in any session measured here.
Until somebody works out what differs, a game-side guard cannot be trusted on
hardware, and the defence may belong in `tools/ps2link/tyrax.patch` instead -
i.e. in ps2link's own ps2sdk, where the other instance lives.

Next probe, cheap: register the guard for `SIF_CMD_RPC_BIND` as well and log that
separately. A bind happens at `fioInit()` before any game traffic, so it says
whether the game's dispatcher was ever consulted at all or stopped being
consulted at some point.

### Finish verifying the SIF RPC completion guard

**Measured, and it can invalidate every other number this feature has produced.**
The guard now counts *every* completion it is handed (`SifRpcGuard::seen()`) and
announces itself once in the log the first time that count is non-zero. Across
**two ps2link sessions, ~6000 and ~7000 `[ps2]` log lines each**, that line never
appeared - so the handler recorded **zero** completions, while the game's `host:`
file I/O worked throughout. The line is present in the deployed ELF (`strings`),
54 other engine `TYRA_LOG` lines arrived in the same capture, `engine.o`
references both `install()` and `report()`, the generated game really does go
through `Engine::run` -> `realLoop` -> `report()`, and moving `install()` to the
very end of `initAll` changed nothing.

If completions are dispatched through **ps2link's own** ps2sdk sifrpc instance -
it keeps one live in the same EE address space, with the stock unguarded
`_request_end` - then a zero `rejected()` count means "never asked", not
"nothing wrong", and the guard cannot protect the devkit session it was written
for. "The game works" does **not** discriminate: ps2link's `_request_end` takes
`cd` from the packet payload and would complete the game's client just as
correctly.

**The lead to pull first is a correlation, not a theory.** The one session that
ever reported a non-zero counter (`g_noPacket = 1`) had
`ProjectSettings::eeCrashHandler` **off**. Every session that measured
`seen == 0` had it **on**, and `ee_dbg_install()` hooks the EE exception vectors.
So: run the same announce build twice, with the crash handler off and on, and see
whether the liveness line appears. That is one build and two deploys.

Also worth checking directly: whether the game's `sceSifInitCmd()` actually
repoints the IOP (it should send `SIF_CMD_CHANGE_SADDR` with its own `pktbuf`
because ps2link left `SIF_SYSREG_SUBADDR` non-zero), and which of the two
`_SifCmdIntHandler`s on `DMAC_SIF0` ends up seeing a non-zero `psize`.

**Until this is settled, treat the guard as unproven in both directions** - it
has no demonstrated benefit and now no demonstrated reach either. PR #222 is a
draft again for that reason.

### Finish verifying the SIF RPC completion guard

The guard (`vendor/tyra/engine/src/debug/sifrpc_guard.cpp`) is measured to do no
harm and has **no demonstrated benefit** — the fault it defends against has
never been reproduced. Teardown A/B on hardware, one generated tree, arms
differing only in `engine.o`'s reference to `install()`:

| build | teardowns | hangs |
|---|---|---|
| no guard | 6 | 0 |
| guard v1 (returned early) | 4 | **2** |
| guard v2 (completes the client) | 3 | 0 |

Three clean cycles against a 2-in-4 rate is p ~= 0.125. **Run three more** and it
is near 0.016. The trigger is a teardown, not load: deploy, settle 45 s, kill the
file server, then reattach a client and count the game's own `open host:` lines —
251-427 when alive, zero when hung (`scratchpad/.../teardown-cycles.sh`). Allow
32 s after `ps2client reset`. Five rapid cycles drive the console into
answers-ping-refuses-every-deploy and it needs the physical button.

Reading the counters needs care: `.bss` addresses move between builds, so
re-derive them per ELF (`nm <name>.elf.sym | grep g_noPacket`) — a previous
build's address returns plausible garbage. And a hung game can keep the `host:`
channel busy enough that `dumpmem` never answers, so it is still unknown whether
the fault fired during those three clean cycles at all.

### Find what produces the anomalous SIF RPC completion

**Four reproduction approaches are exhausted, all negative**, so this is now a
code-reading job rather than an experiment:

| approach | scale | result |
|---|---|---|
| amplified RPC load | 120 min, ~79 500 frames, 2.7x density | nothing |
| live editing session | all four channels writing, 50 min | nothing |
| teardown (kill the file server) | 6 unguarded, 4+3 guarded | no crash |
| livetex hammer | 1024 bumps, **757 confirmed PNG re-reads** | nothing |

The last one closed the final condition from the original report - a whole PNG
decoded inside one frame on the main thread while the streamer thread reads. It
needs no GUI: the `livetex.bin` layout is `TXLT`, version, seq, count, then
104-byte records of a 96-byte path + u32 generation, and a footer of
`seq ^ 0x5A5A5A5A`. Re-announce a byte-identical file so `reload()` cannot take
its changed-size escape, target a path the game really knows
(`inc/texture_data.gen.hpp`), and write tmp-then-rename with a retry - Windows
refuses the rename while the file server holds the target, constantly. A
`--livetex` CLI verb next to `--pad` would make this a first-class test tool.

Two things that shape the search now. The fault fired **once**, in a session
nobody instrumented, and nothing since has moved it - so treat any future
occurrence as precious and read the counters immediately. And note the guard
lives only in the GAME's sifrpc instance while ps2link keeps its own live in the
same address space: if completions are being dispatched through ps2link's
unguarded `_request_end`, the game's counter would stay at zero no matter what,
which is consistent with every zero measured so far. **That is the first thing to
check.**


The guard proves *a* completion arrives with a null packet; it does not say who
sent it. Ranked candidates, none discriminated: ps2link's `pkoSendSifCmd()`
reusing one unsynchronised 1 KB buffer with no DMA wait (its own known rough
edge, and the IOP exception handler shares that buffer); the IOP-side reply ring
`_rpc_get_fpacket()`, a 32-slot round robin with no in-use flag and no interrupt
protection; and `_SifCmdIntHandler()` calling `EI()` before it has copied the
packet out and cleared `psize`. Which counter fires — `rejectedNoPacket` vs
`rejectedBadClient` — narrows it. Note ps2link keeps its **own** ps2sdk sifrpc
instance live in the same address space as the game, and the guard is installed
only into the game's.

### Extend hardware observation of the r7 wedge fix

`pkoReset()` touched the screen (`init_scr()`) BEFORE quiescing the DMAC/GIF the
dead game left mid-chain, and `init_scr()` draws through exactly those blocks.
Blocking there blocks ps2link's EE command thread - the one thread that executes
every command - so the console ignores `reset` and `execee` for good while its
IOP-side file server keeps listening. r7 reorders it: hardware, then vectors,
then screen. Written up in
[ps2link-setup.md](ps2link-setup.md). The 2026-10-02 physical-console smoke test
passed **6/6 vehicle-playground reset/redeploy cycles**, including three after
driving, using the user-flashed low packed no-USB r7 build. Fresh ELF loads and
advancing debugger frames confirmed restarts. Longer observation remains open:
six cycles do not establish elimination of the intermittent failure.

**A caution about the evidence for that wedge.** Of the four apparent wedges
observed while chasing it, **two were provably my own tooling** - a cycle script
pointed at the wrong project directory, and a missing `bin/ps2link.run` marker
(without it the game sets `writeLogsToFile` and opens `log.txt` over `host:` for
every log line, one round trip each, so the boot crawls and looks exactly like a
hang). Both presented identically to a wedged console: `DEPLOY FAILED`, a static
picture, no response. So the "five rapid reset+teardown cycles reproduce it"
recipe recorded earlier is **not trustworthy** - the real rate is unknown and
lower. Verify a deploy is fresh from the capture (a boot line, or a low first
`VRAMSTAT f=`) before recording anything about console state.

### Recover the console from the refuses-every-deploy wedge

Historical symptom: ping answers, `tcp/18193` listens, every `execee` is ignored,
and software resets do not recover the console. The earlier five-rapid-cycle
reproduction claim is withdrawn for the tooling reasons above. If this recurs
on r7, retain a fresh-launch capture and the flashed ELF identity before
investigating; a physical Reset remains the historical recovery. See
[ps2link-setup.md](ps2link-setup.md).


### Move generated scene data out of the header

Moving one object changes `scene_data.hpp` and currently invalidates most game
translation units. Emit the data into one generated `.cpp`; keep only stable
types, declarations and shape constants in the header. Audit every
`constexpr`/array-size consumer and prove unchanged projects regenerate
byte-identically before measuring the rebuild win.

### Preview BLSS in the editor viewport

The viewport already renders at PS2 resolution and presents through a fragment
shader. Add an honest preview of the selected BLSS mode and debug view; do not
ship a visual approximation that disagrees with the console. See
[neural upscaling](neural-upscaler.md).

### Drag HUD images in the viewport

Let authors move HUD images directly in the viewport with snapping and numeric
properties staying in sync. The drag must respect the active display mode,
logical canvas and widescreen behavior.

### Unify HUD, loading-screen and credits coordinates

Menus use a logical 512x448 canvas and compensate for output mode and anamorphic
widescreen. Decide which non-menu elements preserve aspect and which pin to
screen edges, then apply one coordinate model to the HUD, loading screens and
credits. See [menu styles](menu-styles.md).

### Finish the BLSS proxy budget twin

The engine-side proxy cap exists behind `TYRA_BLSS_PROXY_BUDGET` but stays off
because the host corpus does not apply the identical rule. Implement the same
whole-box projection, tile count and part stride on the host; enable both twins
in one change and re-run parity plus performance measurements. See
[BLSS reconstruction](blss-reconstruction.md).

### Attribute hardware pipeline stalls before further micro-optimizations

The [hardware timeline](hardware-profiler.md) and seven
[physical controls](hardware-profiler-results.md) are complete. Raster-area
suppression saves little; host I/O costs roughly 5 ms, and EE-side static
submission remains expensive. Next split Dispatch into package classification,
copies and packet assembly, then select a retained representation with explicit
DMA ownership. VIF1 DMA wait is not a VU1 execution timer; no exact hardware
utilization percentage is claimed. The transform-cache and DMA clip-table
candidates still do not earn integration.

### Cache static packet templates only after proving packet lifetime

Static geometry, transforms and bounds are already cached, and compact model
parts now join spatial static batches. Reusing a complete VIF/DMA packet is not
the same operation: camera/frustum clipping and per-frame uniforms still change
its contents, while earlier attempts to retain or recycle packet storage froze a
physical PS2 until a hardware reset. Isolate an immutable geometry-only segment,
record its ownership through DMA completion, and prove it with a console stress
test before enabling any cache. Do not treat a PCSX2 pass as lifetime proof.

### The static batcher's remaining exclusions, counted not guessed

With `drawDistance` moved onto the batch key (1.98.0), the Motor District's
census reads: 142 objects, 31 not batchable shapes at all (roads, areas,
lights, vehicles, the player), **18 rejected by `reflected`**, **6 by
`physics`**, and 87 eligible. The two remaining rows are worth the same
treatment the distance cut-off just got, in this order:

- **`reflected`** is the bigger one and the harder one. The object is
  re-submitted into the environment-map pass through its own solo bag, and a
  batched member has no solo bag. The projected-shadow, portal and highlight
  passes already solved exactly this by baking the solo geometry on first use
  (`objectGeometry[i].parts.empty() && !o.dirty`); the env pass could do the
  same and let the object batch for its MAIN draw. Measure before believing
  it: 18 objects is 18 submits, but the env pass runs per frame.
- **`physics`** is genuinely not batchable while a body is awake. A sleeping
  body is a different question - `physSleep` exists - but a batch that
  re-bakes when a body wakes is the per-frame-rebuild trap, so this needs the
  demotion path to be cheap enough first.

Do the census before any of it. On this scene the exclusion everyone expected
to matter (`dynamicLighting`) rejects nothing at all.

**The census is now one command** (1.110.0):
`tyrax-editor --batch-report <projectDir>`, or *Tools > Static Batches*, which
name the reason per object instead of totalling it
([static-batching.md](static-batching.md)). It reproduces the figures above
from the twin rather than by reading the generated header — 87 eligible, 65
batched in 48 batches, and the 22 solo are **all singleton groups**, which is
the row this item did not have before. So the next step on either bullet
starts by running it on the scene in question, and an arm's effect on the
grouping is a diff of its `[batch]` tail.

### Price static batching on wide-spread content with NO draw distance

Done, for everything that has a cut-off: the grouping cell is bounded by the
draw distance its members share, which removed the +3.20 ms `large-terrain`
regression outright and leaves the Motor District untouched
([model-pipeline.md](model-pipeline.md), "Why the cell is bounded by the draw
distance").

What is left is the case that states no length. `drawDistance` 0 keeps the base
cell (`max(mapW / 4, 48)`), so a big map full of unlimited-distance props still
groups coarsely. Measured on an adversarial `large-terrain` with every cut-off
zeroed, that is a **trade rather than a loss** - +23.5% triangles against
**-43% packet flushes** - where the draw-distance case was worse on both axes
at once. On this console a submit is the expensive half, so it may well pay;
nobody has priced it.

Two things that measurement needs, and neither exists yet:

- **A shipped example of that shape.** Every example in the tree is inert for
  this rule: the district and `impostor-grove` have cells that already fit,
  `deep-forest` batches nothing at all (eligible 0), and `large-terrain`'s
  content all carries a cut-off. The adversarial fixture was constructed by
  zeroing 1,180 draw distances, and **its own repeats are not byte-identical**
  (283-419 px of 200,704 between two captures of one arm), so it can only be
  read for counts. A deliberate example with a frozen, repeatable vantage would
  make this answerable.
- **Hardware milliseconds.** PCSX2 gives the counts and the pixels; the
  flush-versus-triangle trade is exactly the kind of thing its missing EE data
  cache prices wrong.

If it turns out not to pay, the lever is already shaped: give `cellFor` a bound
for the 0 case too, derived from the batchable objects' own extent rather than
from the terrain width (the object cloud is 940 units wide on a 2048-unit map,
so `mapW` over-states the spread by more than 2x).

### Measure opaque state sorting beyond static batches

Static batches already group by resident texture, which removes the safe bulk
of redundant submits and texture binds. A global opaque sort can defeat spatial
culling and can reorder special material paths; texture state is embedded in the
generated packet rather than being a cheap host-side bind call. Add a stable,
cell-local ordering experiment and compare bind/packet counters on hardware
before widening the sort.

### Make the small render targets follow the colour depth

The env map, the camera feed and the four shadow slots are PSMCT32 render
targets read as textures (~192 KB when a project uses all three). They could
follow the project's colour depth the way the post-fx work buffers now do, for
about half of that — but they are bound through `Texture::vramResident`, whose
`TextureBuilderData` has no 16-bit `bpp`, so `TextureBpp` has to be widened
first. Left out of the colour-depth work deliberately: the frame buffers were
90% of the win. See [GS VRAM](gs-vram.md).

### Specialize VU programs per project

Generate only program variants a project may use, including spawn-pool prefabs.
Keep a generate-everything path for Live Link so runtime additions cannot ask
for a missing program. Measure micro-memory headroom (1698 + 206 billboard
words of 2042 for the all-class VU1-clipping set since the VU1 audit, so program
set swaps are gone for every built-in configuration; what specialization would
still buy is room for a project's own looks). See [VU authoring](vu-authoring.md).

### Capture object-data uploads in VU replay

`--vu-replay` captures the qbuffer chain but reconstructs some per-mesh
constants from a memory snapshot. Capture the object-data upload chain too, so
input and output can be paired exactly on both PCSX2 and hardware. See
[the VU framework](vu-framework.md).

### Query runtime procedural compatibility from the AI Assistant

Expose the editor's capability check for a specific procedural graph as a
read-only AI tool. The answer must name unsupported nodes and parameters rather
than returning a bare yes/no. See [runtime procedural generation](procedural-runtime.md).

## Large

### Add terrain mipmaps

Build and upload an opt-in mip chain, terrain first, and use the GS LOD path to
reduce distant shimmer and moire. Account for the roughly 33% texture-memory
cost — the heap is bigger than it was, 1.08 MB at 32-bit colour and ~1.95 MB at
16-bit ([GS VRAM](gs-vram.md)), but a mip chain is still opt-in per texture —
verify small-level addressing on hardware, and keep non-mipped textures
unchanged.

### Offer a 16-bit Z buffer

The z buffer is the largest single block left at 229 376 words, as much as both
frame buffers cost together at 16-bit colour, and `PSMZ16` would halve it. What
stops it being free is precision: the projection runs `near` 0.1 / `far` 51200
and depth is hyperbolic, so 16 bits resolve roughly 1.5 world units at 100 out
and ~38 at 500 — terrain and baked shadows would z-fight. So it is a per-project
option gated on raising `near`, judged on a fixture with a short view distance,
not a default. The mechanical part is small but crosses four layers: the VU1 z
scale (`0xFFFFFF / 2`) is an EE-side constant in `stapip_vu1_program.cpp` and
its dynpip / mcpip twins, and the same constant appears in the generated game's
EE clipper (`templates.cpp`), in `vugen`/`vusim` and in `vucap`. Also unsettled:
whether `PSMZ16` may pair with a `PSMCT32` frame buffer, which decides whether
the two depths can be chosen independently at all. See [GS VRAM](gs-vram.md).

### Render particle emitters in the BLSS corpus

The corpus counts emitter coverage and can describe emitter proxies, but its
training images still omit the particles themselves. Render the same billboard
population, motion and blending as the runtime, then retrain and re-evaluate the
affected example projects. See [neural upscaling](neural-upscaler.md).

### Refresh dynamic content on extrapolated frames

Frame extrapolation carries camera motion between rendered world frames, while
animations, moving objects and HUD updates remain at the world rate. Redraw the
dynamic layers on the synthetic presentation without feeding that frame into
BLSS history. See [frame extrapolation](frame-extrapolation.md).

### Add a frame-extrapolation guard band

Render beyond the visible picture so a camera warp reveals real pixels instead
of stretching the edge. This changes raster size, frustum math and BLSS
host/console assumptions, so treat it as a shared rendering design rather than
a larger texture allocation.

### Grade palettized textures through their CLUT

Remap palette entries through a grading curve for per-pixel textured colour at
no extra draw pass. Pair it with a defined path for untextured geometry, settle
whether runtime palette updates are cheap, and avoid grading any surface twice.

### Load ps2link USB modules from the memory card

Replace the embedded USB IRX buffers with `SifLoadModule` calls to modules next
to `PS2LINK.ELF`. The goal is one build that boots from FreeMcBoot and still
supports keyboard and mouse; verify paths, missing-module feedback and both
launchers on hardware. See [ps2link setup](ps2link-setup.md).

### Host collaboration sessions over the internet

Add an invite-link transport on top of the existing `wire::Transport`
interface, preferably using an optional tunnel rather than exposing a raw
listening port. Define authentication, session lifetime and failure UI before
shipping it. LAN and mesh-VPN sessions must keep working unchanged. See
[collaboration](collaboration.md).

### A devkit self-screenshot command (works on locked desktops and real hardware)

The 2026-08-17 corona session proved the game can dump its own framebuffer
through `host:` (ps2sdk libdebug's `ps2_screenshot_file`, VIF1 reverse FIFO;
pass the framebuffer address in BLOCKS - `fb->address / 64` - or SBP's 14 bits
overflow and the pages scramble). Productize it as a devkit channel: a command
bit in `livedbg.cmd` (the VU capture is the precedent), a debug-only generated
runtime write into `bin/frame.tga`, a Debugger button, the TXDEVKIT marker +
`kStringNeedles` entry, and stale-file cleanup in both Runner launch paths. It
is the only capture path that survives a locked desktop, and the only one that
exists at all on a real console. See [live-debugger](live-debugger.md).

### Preview the AO-only lightmaps in the viewport

The viewport now draws the GI cache's terrain map and primitive atlas per
pixel (docs/global-illumination.md, "The editor viewport"), but a scene with
GI off still previews its ambient occlusion through the analytic per-fragment
twin: that atlas is written by texbake at build time and never cached, so
there is nothing for the viewport to read. Baking it host-side on demand
(`aobake::bakeSceneLightAtlas` is sub-second on the examples) and feeding it
through the same `setGiAtlas` seam would make the AO preview texel-exact too.
While there: the GI bake's ground grid follows object footprint AABBs, so a
ROTATED thin wall still shows a faint version of the straddling teeth at its
AABB's corners - splitting the ground cells along the rotated footprint is the
fix if anyone reports it.


## Animated probe lighting

Full signed RGB SH L1 now reaches animated and explicitly dynamic-lit receivers
without larger probe tables or extra passes (docs/global-illumination.md).
Next quality candidates: contact occlusion around feet, visibility-aware probe
interpolation to reduce light leaking through thin walls, then exact normals
under nonuniform scale/shear. L2, animated self-shadowing and surface-transfer
PRT need separate measurements and are not implied by full RGB L1.

### Previous humanoid LOD hang: not reproduced

The earlier three-humanoid meshLod 1.5 doorway hang had no identified cause.
After exact duplicate-corner skin reuse, the same scene passed the doorway
walk; a second fixture forced tiers 0/1/2 every 120 frames and ran beyond
2400 ticks without stopping. This is a successful stress test, not proof of a
specific hang fix. If it recurs, preserve the ELF, scene, log and pad sequence
before rebuilding. The shipped example still uses one full-mesh humanoid and
lightweight neutral receivers. Avatar skin time fell from about 10.8 to 5.6 ms
in PCSX2, with identical geometry and full-rate animation.

- Impostor follow-up: measure cold versus warm batch GPU capture time and consider
  background batch baking. Configurable 4/8/16 views and optional GPU capture
  with CPU fallback are implemented; see [impostors](impostors.md).

### Hardware timeline follow-up (1.92)

Native editor viewing and finer package/classification/copy/packet scopes are
implemented. The bounded same-range classification reuse trial was rejected: no
convincing submission-time gain on physical PS2. Larger submission scheduling
changes remain open; do not treat this as a shipped engine speedup. See
[hardware profiler results](hardware-profiler-results.md).

## Baked decals print onto receivers in front of the caster

`shadowbake`'s per-triangle depth filter (`litPastCaster`, docs/shadows.md,
"Print through a wall") keeps a receiver triangle whenever a sample point sees
the sun, so a receiver on the SUNWARD side of the caster keeps the tile's ink.
A caster sunk into another object (a post 0.8 units into a plinth) printed its
buried silhouette on the plinth's lit face. The example was fixed in the scene;
the bake fix is to split projected triangles into pieces (like the caster's own
floor pass, `kSelfPiece`) and drop a piece whose points all have the caster
BEHIND them along the light (a trace away from the sun hits the caster) and
none has it toward the sun. Verify on a copy of examples/baked-shadows with
post-5 back at y = 1.

The [Core prefix and Core-owned submit record](tyrax2-prefix-submit-scopes-2026-10-04.json) qualifies separate V8/V9 both-order physical pairs and emulator controls. Own On windows, actual dynamic reads, tax/chunks and sparse contexts remain bound to each source/ELF. No cross-version subtraction, optimization gain, pure EE/GPU bill, common cost or ordinary 60 FPS acceptance.
