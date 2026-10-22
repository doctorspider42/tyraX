# EE performance audit â€” 2026-10-05

Read-only source and linked-target audit of `codex/tyrax2-foundation`, HEAD `d085f938e76f5bbb860137219a73dda35d25e568`. No source changes, builds, emulator launches or console commands were performed. Paths and line numbers below refer to the current repository, not private candidates. `source-pins.json` pins the principal inspected files. Runtime activation and performance measurements quoted below come from existing qualified records; all new proposed gains remain unmeasured.

## Executive assessment

EE still performs substantial render preparation: object visibility and package routing, light choice/localization, projected-light geometry and ST/color production, particle/beam construction, packet emission, snapshot ownership and native-chain conversion. Calling all of that â€śgeometry preparationâ€ť hides several independent linear scans and copies. The most credible direction is to remove entire producer stages or transfer their arithmetic directly into the already consuming VU1 program. Dispatching one tiny function at a time to VU0 is unlikely to help.

There is no evidence here for one catastrophic remaining generic Tyra bug. Many famous original inefficiencies are already repaired in the shipped StaPip path: per-package allocation, eight-corner frustum transforms, repeated max-size propagation, extra native sizing, per-material texture lookup and software squared-length square roots. Some unrepaired inherited APIs remain, but the generated game bypasses them. Their existence cannot explain this fixed night's frame time without positive call evidence.

The source-only suspicion that night effects repeatedly call software `sqrtf` was checked against the actual linked V31 ELF and rejected. The beam function contains three `sqrt.s` sites and the pool function eleven; neither calls `sqrtf`. One genuine direct call exists in `updatePlayer`, which is a different path. This prevents a plausible but ineffective bulk replacement.

The linked witness is the private V31 far-light ELF: only four engine/debug files differ from its qualified corona-pricing baseline; generated game source is unchanged. It is evidence about emitted instructions for those pinned templates, not a claim that the private picker or debug instrumentation is shipped. Its ELF and symbol text were independently qualified by the root; their hashes are recorded in `disassembly-proof.json`. Historical observations below remain specific to their own configurations.

## Existing evidence that constrains priorities

- `docs/tyrax2-night-ablation.md:25`: joint removal retains the car/map/sky/HUD/material environment and changes full-night work about18.8â€“18.9 ms to13.37â€“13.39 ms, with engine flip-return cadence33.3667â†’16.6833 ms. Simplified night can reach ordinary NTSC cadence; full quality is not established.
- Same document:52â€“54: separate pool/receiver, beam/corona and glow cuts are approximately0.875â€“0.968,0.681â€“0.782 and0.296â€“0.530 ms respectively. These are whole-route elapsed contrasts, not pure EE bills or additive components.
- `docs/tyrax2-player-light-receivers.md`: private player-only receivers improve roughly1.4â€“1.5 ms; removing scene pools as well improves roughly2.25â€“2.37 ms versus full. Still no stable60. The129 receiver rejections include preparation calls that never actually picked a light.
- `docs/tyrax2-ee-vu0.md:63`:171,840 mutable REF bytes across421 operations and106 snapshots/frame in the fixed census; original static roads do not contribute copied mutable REF bytes. Immutable-owner labels alone are not borrowing permission.
- Engine skill/current historical records: existing waits around0.003 ms and flush around0.009 ms in a qualified October2 view; old1.09 ms flush estimate is obsolete. Duplicate inverse work measured only about0.0635 ms in a scoped diagnostic; a64-byte input key cost about0.138 ms. A generic inverse cache is a poor first target.
- Root's completed conservative far-light gate: 114 picks/912 candidates, 61 safely rejected (~6.7%). All 12 cold frames matched 114 pointer comparisons each. Both physical orders regressed by +0.238528/+0.230035 ms against their own mean controls; all rendered periods remained approximately33.367 ms. This predicate is rejected for production; fewer square roots did not improve the frame.

