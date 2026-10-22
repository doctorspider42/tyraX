# TyraX2 scene work audit

This audit reconstructs the work requested by Vehicle Playground before choosing another renderer optimization. The decision register remains [the investigation register](tyrax2-performance-investigation.md); this is its A01 evidence ledger, not a replacement.

## A15 EE packet preparation walkthrough (2026-10-09)

Follow-up: A16 fixes and qualifies
the TEX1 ordering defect below, and tests target rejection separately. The
walkthrough records the audited pre-fix order, not the corrected runtime order.

Source audit requested after A14, not a new experiment. No runtime code, build,
deployment or authored map was changed. Hardware tests remain paused and
power-off remains forbidden.

### Scope and units

This is the main StaPip route used by terrain, roads, static meshes, vehicle
bodies and their additional passes. DynPip/skinning and sprites/postprocessing
are separate producers. A bag is one geometry/material/pass submission, not
necessarily a whole object. A package is a VU-sized geometry range; a qbuffer
describes its streams. A command packet can contain multiple ranges/bags. The
frame chain combines these packets and other commands in execution order.
Submitting a bag or recording a packet is not necessarily a physical DMA launch.

### Ordered path

1. Scene code chooses views, passes and geometry. Reflection capture invokes
   scene producers again with its own view. Terrain companion passes and each
   material can create additional bags for the same underlying geometry.
2. `StaPipCore::render` rejects empty input and resolves enabled layout/features.
   VU capacity depends on input streams, output layout and the selected program.
3. Bounds are looked up by geometry/count/version/capacity, or constructed on
   a miss. Model/view-projection reuse checks include matrix values, not just
   pointer identity. View planes are transformed to object space.
4. The whole bag is classified. OUTSIDE returns before texture, light and
   uniform work. MVP is computed or reused for accepted work. A partially
   visible bag inside the actual VU guard band can still take the direct route.
5. Batch compatibility is decided before texture use so pending readers precede
   a required upload. Texture residency is checked and upload commands prepared
   when needed. Wrap changes use ordered in-band state, not an unconditional
   EE drain in frame recording.
6. The VU program set is ensured. Eligible lighting selects lights and converts
   parameters to local space; optional lighting work is conditional. Fog facts
   and whether clip uniforms can be needed are established.
7. `sendObjectData` writes the per-draw command prefix: MVP, required lighting,
   fog/material/texture/test/blend state and optional environment/paint basis.
   FLUSHE protects absolute VU uniform storage from an earlier microprogram;
   FLUSH/DIRECT orders applicable GS state after prior XGKICK output. Temporary
   uniforms are copied inline. This does not yet close or submit the packet.
8. The current source then calls `setClipperMVP` and `setInfo`. Cache keys are
   finalized after primitive state is set. This order has the TEX1 issue below.
9. An eligible baked replay hit appends the cached geometry command body. It
   skips package dispatch, NOT the bounds/state/texture/uniform prefix above.
   Direct misses describe input ranges by pointers; they do not always copy
   every vertex or run EE clipping.
10. Partial visibility uses pooled package records and range tests. Outside
    ranges disappear, accepted ranges use the cull program, and boundary ranges
    can split into smaller subpackages for the clip program.
11. Boundary strips use a cached triangle-list expansion or expand on the EE
    on a miss. Applicable terrain refinement classifies finer ranges and merges
    safe adjacent spans. Some list subpackage combinations copy/concatenate
    streams into pooled buffers. The legacy EE clipper copies and clips on CPU,
    but that is not the active clipping route in the measured fixture.
12. Qbuffer dispatch emits or replays VIF UNPACK/REF commands, range size/clip
    mask, strip/list primitive state and MSCAL/MSCNT. Retained command replay
    must adapt entry state to the live preceding program. Compatible bags may
    share a packet; capacity, incompatibility and texture mutation end batches.
13. `sendPacket` closes the chain and submits to the VIF1 queue. Recording owns
    command data and snapshots mutable REF payloads. Registered immutable spans
    may remain borrowed with lifetime leases. This is not a second unconditional
    copy of every geometry stream. Bounded resource failure has drain/retry or
    fallback paths. Source-copied packet storage can be reused immediately;
    otherwise reuse waits for the relevant reader sequence.
14. Native frame recording assembles ordered transport from those owned
    snapshots. Barriers and cache writeback preserve cross-path ordering and
    EE-to-DMA visibility. Recorded barriers are not automatically CPU waits.
15. Frame end flushes pending producers, closes recording and records final
    completion. The pipelined renderer can mark a pending frame and return;
    completion, framebuffer reuse and presentation synchronize when required.
    Frame-bank reset waits for its prior reader before releasing owned data.

Source entry points: `stapip_core.cpp::render/renderPkgs/renderStrippedPkgs/
renderSubpkgs`; `stapip_qbuffer.cpp` pointer/copy/strip fills;
`stapip_qbuffer_renderer.cpp::sendObjectData/setInfo/addBuffersDataToPacket/
sendPacket`; `vif1_queue.cpp::submit/beginRecordingFrame/endRecordingFrame`;
`renderer_core.cpp` frame recording and pending presentation.

### Intended division of work

- At load or actual geometry mutation: prepare topology, spatial bounds, stable
  streams and reusable command bodies. Do not rebuild these solely because the
  camera moved. Existing caches and pools already implement much of this.
- Per frame/view: decide necessary views and passes, visible consumers and
  conservative visibility. Reject unnecessary work before detailed draw prep.
- Per accepted draw: supply changing transforms, material/texture addresses,
  relevant lighting and capture basis, then select ranges and queue commands.
- EE owns command scheduling, DMA-visible storage, coherence and ordering.
  VU performs bulk vertex transforms, active clipping and applicable shading,
  fog and environment coordinates; GS rasterizes. Some EE vertex work can still
  be justified by total cost and overlap, so it is not categorically forbidden.

### Conflicts to preserve when simplifying

- Screen visibility and the VU safe guard band are different volumes. A14's
  candidate should first be an additional conservative OUTSIDE-only gate for
  the actual target. Do not accidentally route accepted edge geometry through
  expensive clipping by shrinking the existing guard band. Independently
  changed plane sets also need explicit cache identity/invalidation.
- Static geometry does not make MVP, light state, VRAM addresses or environment
  basis static. A held capture must retain its accepted basis, not adopt the
  latest camera basis. Replaying an entire stale draw is not a safe substitute
  for replaying its immutable geometry body.