## Findings and discriminating experiments

### 1. Projected-light STQ, reach colors and copied receiver vertices remain an EE producer â€” high priority

**Source:** `src/game_templates.inc:14504`,`:14562`,`:14604`,`:14615`,`:14634`.

**Trigger/work:** a flashlight hitting supported world-space receivers scans triangles, computes cross-product normals/centroids/facing, appends three copied vertices, then generates projective STQ and reach-modulated colors for each accepted triangle. The shared output budget is3997 vertices. One geometric pass becomes three mutable arrays and a second rendered receiver pass. Unsupported matrix-mode/animated receivers are intentionally excluded at14584.

**Evidence versus hypothesis:** these loops and allocations of bandwidth are actual source behavior; activation/counts in the current camera and their exclusive EE cost are not established. Scene receiver batching accounts for34,272 copied Bag bytes in a historical origin census, but that does not identify every flashlight branch.

**Change:** keep EE receiver selection and exact triangle budget/facing initially; send original immutable receiver positions plus a small per-light affine projector and reach parameters, and generate STQ/color at VU1 consumption. Algebraically the numerator and denominator are affine dot products; the existing camera transformation remains VU1 work. A subsequent stage can transfer facing/compaction to VU1 only if its ordered budget contract is preserved.

**Risks:** exact STQ clamp atfwd0.05, GS depth equality, per-receiver budget order, alpha conversion and winding heuristics; VU1 register/micro-memory pressure and changed package limits. Current qualified resident programs nearly fill micro memory, so this needs replacing/streaming a variant rather than appending blindly.

**Experiment:** first count accepted triangles and bytes/cold boundaries in ordinary batching. Compare a producer-only candidate with identical emitted final vertices/STQ/RGBA and physical both-order windows. Do not accept â€śEE loop deletedâ€ť when an oracle silently recreates its arrays or when the candidate adds more VU1/GS work than it removes.

### 2. Flashlight terrain hull is a nested sampling kernel â€” high priority if active

**Source:** `src/game_templates.inc:15212`,`:15217`,`:15222`,`:15229`; `:21434`; road query`:21309`,`:21326`,`:21352`.

**Trigger/work:** an off-geometry flashlight footprint samples `(4*cellsA+1)*(4*cellsC+1)` ground points; declared storage supports up to33x17=561samples. Each cell subsequently evaluates25 sample-versus-bilinear-sheet differences and updates four maxima. `groundSurfaceAt` invokes terrain AND indexed road lookup; a road query iterates candidates in the current spatial cell, recomputes triangle bounds/denominator/barycentric coordinates and divisions.

**Evidence:** the nested loops are current code. The nearby comment â€śabout300 height readsâ€ť is an estimate, not a current runtime count. Roads already have a spatial index; there is no full-scene road scan here.

**Change:** batch x/z generation and bilinear-sheet/bulge arithmetic into a VU0 macro kernel first. Keep irregular road lookup/gather on EE. Alternatively precompute static road triangle coefficients and inverse denominators at index construction, preserving exact fallback for ill-conditioned triangles. A VU0 micro kernel could consume one contiguous sampled-height tile and return only hull heights.

**Risks:** road-edge tolerance0.0001, overlap â€śhighest road winsâ€ť, grip/coverage semantics, terrain triangle-versus-bilinear differences, moving/streamed road invalidation; arithmetic reassociation alters bulge/depth behavior. VU0 upload/readback and ownership can exceed this small kernel's savings.

**Experiment:** cold count actualdimensions,terrain reads,road candidate tests and triangle-denominator reuse; scope the whole producer once with calibrated observer controls. Freeze sample inputs and compare hull outputs before any runtime test. Never infer whole-pool gain from arithmetic instruction count alone.

### 3. Static cone batches rebuild scratch output even when write-on-change preserves the submitted arrays â€” medium/high