- A valid C++ pointer is insufficient for zero-copy. Sky/proxy/colour streams
  can be overwritten by another view or frame before DMA consumes them. Keep
  snapshots or a proven last-reader lifetime lease. DMA completion alone also
  does not justify deleting the VU uniform FLUSHE dependency.
- Clip capacity includes worst-case output expansion and scratch. Current VU
  clipping divides capacity by six and rounds to complete triangles; legacy
  EE clipping uses a different budget. A 66-vertex strip represents 64 triangles,
  or 192 list vertices; the ordinary textured boundary route can require 16
  calls of 12 input vertices. Larger input batches need a memory/output proof,
  not just a larger constant. Preserve strip parity and live program state.
- Late subpackage rejection still pays earlier per-bag preparation. Earlier
  rejection is useful only with conservative, current bounds. Grouping draws
  must respect blending, coplanar passes and texture/state dependencies.
- Packet-to-owned-snapshot-to-native transport is real compatibility work, but
  prior H02/C05 direct/fused attempts regressed. Do not repeat them without a
  new isolated hypothesis and matched evidence.

### Concrete correctness finding: TEX1 precedes current filter selection

`stapip_core.cpp:1060` calls `sendObjectData` before `setInfo` at line 1064.
The former serializes TEX1 from `lod->mag_filter/min_filter`
(`stapip_qbuffer_renderer.cpp:1500`), while the latter sets those fields from
the current bag's `textureMappingType` (lines 1624-1635). Thus a nearest bag
following linear state can emit linear filtering, then leave nearest state
for the next bag. Identical consecutive modes hide the discrepancy. Geometry
replay does not repair the already-emitted uniform prefix. Separate static
preparation calls do not guarantee the right state before every normal draw.

This is source evidence of a state-order defect, NOT a demonstrated cause of
the FPS loss or a target-reproduced visual bug. Before changing the order, add
an emitted-packet regression alternating nearest/linear across direct replay,
partial clipping and batch boundaries, and verify primitive/cache entry state.

## A14 reflection call-site audit (2026-10-09)

The user requested a fresh source walkthrough before another experiment. This
section is source evidence, not a new physical price or a shipped optimization.
No build, deployment, authored-map change or renderer change was made.

### Actual call chain and configuration

Current `src/game_templates.inc::renderScene` prepares the shared camera basis,
tests cadence/users, computes the reuse key, and only then decides to capture.
Capture calls envMap.begin -> pushEnvView -> sky dome / sky bodies -> generated
ground capture -> reflected object loop -> popEnvView -> envMap.end. The main
sky, terrain residency update and main geometry follow later in renderScene.
The classic shared pass does not update terrain residency or rebuild roads.
Road submitHeavy is immediate here; heavyCollect is set later for the main
view and reset after collecting its roads. RenderObjectProbe is a different,
inactive path in this fixture; do not attribute its per-object captures here.

A13's generated configuration has reuse budget 4, ground radius 20 and reflected
probe mode false. Its generated ground capture calls renderTerrain and
renderRoadChunks. The authored map omits reflectionGroundProxy/reflectionScenery;
both default false in project.hpp, and no REFL_GROUND_GRID is generated.
The current main scene's reflected-object list contains seven model proxies
and twelve ordinary box objects; they are authored non-physics objects.
Do not present duplicate matrix updates as a demonstrated hot cost for them.

### 1. Reflection culling uses an intentionally oversized volume

`renderer_core_3d.cpp:83` creates a square projection with FOV 110 and aspect 1,
but lines 97-98 update the classification planes with FOV 154 (110 * 1.4).
`renderer_3d_frustum_planes.cpp::computeStaticData` also takes aspect from the
main display settings, not the reflection target. The source explicitly calls
this a conservative workaround for the square/main-aspect mismatch.

At the current non-widescreen aspect 512/448, the view-space half extents per
unit of forward depth are:

| Volume | Vertical | Horizontal |
| --- | ---: | ---: |
| Square 110-degree target | 1.428148 | 1.428148 |
| Existing 154-degree classification | 4.331476 | 4.950258 |

These are geometric widths, NOT work ratios. A simple host-arithmetic witness
with x in [19,21], y near zero and positive forward depth in [9,11] lies wholly
outside the target's right plane but wholly inside the old lateral planes.
Near/far are held unchanged. This proves an admitted off-target region, not
how many actual scene bags fall there or its hardware cost.

The mismatch affects generated terrain/road coarse rejects and Core main/package
checks. Core can then prepare object data, texture/state and qbuffers for those
bags. Guard-band routing may deliberately submit them and let GS scissor remove
their pixels. That route is useful for partially visible bags, but does not make
preparing a completely off-target bag necessary. Some bags are still rejected
later; do not equate every admitted bag with clipping or a DMA launch.

Priority: qualify target-matching conservative OUTSIDE rejection, initially as
an additional reject that leaves accepted-bag routing/guard-band logic intact.
This differs from A03/A04: those tightened geometry slice bounds against the
same existing planes; this addresses which view volume is used in the first
place. Do not merely change the 1.4 multiplier or assume FOV alone is sufficient.
Derive the target raster limits from actual projection/viewport conventions;
cover near/far, edge-touching/crossing primitives, negative w, display modes,
camera-feed callers, view restoration and transform-cache identity. Price only
after target output safety and positive actual-scene reject coverage are proven.

### 2. Consumer existence is not consumer visibility

`g_dynamicEnvUsers` increments on loading reflective model/material resources
(`game_templates.inc:6057`, plus the corresponding material/loader variants),
not when a visible reflection draw is accepted. The capture gate at 23232/23349
therefore runs for loaded users even if no current visible draw will sample the
result. The main visibility/material loop runs later. This is a real missing
demand gate, but not proof of wasted captures during a chase-camera drive where
the player's reflective vehicle is visible. A fix needs conservative visibility,
first-visible-frame refresh and secondary-view/retained-basis handling, not the
previous frame's visible count alone. Keep it secondary to finding actual
off-target work in the current driving workload.

### 3. Ground pass multiplication is active, but its alternative already exists

`templates.cpp::reflGroundCapture` calls the ordinary renderTerrain path when
reflectionGroundProxy is false. `game_templates.inc:27855` submits base plus
every present layer/AO/ground-shadow/emission companion with no reflection-only
pass policy. Each accepted companion independently enters Core. This is not
proof every possible companion exists in this map: A02b positively observed
terrain base and ground-shadow capture work, not every generic branch.