**Source:** `src/game_templates.inc:17590`,`:17597`,`:17612`,`:17731`,`:17766`,`:17780`; `:291`.

**Trigger/work:** `TYRA_BEAMS_KEEP` avoids writes/re-stamping of unchanged final arrays, but every frame still walks visible lamps, gathers6 corona and 24 cone vertices per shaft, re-scales cone colors, appends scratch vectors, then compares scratch/final bytes. Static cone geometry is retained per lamp, not the aggregate production effort.

**Evidence:** actual source. Existing beam/corona whole-route cut around0.7 ms bounds the combined route; this is not its pure producer cost. Corona GS SPRITE trials and pool-lattice trials already exist; this is not a new reason to rerun them unchanged.

**Change:** separate view-dependent coronas from world-space cones and separate topology/membership change from brightness change. Reuse retained cone ranges when ordered membership, geometry, tint and brightness match. If brightness changes, send one coefficient/descriptor and let VU1 multiply colors instead of building a 24-element color stream. An emitter descriptor can generate billboard corners directly on VU1, but current billboards/SPRITE experiments must be reviewed for overlap.

**Risks:** portal-specific eye, frustum membership, flicker levels and list order; losing a disappeared lamp, stale live color, false content/bbox invalidation; larger descriptor/scratch ownership than savings.

**Experiment:** count actualstable membership and unchanged inputs without constructing output; then compare direct packet output in stationary AND moving-camera/portal cases. Measure the selected producer and total work with both orders.

### 4. Per-beam lookup of a dynamic light is a nested list walk â€” low effort, activation first

**Source:** `src/game_templates.inc:17628`â€“`:17634`.

**Trigger/work:** each dynamic beam searches `g_dynLights` by object index to retrieve`lastLevel`:O(beamCount*dynamicLightCount) worst case. Small engine pick registry capacity does not prove the generated runtime vector is equally small.

**Change:** store a validated runtime-light index with the beam or build an objectâ†’runtime-slot map at setup. Read the live level through that slot; rebuild on scene/runtime registration changes.

**Risk:** lifecycle/index invalidation and duplicate object registrations, animated/flow state, portal sharing.

**Experiment:** cold count loop iterations; exact level/beam-output parity across scene switch and light toggles. No per-lamp clocks are needed. This is ordinary EE bookkeeping, not a useful VU offload.

### 5. Flashlight exclusion reconciliation performs pairwise membership work and repeated bag writes â€” medium

**Source:** `src/game_templates.inc:14023`,`:16381`â€“`:16404`,`:16425`,`:16469`.

**Trigger/work:** last frame's skip list is reset, the new one applied; `updateFlashSpotOff` scans projected boxes and checks each against extras, then scans previous flags against the new vector. Every retained object is re-applied across its parts. ComplexityO(boxes*extras+oldOff*newOff+flaggedParts). Part rebuilds explain some repeated application; not all writes are redundant by definition.

**Change:** dense object-index epoch/bitset membership and explicit applied-state/version per info-bag owner; only change flags when desired state or owner generation changes. Retain shared-batch safety: only lone batches can take a receiver flag.

**Risk:** restored newly rebuilt bags, shared batchInfo ownership, one-light shadow slot, moved models and global overrides. Current flags affect rendering in later/next passes, so reconcile timing must remain identical.

**Experiment:** cold census comparisons, writes and owner rebuilds; exact bag-flag transcript and raster under torch sweep/scene reload. Price the whole reconciliation region once. VU is unsuitable for this pointer/owner bookkeeping.

### 6. Light selection/localization runs per bag rather than per genuinely independent receiver â€” medium, current gate is narrow

**Source:** `renderer_core.cpp:284`â€“`:329`; `stapip_core.cpp:846`â€“`:876`; `stapip_qbuffer_renderer.cpp:1048`,`:1309`.