Correction to the prior proposal: a separate coarse reflection ground already
ships as the optional reflectionGroundProxy setting. It is NOT a new feature
to invent. See [the ground stand-in](reflective-materials.md#the-ground-stand-in-11610):
21x21 height-following cells, nine bags, host-baked albedo including roads.
Historical Motor District data reports about 2 ms frame savings outdoors and
little change indoors, with a darker/different reflected ground appearance.
Those are older measurements, not A13/TyraX2 pricing. It is disabled here.
Reusing it would be a separately labelled quality/performance comparison, not
an image-identical renderer fix or evidence of a missing implementation.

### Smaller findings and excluded false leads

- The reuse gate hashes the full eligible reflected list and computes nearest
  distance before checking motion drift. It also checks budget > 0 only at the
  end. Budget 0 makes this accounting unnecessary, but the current fixture uses
  4, so that branch does NOT explain today's slowdown. An early rejection of
  reuse when angular drift alone exceeds the budget could avoid key work, but
  stored content/basis/nearest state must remain valid. Not a millisecond claim.
- The reflected-object loop has no coarseObjectOutside call before submitting
  its proxy/parts, unlike the main loop. It can rebuild dirty geometry and build
  a first-use proxy before Core rejects it. Clean static proxies persist; it
  does NOT scan their complete source mesh on every capture. A conservative
  precheck must handle dirty bounds and moving-geometry programs safely.
- The shared target is VRAM-resident; it is not read back and reuploaded each
  frame. envMap.begin/end packet buffers are allocated in init and reset later,
  not allocated per capture. Their FINISH-looking wrappers record ordering in
  the active frame pipeline; C07/P06 already rule out treating these calls as
  a newly discovered unconditional blocking drain per reflection.
- Terrain generation happens in the later main residency update, not inside
  this capture. `renderRoadChunks` traverses existing owner -3 chunks and does
  not retessellate the authored road every frame. Existing base/companion passes
  are visible-image policy, not automatically deletable duplicate pixels.

Decision: the projection/classification mismatch is the strongest newly
identified source mechanism for a correctness-preserving experiment. No new
cache, blind grouping change, silent proxy enablement or production default
change. Resume a bounded implementation/test only as the next explicit step.

## Scope and Reproduction

Run from the repository root:

```sh
python tools/tyrax2-scene-ledger.py --output docs/tyrax2-scene-ledger-2026-10-08 --selftest
```

The tool verifies the frozen P06 input hashes against its archive manifest,
reconciles every recorded road bag with the corresponding frame's Core-call
count, and reconstructs 72 actual PS2 frames / 2,220 road records. Six negative
controls reject lost ownership, invalid views, wrong phases/windows, negative
geometry counts and missing bags. It does not run the console or modify maps.
Verified on Windows Python and WSL Python: both pass all six controls and
produce equal parsed JSON and normalized Markdown. Path keys use POSIX
separators so provenance is identical on both platforms.

Outputs: readable ledger and
machine-readable ledger.
The JSON preserves per-road/view counters, source hashes and later-cycle peers.
The whole-scene producer census is included separately and explicitly marked
historical: it predates integrated terrain refinement. Never sum those costs
with P06 or treat them as the current stationary/driving frame breakdown.

## What the Existing Frames Establish

1. **The two small clipped roads do not describe the whole workload.** At
   offset 1351, main-view roads account for 35 bag calls, 16,743 submitted
   vertices and 265 logical VU chunks, only 32 of them clip chunks. At 1504,
   the main view has 25 bags, 4,656 vertices and 82 logical chunks, 16 clipped.
   This is different visible content as the camera turns, not measured waste.
2. **Reflection can be dominated by tiny clip chunks despite little geometry.**
   At 1336 the reflection has three road bags, 222 bag-input vertices,
   474 submitted vertices and 34 chunks, 32 clipped. Roads 24 and 34 each
   turn a 66-vertex strip into 192 list vertices / 16 clip chunks. Whether
   those triangles really straddle clip planes is still unrecorded.
3. **The largest individual road calls in these windows are not clipped.**
   All three calls above 0.6 ms are direct-route new baked entries:

| Offset | Road ID | Input vertices | Clip chunks | Core elapsed ms | Same-angle next two cycles, ms |
|---|---|---|---|---|---|
| 1335 | 9 | 1074 | 0 | 1.473 | 0.052 / 0.053 |
| 1336 | 42 | 1200 | 0 | 1.960 | 0.076 / 0.077 |
| 1347 | 8 | 1074 | 0 | 2.057 | 0.147 / 0.101 |

The three events have `MissNew=1`, not content/bounds invalidation, eviction,
expiration or allocation refusal. Later peers replay existing streams. This
is correlation with first preparation, not a controlled price of baking.
The windows do not include every frame or every owner. Do not extrapolate
these three events into the entire drive's bottleneck.

**Important accounting distinction:** the cull chunk counter includes derived
counts for whole-bag replay. A replay can submit one EE REF for many logical
VU chunks. Thus 265 chunks does NOT mean 265 EE allocations, dispatch calls or
DMA launches. These frames each have one recorded native prefix flush.
Road-triggered snapshot bytes can include pending work from other owners.

## Source-Level Explanation and Remaining Doubts

`StaPipQBufferRenderer::bakeBlock` in
`vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp`
transcodes the ordinary DMA fragment into a pure VIF stream. It copies REF
payloads into `bakeScratch` one quadword at a time with `push_back`.
`endBakedBag` allocates an aligned destination, copies the scratch stream into
it and registers immutable storage. This preparation occurs during the draw,
not at road generation. Source establishes extra copying/allocation work;
the current records do not separate its cost from texture/program setup,
queue work, CPU stalls or thread preemption. Do not label the whole 2 ms
as `memcpy`, and do not respond by blindly enlarging the cache.

The normal road geometry is generated at scene load. Its active builder is
`game_vehicles.gen.cpp::buildRoads`, not the commented older copy in
`game_collision.gen.cpp`. It already reduces horizontal and coplanar lateral
spans. Dense chunks can still contain 1,200-1,800 vertices. An audit must
inspect actual positions/UVs and failed reduction reasons before alleging
that the grid is unnecessary or changing tolerances.

Shared reflection uses ordinary `renderTerrain`: accepted chunks can draw
base, painted layers, AO, ground shadows and emission. The source permits
this multiplication, but P06 has no terrain material/pass tags. Authored AO
being disabled does not make the ground-shadow pass disappear. Keep this as
a separate unresolved source of work, not an already measured multiplier.

## A02 Bounded Capture Contract

### A02b: scene lineage (completed capture, 2026-10-09)

User-approved next step prioritizes steady movement rather than promoting a
cold-build fix. Nine named frames cover stationary, early/repeated turning,
and two driving locations. Six unchanged-drawing phases provide observer
off/on/off controls for turn and drive. Capture all StaPip bag counters with
owner, view, pass, phase-local source identity, actual cull/clip calls and
logical replay counts; copy actual clip input triangles and eight local clip
planes to fixed RAM, exporting only after each phase. Overflow invalidates
the run. This does not cover DynPip or establish exact raster visibility.
The private VU paint baseline is held constant, as in P06/A02a. No cache or
clip capacity policy is changed.

Fixture: `F:/Projects/tyrax2-scene-capture-20261009-v4`.
ELF: `98cc8ea98d4dbca424a334af3083ce589fc31cfaa6e3585eb09b4b9ce458059e`.
Native mirror/source binding: 467 files. Host recorder tests pass ASan/UBSan,
off gates, owner/pass/view scopes, frame accounting, exact float bits, missing
plane/oversize/capacity rejection and no recorder clock reads. The analyzer
rejects missing ownership, bad views, lost clip calls and missing geometry;
its independent synthetic plane tests cover inside/outside/crossing triangles.

Emulator: six valid phases, 1,027 bag records and 757 clip-input records.
V4's screenshot monitor started late and captured only phase 5, visually
inspected. The V3 prototype has all six screenshots (turn and drive), but those
are separate-build smoke evidence, not V4 pixel equivalence. No rendering
policy was changed, and no raster-equivalence oracle is claimed.

PS2: one editor-protocol launch, six valid phases, **1,027 bag records and 892
actual clip-input records**, zero overflow/accounting errors and no unknown
owners in the nine frames. All 303 staged input hashes remained unchanged.
The user did not need another manual reset. The owned file server was stopped
after completed export; PS2 was left at the completed test, not powered off or
reset. The helper follows `Runner::resetPs2Link/deployToPs2`: owned-server
release, reset witness, one reset, channel cleanup, exact marker, 10-second
settle, execee from bin, and keep-live on a silent 15-second timeout. This is
one successful launch, not proof that historical IOP wedges are fixed.

| Nine-frame PS2 group | Actual clip calls | Tight box safe in 8 planes | Tight box outside one of 6 VU planes |
|---|---:|---:|---:|
| Roads | 358 | 232 | 79 |
| Sky | 221 | 74 | 58 |
| Terrain base | 90 | 0 | 0 |
| Terrain ground-shadow companions | 85 | 21 | 18 |
| Other shadow geometry | 82 | 15 | 14 |
| Static batches | 28 | 5 | 8 |
| Object base/proxy | 16 | 0 | 0 |
| Effects | 12 | 4 | 6 |
| **Total** | **892** | **351** | **183** |

Thus 534/892 calls in THESE SELECTED FRAMES have a conservative tight-box
alternative. This is not a population estimate or a 60% performance gain.
The road portion overlaps C18's already measured strip-refinement opportunity;
do not present it as a new road fix. **223/534 opportunities are outside roads**,
including 132 sky calls. Already-refined terrain base has zero tight-box wins,
which keeps the result distinct from merely repeating terrain refinement.
The 3,609 input triangles classify as 1,855 safe, 1,092 definitely outside and
662 boundary/uncertain; 203 exact degenerates overlap these categories.
Triangle classification is not an exact visible-pixel test.

The warm repeated turn frame at phase 1 / offset 1576 has zero `MissNew`:
136 bags and 172 actual clip calls. Shared reflection contributes 44 sky,
32 road, 13 terrain-base and 8 terrain-ground-shadow clip calls. The main
view adds 8 sky, 32 road and other calls. The sky's source has only 1,152
vertices, yet its reflection alone dispatches 11 cull plus 44 clip buffers;
21 of those 44 clips have a tight-box alternative (11 safe, 10 outside).
Ordinary vehicle base/environment/wheel bags in that frame replay with zero
actual cull/clip calls, despite 251 logical cull chunks. These counters cannot
be read as 251 EE allocations or as proof that vehicles cost nothing.

Driving includes an actual capture at phase 4 / offset 1501: 125 clip calls,
including 36 reflection-sky calls; 21 of those have a tight-box alternative.
Emulator driving offsets did not capture reflections; the physical run did.
Compare source geometry/view state, not equal offsets across platforms.

Observer off/on/off moving-window work means on PS2:

| Workload | Before ms | Observed ms | After ms | Loop Hz before/on/after |
|---|---:|---:|---:|---|
| Turn | 16.157482 | 16.534630 | 16.193033 | 48.058760 / 47.372858 / 48.058655 |
| Drive | 14.370108 | 14.520062 | 14.458851 | 50.415451 / 48.623104 / 49.968596 |

Turn observer displacement is 0.377148/0.341597 ms and includes changed
capture cadence (174/179/174 captures). Driving uses real dt, so arms reach
the same endpoint in different frame counts (450/434/446 moving rows,
78/77/77 captures); the three averages are NOT a paired optimization price.
Common compiled hooks, RAM and code layout remain unpriced. Captured-frame
timings include recording overhead; do not subtract these from other ELFs.

**Decision:** test tight bounds for actual list clip slices, starting with
the identified sky path, preserving every triangle, stream attribute and
VU capacity. This targets the granularity mismatch described below, not
another cache. Require an oracle for conservative inside/outside decisions,
unchanged primitive order and near/far rules, visual/depth checks and same-ELF
A/B/A hardware pricing before broader rollout. Keep coplanar shadow routing
paired; do not turn the table into a blanket global toggle.

Remaining coverage: DynPip/non-StaPip, rebuild/allocation/texture-upload events,
complete command/reference-byte ownership, per-caster IDs for aggregate
shadow/effect producers and broader camera/night/streaming states. Transport
snapshot bytes attributed to a triggering bag may contain mixed owners.
A02b does not establish an EE-only bottleneck or implement a production fix.

Evidence: frozen captures, sources, ELF and tools.
All 1,046 archived files pass the SHA-256 manifest check; both archived
source trees independently match their 467-file native bindings and ELF hashes.
V1 generation failed on a commented duplicate method; V2 was rejected by the
host accounting test before any target run. V3 passed emulator accounting but
left five shadow bags unlabelled in turn frames; it is archived separately as
superseded. Only V4 was deployed to PS2. Both original authored maps are unchanged.

### A02a: first-build events

The first bounded slice extends the unchanged P06 road fixture, not the old
rejected prepared/pinned static-batch policy. Its question is whether the
observed direct-route first-use spikes occur in VIF payload transcoding/final
allocation or elsewhere in per-bag preparation. Record existing prefix timing
deltas and two additional build/finalize scopes, plus copied payload/header
qwords, scratch capacity changes and final arena bytes, only within P06's
72 road-frame windows. Export after the phase. Drawing and cache policy stay
unchanged. This is diagnostic timing with observer controls, not a fix price.
Decide on a first-build intervention only after locating the work; retain the
broader pass/view/plane audit below as unfinished.

Private tools:

```sh
python tools/tyrax2-first-build.py <new-fixture> <authored-project>
python tools/verify-first-build.py <fixture> # WSL g++, ASan/UBSan
python tools/tyrax2-first-build-analyze.py <fixture> <csv-directory> <target-log>
```

Fixture: `F:/Projects/tyrax2-first-build-day-20261008-v1`.
ELF: `81c82f95779f86ac08a8dd178f0386c0c027df5a517fe2e18452cbef1f4572d1`.
Native binding verified 467 source files. Host checks pass off/out-of-window
gates, timing deltas, disjoint scopes, wrap and all hook sites, in addition to
the existing P06 identity/overflow/wait checks. Emulator completed all three
phases and 2,220 records; the analyzer rejects four additional corruptions.
All three in-route screenshots were captured, phase 1 visually inspected.
Emulator numbers are not hardware performance estimates.

Its observed new direct bodies are 53,232 / 59,392 / 53,232 bytes for road IDs
9 / 42 / 8, plus 2,560 bytes for ID 60. Each is copied into scratch and then
into its final arena. None grows scratch capacity in the recorded windows.
`growthBlocks` counts blocks whose capacity changed, not allocation calls;
`arenaBytes` excludes alignment padding, allocator overhead and other caches.
The bounds prefix also increases on first use. Source inspection finds that
`getBBoxes` creates fine, main and coarse bounds before the direct-route test;
the current prefix timer does not isolate individual constructors.

The previous prepared-static experiment is NOT this intervention: it prepared
ordinary static batches (28 bodies), not these road first-use events, and
failed to reduce measured runtime misses. Its negative result remains valid;
do not re-enable global scene pinning based on A02a.

#### Physical result: completed

The first network-reset boot failed during asset loading with repeating
`freepad: DMA Busy`, before any measured phase. It is preserved as rejected
startup evidence, not a renderer failure. After the user's physical restart,
bare `execee` ran the same ELF through all three phases with `valid=1`.
All 303 staged inputs remained unchanged. The client and owned emulator/Xvfb
processes are stopped. The console was not shut down or reset after this run.

Both target parsers accept 2,520 frame rows and 2,220 bag records, reject the
base 16 controls, six P06 trace corruptions and four A02a corruptions. The
first-build payload/header/final arena sizes match on emulator and PS2.
Evidence, exact ELF, generated source, logs, screenshots and tools are frozen
in the A02a archive; all 529 archived files
were checked against its SHA-256 manifest.

| Road / offset | Core elapsed ms | Bake ms | Finalize ms | Bounds prefix ms | Texture prefix ms |
|---|---:|---:|---:|---:|---:|
| 9 / 1335 | 1.513489 | 0.451199 | 0.427331 | 0.433848 | 0.006704 |
| 42 / 1336 | 1.966566 | 0.489149 | 0.417124 | 0.383911 | 0.024865 |
| 8 / 1347 | 2.037062 | 0.459676 | 0.449832 | 0.377553 | 0.007145 |
| 60 / 1351 | 0.300971 | 0.021542 | 0.041358 | 0.092743 | 0.006900 |

These are per-call elapsed scopes. Bake and finalize are disjoint from each
other, but contained within Core and existing dispatch buckets; never add
them to Core. Finalize includes reservation, allocation, copy and immutable
registration, not just memcpy. Bounds includes more than bbox construction.
Road-triggered queue/snapshot work may still mix pending owners.

Whole-turn observer off/on/off: **15.854457 / 16.063458 / 15.886539 ms**;
loop rates **48.166137 / 47.634400 / 48.166031 Hz**. The observer arm adds
0.209001 / 0.176920 ms relative to its controls, including changed reflection
cadence (174 / 179 / 174 captures). Common compiled hooks, RAM footprint and
layout remain unpriced. Do not subtract these numbers from another ELF or
advertise the event scopes as a measured optimization gain.

**Decision:** first-use preparation is a real, localized hitch mechanism:
the three large events spend 0.879-0.910 ms constructing/finalizing streams
and another 0.378-0.434 ms in the bounds prefix. Scratch growth and texture
selection are not the dominant explanations for THESE events. This does not
close steady-state motion cost or establish EE-only attribution.

A subsequent intervention should prepare the identified immutable road bodies
and required bounds at an explicit loading/residency boundary, with readiness
and actual-consumption witnesses, without a draw or global cache pinning.
Keep a budget/fallback and qualify key changes, scene revisit and streaming;
the existing prepared-static API neither prepares bbox data nor demonstrates
that these road entries are consumed later. Do not enable it globally.
Before claiming a stable-60 fix, complete the broader pass/view/plane ledger.
No production behavior was changed by A02a.

The broader contract below is only partially covered by A02a/A02b. A02b records
StaPip lineage and clip inputs; rebuild/allocation/texture-upload events and
non-StaPip work are still missing. It must fill the missing
lineage, not add another broad timing matrix. Use the current private baseline,
unchanged original map and drawing, with stationary, turn and full-throttle
snapshots clearly distinguished. Do not reuse road-only parked data as driving
evidence.

- Tag every StaPip bag with producer, stable object/chunk identity, main/shared
  reflection/secondary view, and explicit pass: base/layer/AO/ground-shadow/
  emission/environment/other. Preserve tags across `submitHeavy`/`dripHeavy`.
  Mark DynPip and non-StaPip work out of scope explicitly.
- Record bag/package input counts and topology, actual route and rejecting or
  intersecting plane mask. For selected partial packages capture source
  positions and the exact object-space planes for offline triangle tests.
  Distinguish all-inside, trivially outside one plane, and unresolved
  straddlers. A straddler is not proof of raster visibility; use full polygon
  clipping when exact intersection is required. Preserve guard-band policy.
- Separate logical VU chunks from EE emitted chunks and whole-bag replay.
  Attribute referenced bytes, copied bytes and command bytes separately;
  transport flushes retain their possibly mixed ownership.
- Record first-build events with payload size, scratch size/capacity growth,
  final arena allocation size and queue-copy activity. Also capture terrain
  rebuild, texture acquisition/upload and capture-start events. Absence of a
  hook must be reported as unknown, never zero.
- Use fixed-capacity RAM records for a few named frames and dump after the
  measured section. No per-bag text I/O or heap allocation by the recorder.
  Overflow invalidates completeness rather than silently truncating. Validate
  owner/pass reconciliation and identical rendering before considering timing.

Decision gates:

1. Excess clip routing: intervene only if geometry/plane evidence identifies
   avoidable work beyond the already tested terrain refinement and C18 road
   subranges. Do not enlarge worst-case VU clip capacity.
2. First-build work: test whether preparation can be moved to a bounded loading
   or residency phase without drawing early or breaking storage lifetimes.
   Test new visibility, scene revisit and streaming; a warm-turn-only win is
   insufficient. Price the identified operation, not an arbitrary cache knob.
3. Pass/view multiplication: compare actual companions and accepted geometry;
   change reflection representation only with a visible-quality contract.
4. Dense geometry: require geometry/UV equivalence or an explicit error budget
   before altering tessellation. Keep rendering geometry separate from road
   contact and light receivers.

No production renderer change is claimed by the audit. Full current-scene
lineage, including event and non-StaPip coverage, remains open.

### Source Finding: List Bounds Do Not Match Clip Slices

`StaPipBagPackagesBBox` builds fine bounds at `maxVertCount / 3`.
`StaPipCore::clipPackageSize()` uses `maxVertCount / 6`, rounded down to a
multiple of three, when VU clipping is enabled. The packager's split overload
does not build the smaller slice's bounds: `checkFrustum` merges every old
one-third box it overlaps. A small input therefore inherits vertices outside
its actual range, sometimes from two old boxes. This is conservative, not a
missing-triangle bug, but can unnecessarily select the expensive clip route.

This differs from C18's opt-in strip refinement: ordinary list geometry,
including the sky and shadow companions, still uses this older granularity.
The A02b analyzer constructs a tight box from each captured actual clip input
and tests its eight corners against the captured eight planes. It separately
reports safe boxes and boxes outside a common one of the first six VU planes.
These are geometric opportunities, NOT milliseconds saved or permission to
enlarge clip buffers. Coplanar companion routes, exact near/far safety, masked
VU clipping, primitive order and depth rounding remain qualification gates.

## A03 Sky Tight-Bounds Trial: Not Promoted

The user approved this follow-up on 2026-10-09. The private generator
`tools/tyrax2-sky-bounds.py <fixture> <original-project>` selects only the
sky dome's ordinary small triangle-list slices that the existing classifier
would clip. It rebuilds each selected input's exact box, then uses a scaled
positive margin: all eight planes must prove inside for the cull route;
one common plane among the first six must prove outside to reject. Uncertain
cases preserve the old route. Geometry, texture coordinates, colours, VU
capacity, roads, shadows and reflection policy remain unchanged. The ordinary
cull-first dispatch grouping can change order as routes change; no ordering
or raster-equivalence claim follows from the geometric proof alone.

Six phases hold the private VU paint baseline constant and compare off/on/off
for both the established turn and full-throttle drive. Sparse equal-schema
counters record old clip eligibility, safe/outside alternatives, applied
decisions and actual sky cull/clip calls. A cold independent per-vertex double
precision witness runs before timing in every arm; no nested clocks are added.
Host ASan/UBSan replay of 221 captured PS2 sky inputs reproduces 74 safe,
58 outside and 89 unchanged decisions, plus 50,000 random safety cases and
boundary/nonfinite/EE-only-plane controls. Native V2 source binding is 468 files.
The V1 generator stopped on an obsolete instrumentation anchor, before building.
The same V2 ELF completed all six emulator and physical phases. Each phase's
cold witness reports 1,300 checks, zero failures, 500 safe and 400 outside.
The analyzer rejects deliberately unapplied decisions, lost clips, wrong
vehicle pose and a failed oracle. Every row reconciles old clip eligibility
with actual clip calls plus applied decisions.

Physical PS2 moving-window results (work excludes explicit pacing):

| Phase | Mode | Frames | Mean work ms | P95 ms | Loop Hz | Sky clip calls |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Turn 0 | Off | 720 | 15.107914 | 18.924733 | 48.166 | 16,734 |
| Turn 1 | On | 720 | 15.349806 | 19.322283 | 47.952 | 6,126 |
| Turn 2 | Off | 720 | 15.123478 | 18.887678 | 48.166 | 16,734 |
| Drive 3 | Off | 489 | 13.365840 | 17.600752 | 54.786 | 8,664 |
| Drive 4 | On | 479 | 13.545651 | 18.087175 | 53.665 | 2,806 |
| Drive 5 | Off | 478 | 13.473303 | 17.789642 | 53.554 | 8,582 |

Turn classification saves 10,854 of the candidate's 16,980 eligible calls:
7,009 safe and 3,845 outside. Actual clips fall about 63% versus the controls,
but mean work regresses 0.226-0.242 ms. Reflection captures are 174/179/174,
so this prices the complete policy and its scheduling consequences, not a
pure bounds-scan cost. Drive uses real dt and a fixed endpoint, not identical
per-frame poses: its unequal frame counts/capture cadence are workload
outcomes, not a paired operation timer. It shows no demonstrated gain.
Do not compare these absolute times with A02b's heavier recorder as a fix price.

Six emulator stationary screenshots passed capture-window checks. The
sky/building ROI is pixel-identical for all four A/B and A/A comparisons;
world/paint differences are no greater than off/off temporal differences.
This is a stationary smoke test, NOT moving-frame or depth equivalence.
No claim of unchanged dispatch order or fully qualified raster output is made.

Decision: reject promotion of this on-the-fly implementation. The geometric
overclassification is real, but fewer clips alone are insufficient. A bounded
follow-up may prepare exact clip-slice bounds with geometry and reuse them
across views. Keep the actual parent-relative slice boundaries, source version
and lifetime, eight-plane margin, fallback and oracle; do not substitute a
global cache/TTL experiment. Per-frame box scanning is a plausible cost, not
an isolated measured cause of the regression. A04 below implemented that
follow-up and also rejected promotion.

Evidence: `tyrax2-sky-bounds-2026-10-09/`, including source binding, raw CSV,
logs, host tests, emulator pictures, pixel report and tool snapshots.
All 511 archived files match the SHA256 manifest; all 468 bound source files
and the ELF independently match the native binding.
ELF SHA256: `43b51378e4f0cd77ca8d3fb65b5afd2d213aeffa69c03e6fc7cb94227bc864eb`.
The first editor-protocol deployment completed; all 303 launch inputs and both
original authored maps stayed unchanged. Owned emulator and file-server
processes are stopped. The console remains at the completed test.

## A04 Precomputed Sky Bounds: Not Promoted

The overnight user authorization permits autonomous, justified follow-ups.
A04 changes only where A03's exact boxes are constructed: buildSkyDome uses
the actual pipeline-derived package maximum and builds parent-relative clip
slice boxes after assigning the geometry version. A private single-sky store
retains positions' min/max, not view decisions, commands or live DMA inputs.
Lookup checks source pointer, count, version, maximum, exact offset and length;
unknown shapes retain the original clip route. Rebuild replaces the metadata.
All arms prepare metadata, so measured A/B isolates consumption, not startup
construction. Preparation and memory are reported separately by SKYPREP.

Cold target checks compare each prepared decision against A03's actual-vertex
classifier and independent vertex witness. Sparse hit/miss counts require
complete candidate coverage. Host tests cover 191,828 ranges over six package
sizes, small direct bags, partial parents, tails, pointer/version/shape rejection,
same-buffer rebuild and nonfinite input, with ASan/UBSan. V1 stopped before
launch on chained Vec4 assignment (which returns void); V2 uses separate copies.
No production promotion is accepted.

A04 V2 fixture: `F:/Projects/tyrax2-sky-prepared-20261009-v2`.
Native source binding covers 470 files; ELF SHA256 is
`ea016aadaedd91359362f8d8d0db33c79dd2682e23a146ec476da461d7337f99`.
Host tests and six emulator phases pass, with 1,300 cold checks per phase,
zero prepared misses and full candidate eligibility coverage. The final sky
has 108 prepared boxes / 5,184 live payload bytes, rebuilt at phase offset 4;
the initial 504-vertex boot sky has 32 boxes before the authored scene loads.
These are live sizes, not vector-capacity or allocator-overhead measurements.
All four stationary sky/building ROI comparisons have zero differing pixels.
Other regions retain temporal differences (drive A/B exceeds A/A in the world
ROI), so this is NOT full image equivalence. Phase 1 was visually inspected.
The owned emulator was stopped after export. Physical deployment completed all
six phases on the first editor-protocol start, with the same cold checks,
zero misses and complete prepared-hit coverage. The parser also rejects a
missing prepared hit, a metadata miss and preparation inside the timed window.

| Phase | Mode | Moving frames | Mean work ms | P95 ms | Loop Hz |
| --- | --- | ---: | ---: | ---: | ---: |
| Turn 0 | Off | 720 | 15.298425 | 19.165894 | 48.166 |
| Turn 1 | On | 720 | 15.540215 | 19.506721 | 47.846 |
| Turn 2 | Off | 720 | 15.345551 | 19.143934 | 48.166 |
| Drive 3 | Off | 487 | 13.416474 | 17.584656 | 54.561 |
| Drive 4 | On | 484 | 13.523784 | 17.822991 | 54.225 |
| Drive 5 | Off | 477 | 13.507364 | 17.463189 | 53.442 |

Turn clips remain 16,734 / 6,126 / 16,734, exactly A03's outcome; the candidate
still regresses work by 0.195-0.242 ms. Driving again shows no demonstrated
gain and retains real-dt, endpoint and unequal-pose caveats. A04 does not prove
an individual lookup/scan cost: code/data layout and apparatus differ from
A03. It does refute promoting the current policy merely by moving box creation
to geometry construction. Close sky refinement for now; do not tune its cache.

Evidence: `tyrax2-sky-prepared-2026-10-09/`. All 303 physical launch inputs and
both original maps retained their hashes. The owned file server was stopped
after export; the console is at the completed test. Next direction: audit
actual longitudinal road positions/UVs before any geometry reduction.
All 524 archived files match their SHA256 manifest; 470 source files and the
ELF also match the native source binding.

## A05 Longitudinal Road Geometry Audit (Completed Capture)

The active road emitter reduces each station pair laterally, then appends two
vertices for EVERY collapsed station along the road. It does not prove whether
intermediate stations on a straight span preserve the same surface and affine
UV field. This is distinct from prior road group/cache/clip refinement trials.
First capture all actual road XYZ/STQ/colour streams and original chunk/run
identities from the unchanged scene, outside measured frames. Analyze offline:
decode strip runs and degenerates, retain boundaries, and measure plane and
attribute interpolation errors before proposing any reduced draw stream.
Do not alter the authored sampleStep, road-contact queries, light receivers,
chunk bounds or texture rebasing. Junction/edge/blended geometry needs explicit
qualification and is excluded from the initial longitudinal candidate.

Fixture: `F:/Projects/tyrax2-road-geometry-20261009-v1`; 468 native-bound files.
ELF SHA256: `2e9f39dee14e7fd4a3b56b0516967168052e7870e843440c99a40e0a83d32df5`.
One unchanged-drawing phase completes on PCSX2 and PS2. ROADGEOMETRY at offset
1050 writes all 66 chunks / 26,835 vertices; the capture is not a timing price.
The decoder verifies the RDA1 payload, scene/phase/18-pin witnesses, lengths,
finite fields and complete consumption. Twelve synthetic controls cover affine
and nonplanar spans, UV/Q/colour errors, padding, blend/edge exclusions, triangle
intersection identity, altered surfaces and missing UV coverage.

The first whole-chunk test accepts only emulator chunk 23. Piecewise reduction
without relaxing the 1e-5 tolerance accepts four chunks on both targets:

| Zero-based chunk | Diagnostic road ID | Original | PCSX2 reduced | PS2 reduced | PS2 max surface error |
| --- | --- | ---: | ---: | ---: | ---: |
| 23 | 24 | 66 | 6 | 12 | 9.594276e-6 |
| 24 | 25 | 66 | 9 | 12 | 9.687022e-6 |
| 25 | 26 | 60 | 12 | 18 | 8.605561e-6 |
| 26 | 27 | 42 | 12 | 24 | 9.908885e-6 |

These streams have monotone paired UV rails, constant colour/Q and one strip
run. Retained stations preserve the domain and strip parity; repeated end
vertices pad to a multiple of three. An independent double-precision UV
triangle-intersection oracle compares piecewise-linear positions at every
intersection polygon vertex and reconciles covered area. Convexity bounds the
surface error over each intersection, not merely over original mesh vertices.
This is a world-space geometry/UV proof, NOT exact GS depth or pixel identity.

Important: the two targets' source XYZUV floats differ, so copying emulator
selection indices blindly is invalid. The physical budget is 234 -> 66,
only 168 fewer vertices out of 26,835 overall. Most short strips are genuinely
non-affine; 38 chunks are not single-run strips. This opportunity includes the
previous hot ID24, but NOT hot ID34. No FPS gain or production fix is claimed.

Next A06 may use separately owned reduced DRAW streams while retaining original
road collision/light-receiver data, chunk identity and bounding boxes. Preserve
BagArray immutable ownership through deferred submission and validate actual
target inputs. Price a bounded same-ELF candidate; do not turn this result into
blanket simplification or loosen the tolerance to inflate eligible counts.

Evidence: `tyrax2-road-geometry-2026-10-09/`, 504 SHA256-verified archived files,
including both raw geometry payloads and analyses. Native source/ELF binding
also verifies independently. All 303 launch inputs stayed unchanged. The first
editor-protocol launch completed; owned host/emulator processes are stopped.

## A06 Draw-Only Road Reduction (Emulator Complete, PS2 Interrupted)

The private generator unions A05's retained stations from both platforms and
rechecks their UV-domain intersection surfaces: chunks 23..26 become
12/15/21/24 vertices (234 -> 72 total). It does not copy positions from the
host: it copies the selected indices from the actual runtime original arrays.
Every original XYZ/W, STQ/W and RGBA bit must match one complete captured
platform input before use. Unknown geometry fails the private trial, not a
silent approximate fallback. This fixture-specific matcher is not a proposed
production content-addressing system.

Each ProcChunk owns separate BagArray position/UV/colour streams, prepared in
all arms. Only candidate phases bind them to the existing bag; source contact
and light-receiver arrays, culling AABBs/centres and chunk identities remain.
Binding is persistent for the phase, not temporarily swapped around deferred
submitHeavy. The source version is rebuilt with the ordinary chunk lifecycle.
Sparse Core and qbuffer counters track selected calls, source/draw vertex counts
and actual cull/clip calls. Sky refinement is OFF and VU paint remains constant.

Host ASan/UBSan tests exercise all eight captured input sets, every position,
UV and alpha mutation, null/count rejection, plan indices and unknown IDs.
The source audit confirms road contact/index consumers read c.vertices rather
than the reduced bag. Same-ELF six-phase emulator validation passes;
world-surface error proof is still not exact GS depth or moving pixel identity.

Current A06 fixture: `F:/Projects/tyrax2-road-reduction-20261009-v1`.
469 native-bound source files, ELF SHA256
`f9993a824b9a46bbd2bbb8a0bc461bacec5664e5e60d1b53bbdefa8965a92779`.
Host matcher checks pass ASan/UBSan. The hidden emulator has passed all six
phases with exact matches for all four chunk payloads and four analyzer negative
controls. Physical accounting/pricing is incomplete. The arrays use the normal
BagArray pointer-plus-content-stamp binding; this wrapper is not a DMA lease
manager. They stay unmodified through a phase and follow ordinary scene-drain
lifetime rules. The private struct adds storage members to all ProcChunks in
both arms; cross-ELF absolute timing is not an isolated reduction price.

Qualification warning from the Core source audit: batch road bags may receive
the selected dynamic light even with no lighting-normal bag. Surface/UV proof
does not cover the colour program's nonlinear per-vertex light or fog output.
Fewer surviving vertices can change their interpolated appearance, especially
at night. This trial only prices the private draw representation; promotion
requires those attributes and raster/depth agreement or an appropriate fallback.
Disabling lights/fog to make the candidate appear equivalent is not acceptable.

| Emulator phase | Work mean ms | p95 ms | Max ms | Selected clip calls |
| --- | ---: | ---: | ---: | ---: |
| Turn off | 7.502821 | 10.411058 | 11.993113 | 14304 |
| Turn on | 7.317418 | 10.143762 | 10.692315 | 2697 |
| Turn off | 7.509820 | 10.446008 | 11.957248 | 14304 |
| Drive off | 7.016263 | 10.105798 | 12.841356 | 9044 |
| Drive on | 6.888615 | 10.062653 | 11.590536 | 2502 |
| Drive off | 7.081400 | 10.197700 | 12.618893 | 9054 |

All emulator arms run near 59.94 Hz, not proof of physical performance. The
selected Core counts include wholly outside bags; actual qbuffer clip counts
above are separate witnesses. Stationary captures exist for all six phases.
Candidate turn screenshot was inspected: scene, roads and three cars render,
but this is smoke only. World-region mean absolute RGB difference is 0.119
for turn off/on versus 0.058 for off/off; drive is 0.117 versus 0.062.
The candidate also changes 159 sky/building-region pixels in both comparisons,
versus zero in the off/off controls. These differences remain unexplained;
do not claim raster equivalence. Owned PCSX2/Xvfb processes were stopped.

Physical run: first editor-protocol launch acknowledged and completed phase 0
and phase 1, 840 rows each. During the following scene load it began repeating
`freepad: DMA Busy ID = 3c4d0002 ret = 1`, `SB_STAT = 00f00160`.
No third-phase construction witness or DRIVEDONE followed. The analyzer fails
on the missing completion marker, as required. Preserve the partial 1680-row
CSV, not a manufactured complete A/B/A. No further reset or execee was sent.
Owned server PID 13224 is retained; launch metadata is authoritative if PIDs
change. All 303 launch inputs and both authored map hashes remain unchanged.
This recurrence after two completed phases is not evidence that the deployment
helper differed from the editor or that the road reduction caused the IOP stall.

Decision: no A06 promotion, no accepted physical price. While hardware cannot
complete tests, focus offline on C16 paint correctness gates rather than starting
more FPS-only road experiments. Evidence snapshot: `tyrax2-road-reduction-2026-10-09/`.
All 516 archived files verify against the manifest; all 469 native-bound source
files and the ELF were independently checked before export. Physical completion
is explicitly false in the manifest. The live log may advance after this snapshot.