**Trigger/work:** each eligible unlit bag derives a world sphere, scans up to 9 lights, performs falloff/cone scoring, then transforms the winner into local space including inverse/direction normalization. Several parts can share transforms yet require distinct sphere-based picks. A disabled spot flag overrides an already computed pick at876.

**Evidence:**114 actual retained picks/912 candidates in existing fixed hardware view; discarded picks measured zero there. Far rejection61 candidates demonstrates modest conservative activation. Affine inverse price is small relative to previous broad guesses.

**Change:** the tested far predicate is rejected; require a more highly activated change before broadening. If exact bounding sphere identity is shared, submit a receiver-selection descriptor once and use it across parts. Consider conservative per-object light shortlist at scene/query preparation, then preserve original final scoring/ties. Reconcile spotLit before picking only where BLSS/shared-sphere consumers and callbacks remain exact.

**Risks:** live public writable light arrays, strict score ties, fallback&spot, zero/negative colors,NaNs,skipSlot and per-material extent differences. Moving the winner selection to VU1 forces extra light data per bag and can disrupt packet specialization; a9-candidate loop is not automatically a good VU0 microjob.

**Experiment:** cold counters for actualshared sphere identities and shortlist exclusions; pointer differential oracle including pathological inputs. Hardware both orders; never use selection invocation count as eliminated DMA count.

### 7. Mutable REF ownership duplicates already prepared output and scans chain tags â€” medium/high architectural

**Source:** `frame_chain_arena.hpp:261`â€“`:317`; `vif1_queue.cpp:363`,`:429`â€“`:473`; `stapip_qbuffer_renderer.cpp:2924`.

**Trigger/work:** ordinary producer packet is snapshotted; mutable REF payloads are copied into frame-owned storage, tags validated/fixed, then native prefix emitted by another tag walk. Current counted snapshots already avoid a separate native sizing walk. Fixed census reports171,840 mutable bytes/frame, but unknown ownership is significant.

**Change:** owner-controlled transient producers emit directly into the current frame bank under bounded allocation, eliminating a whole copy for that owner. Stable storage can be borrowed only with actual immutable leases; public writable mesh aliases cannot be reclassified globally. A typed trusted internal producer can combine validated snapshot/native emission, leaving generic public parser behavior intact.

**Risks:** rollback after capacity failure, cached aliases, bank retirement, caller callbacks, reader leases and scene transitions. Direct producer experiments already exist; generic borrowing and SPR/CALL were not magic wins.

**Experiment:** choose one positively activated owner/interval; compare exact chain bytes and tags plus adversarial rollback/overflow/lifetime controls. Prove copied bytes actually fall for that owner, then physical work benefit. VU0/VU1 are not good replacement engines for pointer validation or RAM DMA ownership.

### 8. Clipped strips expand and partial packages combine via EE copies â€” medium, conditional

**Source:** `stapip_core.cpp:1296`â€“`:1314`,`:1322`â€“`:1346`; `stapip_qbuffer.cpp:183`â€“`:196`,`:253`â€“`:256`.

**Trigger/work:** a strip genuinely crossing a clip plane expands3 vertices per triangle on EE into copy pools; partially classified subpackages can copy vertices/ST/colors/normals to combine smaller blocks. Guard-band and fully inside paths already use pointer fills.

**Change:** strip-aware clip microprogram or triangle/index descriptor consumed directly on VU1. Retain the existing list fallback initially. Better coarse routing may remove producer work but increases offscreen VU/GS work.

**Risk:** strip run boundaries/parity/degenerates, register pressure, clip vertex capacity, full plane masks, package alignment and coplanar depth.

**Experiment:** cold count stripExpanded/copied qwords and clipped triangle count in the exact night pose. If zero, exclude from stationary optimization. Test edge/near-plane motion before physical both-order replay.

### 9. Duplicate clip-plane computation remains, but the obvious reuse experiment regressed â€” low priority

**Source:** `stapip_core.cpp:719` and`:731`.

**Trigger/work:** a partial bag that fails whole-bag guard-band promotion computes8object-space clip planes twice in one render call.

**Evidence:** historical exclusive census about0.377 ms/11duplicatefallback calls includes observer/macro helpers. Same-call reuse candidate measured18.248/19.137/18.411 ms and was rejected. This is real redundancy but NOT a qualified optimization.

**Change:** only revisit with pinned routing/layout and exact target disassembly; fold the reuse into a broader proven package-routing change rather than assume two computationsâ†’half parent time.

**Risks:** adaptive order/code layout and reused state from another view; the old regression's cause is unresolved.

**Experiment:** replay the prior candidate with selector/order fixed and counters compiled out, preserving its negative attempt. No more unqualified â€śremove duplicate, obviously fasterâ€ť claim.

### 10. Geometry updates precede visibility/distance rejection â€” medium, moving-object dependent

**Source:** `src/game_templates.inc:23705`â€“`:23732`.

**Trigger/work:** dirty objects rebuild geometry or refresh matrices before checking visibility/draw distance/coarse outside. Culled moving non-fast-path objects can therefore pay a full bake; matrix updates are much smaller.

**Change:** separate simulation/transform state from materialization. Defer only geometry bake until a consuming view/collision/shadow actually needs it, with conservative author/runtime bounds for preliminary rejection. Preserve other passes that need offscreen objects.

**Risk:** collision/probe/portal/shadow consumers, moving bounds, LOD state, object dirty semantics; blindly moving rebuild below main-camera culling would omit needed geometry.

**Experiment:** count dirty rebuilt-but-not-main-drawn objects and their secondary consumers. Qualify offscreen motion followed by instant re-entry/portal reflection. If the stationary fixture has no dirty culled rebuilds, use a representative moving scene rather than claim night gain.

### 11. Vehicle paint performs EE shading/scatter on key changes â€” VU1 candidate for moving views

**Source:** `src/game_templates.inc:23516`,`:23545`,`:23597`â€“`:23620`.

**Trigger/work:** quantized view/light-key change computes fresnel/specular once per distinct normal then scatters full color output. Existing hysteresis/unique-normal sharing already avoids naive every-frame per-vertex shading. The whole overlay is an additional rendered body pass.

**Change:** pass compactfwd/half-vector parameters and compute modulation beside VU1matcap ST/GScolor packing; remove expanded-color producer and content invalidations. Retain quantized key behavior for an exact first trial.

**Risk:** alpha1..255 andHIGHLIGHT2, unique-normal arithmetic identity, VU1budget/package memory, moving cameraquality and changed replay cost. May trade cheap stable retained replay for expensive per-vertex VUwork.

**Experiment:** stable-camera/moving-camera split with positive key changes and actual VUoutput differential. Price separately from material pass deletion. A stationary night does not establish the motion benefit.

### 12. Cache retirement scans all entries twice each frame â€” low/medium general cleanup

**Source:** `stapip_bag_bboxes_cacher.cpp:37`â€“`:54`,`:71`,`:152`.

**Trigger/work:** per-frame countdown walk then remove_if over all bbox entries; any eviction rebuilds the hash index. Existing lookup is bucketed, so this is not a linear lookup bug.

**Change:** absolute last-used/expiry frame and incremental expiration buckets or bounded maintenance; remove redundant countdown stores. Small fixed caches might make complexity not worth it.

**Risk:** unsignedwrap, exact expiration behavior/addressreuse and deleted entries invalidating indices. Any keep-alive interval modification may increase memory usage.

**Experiment:** coldentrycount/evictioncount plus one calibrated frameEndmaintenance scope. Compare adversarial wrap/streaming behavior. Keep on EE, notVU.

### 13. Inherited high-level mesh APIs still allocate per draw â€” real red flag, not current night evidence

**Source:** `static_pipeline.cpp:56`,`:61`,`:62`,`:96`,`:120`,`:141`,`:157`,`:179`; `dynamic_pipeline.cpp:177`â€“`:184`,`:202`â€“`:209`,`:111`.

**Trigger/work:** public `StaticPipeline::render` heap allocates info/directional bags and material bags. DynPip allocates temporary pointer arrays at each half-buffer flush and final partial send, and uses floating ceiling division for package count. Generated main models mostly submit retained bags directly to StaPip; generated skeletal animation uses SkelInstance and StaPip.

**Change:** stack or reusable per-instance bags for synchronous preparation, preallocated pointer arrays, integer ceiling division with overflow and zero-size guards. Metadata must be snapshotted before reusable owners change.

**Risk:** caller lifetimes and directional-bag pointer reuse under asynchronous submission. Preserve public options and manual material behavior. These changes need broad engine examples, not only this game.

**Experiment:** prove actual calls in the tested example first. Count allocations without sampling the heap at every draw; test StaticMesh/DynamicMesh examples plus mixed StaPip/DynPip ownership. This is not the missing 2 ms until activation is established.

### 14. A surviving software sqrt and software-double pow are narrow inherited cleanup leads â€” low priority

**Source:** `src/game_templates.inc:9006`; `renderer_core_texture_sender.cpp:37`â€“`:41`.

**Actual target evidence:** V31 `updatePlayer+0xd8` at `0x1a5468` calls `sqrtf` at `0x2979f0`, then `__ieee754_sqrtf` at `0x2997d8`. Its implementation loads loop count 25 at `0x29984c` and loops at `0x299860`â€“`0x29988c`. Sender `getSizeInMB` calls double `pow` at `0x236b6c` and `0x236b88`. Repository searches found no caller of Sender `getSizeInMB`; its linked presence does not prove per-frame execution. PNG `pow` calls belong to loader/gamma code, not ordinary night frame work.

**Change:** stick length for finite bounded input could use the hardware helper with validated control semantics. Texture dimensions expressed as a power of two can use an integer shift with checked exponent. Neither needs VU offload.

**Risk:** errno, nonfinite input, signed zero, rounding and general public semantics. The walker route may not run while driving; one call per frame is not a major timing budget. With a centered stick, the software square root takes an early zero return rather than its 25-step loop.

**Experiment:** actual route count first, target instruction difference second, movement/deadzone boundary differential. Do not replace every libm call indiscriminately.

## What can realistically move to VU0/VU1?

1. **Best VU1 fit:** arithmetic consumed immediately by rendering: projective STQ and reach modulation, paint colors, billboard corner generation, strip/index expansion. EE should send parameters and immutable input rather than materialize expanded arrays. This avoids an EE round trip.
2. **Best VU0 fit:** contiguous bulk numeric kernels whose results EE genuinely needs: batched hull bilinear error/max fold, multiple light/sphere queries, weighted skinning/pose work. Start with macro-mode batches to reduce per-Vec4 load/store overhead. Existing Vec4, M4x4, bounds and skinning already execute VU0 macro instructions (`vec4.cpp:34`, `core_bbox.cpp:33`, `skel_instance.cpp:486` onward).
3. **VU0 micro parallelism constraint:** the ray tracer already owns micro/data memory and polls VPU STAT (`vu0_raytracer.cpp:25`â€“`:45`). Macro COP2 uses its register file. A background job needs a global ownership protocol and EE intervals that do not call hidden Vec4/M4x4/bbox/skinning helpers. â€śEE logicâ€ť is not automatically COP2 free. Reserve a bounded batch, kick it, run scalar state work, then fence before the first macro consumer. Measure launch, upload, readback and lost macro overlap as part of the candidate.
4. **Poor offload fits:** tag validation, pointer ownership/leases, vector/hash lookup, mixed road gather and object-registry reconciliation. Make these cheaper or less frequent on EE.

## EE can prepare the next frame, but cannot outrun readers indefinitely

The current renderer already supports N/N-1 overlap. `RendererCore::beginFrameRecording` at `renderer_core.cpp:423`â€“`:434` selects a new recording context only when its conditions admit pipelining; field rendering, BLSS and limiter configuration can retain compatibility paths. `Vif1Queue::beginRecordingFrame` at `vif1_queue.cpp:589`â€“`:602` alternates banks and waits for the previous reader of the reused bank. Before presentation, `completePipelineFrame` at `renderer_core.cpp:461`â€“`:471` waits for VIF completion, GS FINISH and pacing. These are different events: VIF DMA consumption is not GS completion, and GS FINISH is not TV photons.

Removing these fences can make EE overwrite geometry while VU1 reads, sample a raster still being written, or reuse a display buffer still onscreen. More buffering can increase input latency and VRAM/RAM pressure. The meaningful goal is to reduce EE's repeated preparation and exploit bounded overlap while measuring consumer deadlines and job ownership separately. One chain describes submission organization; it does not remove render preparation or synchronization.

## Recommended order

1. Reject the tested far-light predicate for production. The root reports both completed physical orders: +0.238528/+0.230035 ms versus their own mean controls, 61/912 candidates rejected and all rendered periods approximately33.367 ms. All 12 cold frames matched 114 pointer comparisons each. These are root-provided checkpoint results, not independently parsed audit measurements. Preserve the negative attempt; do not optimize this low-activation predicate further on instruction count alone.
2. Collect cold activation for projected receiver production, flashlight terrain hull and beam scratch construction under ordinary batching. Preserve player-only receiver cuts as separate appearance controls.
3. Price one coarse producer scope at a time with Off/On/Off observer controls. Avoid per-object fences, sample-window host I/O and broad register snapshots.
4. Implement one highly activated appearance-preserving producer elimination, preferably STQ/reach on VU1 or static cone producer reuse. Qualify exact output, then both hardware orders.
5. Use moving/showcase scenes to activate paint, clipping, culled rebuilds and legacy APIs. Do not rank their source complexity as this stationary night's bill.

The audit identifies concrete work and discriminating experiments; it assigns no invented millisecond savings and guarantees no full-quality 60FPS result.

## Archived evidence

The subsequent [physical producer observer trial](tyrax2-night-producers.md)
changes the priority for this fixed driving scene: projected flashlight receiver
production and flashlight terrain hull have zero calls in 960 observed loops.
Beam scratch and separate corona/cone commits total approximately 0.235 ms in
three On windows. These leads remain applicable to other activated views, but
do not account for the missing multi-millisecond night budget here. Observer
net tax is unresolved against drift; no subtraction or optimization is claimed.

[Source pins](tyrax2-ee-performance-audit-2026-10-05/source-pins.json), [linked square-root sites](tyrax2-ee-performance-audit-2026-10-05/sqrt-instructions.json) and [disassembly hashes/excerpts](tyrax2-ee-performance-audit-2026-10-05/disassembly-proof.json) accompany this audit. Full disassemblies remain in the external LAB; hashes and compact excerpts are archived here. The completed physical far-gate result is independently documented in [the experiment report](tyrax2-far-light-gate.md).

The [Core prefix partition](tyrax2-core-prefix-partition.md) now qualifies three disjoint preparation regions in both physical clock orders. Prioritize the head/bounds/package region and object-data routing for narrower call-site pricing; texture/program/light facts remain a separate inclusive region. These elapsed measurements include existing waits and observation effects, and do not prove a scalar EE bottleneck.

The [sky retint experiment](tyrax2-sky-retint.md) identifies the physical 1,152-vertex dome owner and resolves a repeated 0.80-0.85 ms same-ELF gain by retaining geometry during RGB-only changes. Wheel-owner activity is zero in those particular cold snapshots; do not substitute the emulator's 1,200-vertex owner for this physical sky witness.
