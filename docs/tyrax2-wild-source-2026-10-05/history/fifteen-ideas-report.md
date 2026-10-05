# Fifteen lateral PS2 rendering experiments for TyraX2

Date: 2026-10-05. Independent source review; no repository edits, builds, emulator use, console access, or speculative plans/backlog inspection. This report is a lab artifact outside the repository. Exactly fifteen candidate ideas follow, in confidence order. Confidence means confidence that the mechanism removes useful work with manageable correctness risk; it does not mean a measured speed gain.

## Evidence and interpretation

Read the implementation and the engine/editor skill instructions, especially rendering pitfalls and performance context. Primary measurements used: `docs/tyrax2-night-extras-2026-10-05.json` and `docs/tyrax2-pool-cache-2026-10-05.json`. Full night sampled inclusive elapsed-minus-pacing intervals are approximately 18.5–19 ms and flip-return periods approximately 33.367 ms. Own-source complete removals save approximately 0.875–0.968 ms for pools, 0.681–0.782 ms for beams/coronas, and 0.296–0.530 ms for vehicle/lamp glow. These are whole-loop contrasts, not exclusive processor costs or additive budgets. None individually reaches 60 fps. Color-only pool cache activation/correctness was established but its timing changes sign or remains within relevant control variation; stable gain and promotion were rejected. The parent's evidence summary additionally records unsuccessful SPR/CALL experiments and an earlier quality-reducing combined removal reaching 59.94 fps. Those are context, not independent measurements made here.

All savings below are **inferred work reductions**, never measured milliseconds. Source byte counts are logical payload counts before clipping, baking, padding, DMA routing or cache-line effects; they are not asserted physical bus savings. Reaching the next presentation deadline requires enough whole-frame reduction under real scheduling. No candidate guarantees 60 fps, and reductions must not be added to the removal contrasts.

## First three minimal experiments

Rank 1 needs a lighting-inactive admission proof: ordinary TC bags may inherit spot/dynamic lighting that billboard T cannot reproduce. If that proof is unavailable, begin the small scalar quadratic rejection experiment from rank 3. Otherwise replace scene-corona storage with the already implemented billboard descriptor, preserving existing visibility, pull and tint arithmetic. Admit only coronas whose four corners are safely within the existing billboard guard/depth region; retain the precise legacy path for boundary cases. Compare actual GS raster pairs, center/input counts and own-source nonpacing timing in both arm orders.

Rank 2 is a larger microprogram experiment: retain one pool's geometry/STQ and replace its repeated colors with one palette/table record, selected by a vertex or run ID. First prove one pool against the legacy integer GS colors, then extend only if a changed brightness actually activates the new path.

Rank 3 can be prototyped without modifying VU1: use a four-candidate SoA cone classifier with squared rejection inequalities, keeping scalar insertion ordering and boundary fallbacks. First compare the exact selected caster IDs and receiver IDs across a recorded state sweep; only then price the kernel end to end.

## 1. Corona descriptors through the existing VU1 billboard expander — high confidence

**Location:** `src/game_templates.inc:17568` (`updateAndRenderLightBeams`), `src/templates.cpp:13935` (`renderVehicleLampGlow`); `vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/bag/stapip_billboard_bag.hpp` and `.../programs/billboard/stapip_billboard_t_vu1.vclpp`.

**Mechanism:** Represent a halo as `(center, m00,m01,m10,m11, RGBA)` rather than six position/ST/color records. Existing VU1 code computes `corner = MVP*center ± MVP*right*hw ± MVP*up*hh`, then emits the original six GS vertices. This is a change of representation, not another batching layer. Main-view and portal basis records stay separate.

**Work removed:** Per halo, logical input falls from 18 qwords/288 bytes to 3 qwords/48 bytes. Removes EE corner construction, duplicate positions, fixed ST array assembly and six repeated colors. Existing GS triangle count remains six; VU1 expansion adds work, so speed remains uncertain.

**Risks:** Existing billboard program rejects the whole quad when any corner is outside its guard/depth range, whereas precise clipping may retain a visible portion. The ordinary TC corona bag may inherit spotLit/dynLightPick, while the billboard program has no lighting. Require a proven lighting-inactive whole call or keep legacy; this can reduce admissions to zero. Camera basis signs, fog policy, pulls and per-view DMA lifetimes must match. Descriptor bounds must include full extents, not centers alone.

**Reversible experiment:** Scene coronas only, same-ELF switch, legacy fallback at boundaries, unchanged CPU oracle. Verify near-plane, horizon, portal and fixture-occlusion raster pairs. Count admitted/fallback halos outside the timed window.

## 2. Pool colors as a coefficient table rather than vertex data — high-to-medium confidence

**Location:** `src/game_templates.inc:13950` (`poolBatchFlush`), pool batching structures in `src/templates.cpp:2775`; cull/clip TC VU1 color load and FixColor paths.

**Mechanism:** A pool member has one color `c = lightRGB*FIX/128`. Upload that coefficient once per member, with a compact member ID or vertex-run boundary. VU1 loads/broadcasts `c` for the member's vertices. Camera/geometry remain normal. Brightness changes update a small table; geometry and STQ do not need re-encoding. This eliminates a redundant dimension from the data, beyond the failed EE color-only cache.

**Work removed:** A 4×4 pool list has 96 equal color qwords (1536 logical bytes); one qword coefficient plus ID overhead can replace them. Removes 96 EE color pushes per rebuilt member and color payload restaging. The actual copied/baked path and table overhead need measurement.

**Risks:** Match float-to-GS conversion order exactly; multiplying pre-rounded colors is not equivalent. VU memory/table size, clip-generated colors, changed member ordering and deferred DMA ownership must be handled. VIF/VU microprogram space is constrained.

**Reversible experiment:** One non-carving steady pool, then flicker it with the same coefficients in both arms. Preserve 96 geometry vertices and precise clipping. Compare emitted integer RGBA and exact raster pairs before timing.

## 3. Cone selection as a four-lane quadratic classifier — medium-high confidence

**Location:** `src/game_templates.inc:14047` (`pickVolCasters`), receiver selection around 14400 and 15500; existing VU0 vector math in `vendor/tyra/engine/src/math/vec4.cpp`.

**Mechanism:** Put four candidate coordinates in SoA lanes. Compute `t = dot(e,d)` and `perp² = dot(e,e)-t²` for normalized `d`, then test `perp² > (1.3*t*tanA + br)²` when the RHS is nonnegative. Radius exclusion can use `dot(h,h)>400`; retain an actual `br` only for candidates that need the expanded cone radius. EE MMI or VU0 can evaluate several candidates together; preserve scalar nearest-four insertion order afterward.

**Work removed:** Removes perpendicular sqrt and three explicit residual-component calculations per candidate; radius sqrt is delayed past inexpensive exclusions. Replaces an irregular scalar dot/residual loop with a lane kernel. No claimed reduction if candidate counts are tiny.

**Risks:** `dot(e,e)-t²` cancellation can go slightly negative. Use conservative tolerance or direct residual-square fallback near the boundary. Negative RHS, nonunit directions, NaNs and equal-depth ordering need legacy handling.

**Reversible experiment:** Scalar quadratic form first, four-lane form second. Compare accepted candidate IDs and sort order across a state sweep and shadow raster pairs; price actual admitted candidate counts in both arm orders.

## 4. GS SPRITE halos with two endpoints instead of six triangles — medium confidence

**Location:** Same corona/lamp builders as rank 1; billboard VCL corner projection; GS primitive construction in `vendor/tyra/engine/src/renderer/core/2d` and post-fx sprite emitters.

**Mechanism:** Camera-facing halos in the camera plane have constant clip `w` and depth across the quad. For a screen-axis-aligned basis, emit one z-tested GS SPRITE with two XYZ/UV endpoints instead of six triangle vertices. Preserve world anchoring through Tyra's actual `2048*x/w` projection, raster offsets and the existing depth mapping. This is a primitive substitution, not the failed scratchpad SPR experiment.

**Work removed:** Six GS vertices become two; eliminate four corner reciprocal/projection paths and reduce GIF output/state arithmetic. Single screen rectangle projection replaces world corner assembly.

**Risks:** Sprite edge sampling and triangle diagonal rasterization can differ by a pixel. Some basis choices produce rotated rectangles; use triangles there. Z interpolation, perspective STQ and clipping must be checked. Do not route through the ordinary HUD coordinate system.

**Reversible experiment:** One square scene corona well away from clipping, direct 3D-depth sprite packet, legacy comparison. Test subpixel motion and occluder silhouettes before any broad rollout.

## 5. Pool lattice as an indexed VU1 tile kernel — medium confidence

**Location:** `src/game_templates.inc:13907` (`buildPoolPatch`), `src/templates.cpp:12946` headlight 3×3 lattice; StaPip VU1 input/output layout.

**Mechanism:** Transmit the unique lattice vertices and let VU1 traverse a constant tile index schedule. A 4×4 pool uses 25 unique points instead of 96 list corners; a 3×3 headlight uses 16 instead of 54. Transform/project each unique point once, retain the converted result in VU memory, then emit the original triangle ordering and diagonal.

**Work removed:** Pool unique-position work falls 96→25; headlight 54→16. STQ and colors can likewise be computed once per unique corner. GS still receives the same list unless a separate primitive experiment is adopted. This goes beyond the existing strip optimization.

**Risks:** VU data memory competes with input/output buffers; clipped tiles require separate handling. Shared transformed values can alter rounding compared with per-occurrence computation if prior paths differ. Index and output lengths must be proven, not guessed.

**Reversible experiment:** Single fully guard-inside pool, fixed 25-entry input and immutable 96-entry schedule; precise legacy fallback for crossing tiles. Compare GS output and raster exactly.

## 6. Gobo projection as one affine matrix on VU1 — medium confidence

**Location:** `src/game_templates.inc:14488` (`goboST`), scene pool STQ loop at 15435, receiver STQ builders; `.../shared/vcl_sml.i` matrix and perspective-correction macros.

**Mechanism:** `S=.5*dot(e,d)+k*dot(e,r)`, `T=.5*dot(e,d)-k*dot(e,u)`, `Q=dot(e,d)` are three affine rows. Compose the light matrix with the object's model matrix and let VU1 generate STQ alongside camera MVP. Carry raw geometry, not an EE-generated projective texture array. Retain the current `Q>=.05` behavior explicitly.

**Work removed:** Eliminates three EE dot products and STQ stores per receiver vertex plus an entire STQ input stream. Adds roughly three affine row evaluations on VU1; likely more attractive for moving receivers/torch than static cached scene lamps.

**Risks:** A Q clamp makes the operation piecewise affine; it cannot simply disappear into the matrix. Geometry pull must not contaminate light-space coordinates. Clip-generated STQ, object transforms and nonuniform scale must match.

**Reversible experiment:** One moving receiver, unclamped positive-Q vertices first, boundary fallback second. Exact integer GS coordinate/raster comparison before pricing.

## 7. Height queries as a patch-plane walk — medium confidence

**Location:** `src/templates.cpp:12946` headlight lattice; `src/game_templates.inc:13907`, `:21304` (`roadSurfaceAt`), `:21434` (`groundSurfaceAt`).

**Mechanism:** Roads/junctions are piecewise triangle planes. Traverse all 16 or 25 sample points against a local plane packet: evaluate `y=a*x+b*z+c` and triangle edge halfspaces in SoA lanes, rather than starting a road search for every point. Coherent lattice traversal advances edge functions by fixed deltas across each row; max with the exact terrain result preserves the surface contract.

**Work removed:** Potentially replaces 16/25 repeated triangle-search traversals with one candidate collection plus affine/edge recurrence. Still evaluates every authored sample, without flattening bumps or roads.

**Risks:** Roads overlap; preserve maximum height and grip/priority semantics. A plane cannot be extrapolated across triangle edges. Terrain interpolation may be bilinear rather than planar and must remain exact. Static scene lamps already skip unchanged patch construction.

**Reversible experiment:** Moving player headlight only; scalar surface-query fallback for ambiguous edges and compare all 16 returned heights. Measure candidate triangle counts and full-loop timing separately.

## 8. Homogeneous camera pull instead of moving four world corners — medium confidence

**Location:** `src/game_templates.inc:17660` scene-corona camera pull; `src/templates.cpp:14029` lamp pull; receiver ray pull around `game_templates.inc:14518`.

**Mechanism:** For eye E, `P'=E+a(P-E)` with `a=1-pull/d`. In view space this is simply scaling P by a; scaling half-size by a preserves screen position/size. Compute projected center and extent from unpulled geometry, then apply a only to depth/homogeneous fields. For fixed-fraction receiver pull, the camera transform composition can fold the operation into the projection rows.

**Work removed:** Scene coronas avoid three normalized displacement divisions, world-center writes and repeated corner offset work; fixed-fraction receiver pulls avoid one pointwise world displacement per vertex. The distance root remains if the pull cap requires it.

**Risks:** Tyra uses a nonstandard projection scale, so derive from its matrix rather than NDC intuition. Projection z includes an affine offset; blindly scaling clip z is wrong. General lamp pull does not shrink half-size, unlike scene coronas; preserve their distinct rule.

**Reversible experiment:** Scene corona only, compare all projected coordinates/depth against legacy before changing packet generation. Keep world-space frustum oracle unchanged.

## 9. Normalization as a scheduled VU0 reciprocal-root kernel — medium-low confidence

**Location:** `src/templates.cpp:13935` lamp camera basis, lamp distance/facing/pull; `src/game_templates.inc:17568` beam camera/distance; VU math normalize macros in `.../shared/vcl_sml.i`.

**Mechanism:** Gather squared distances/lengths into lanes, issue VU0 `rsqrt`, and reuse each reciprocal length for facing and camera pull. Evaluate unrelated center transforms/color arithmetic during Q latency. VU0 has one Q pipeline, not four simultaneous reciprocal roots; the win must come from scheduling and reuse, not a fictional four-wide sqrt unit. Optionally one refinement bounds error.

**Work removed:** Each lamp's length currently feeds facing division and three pull divisions. One reciprocal-root value replaces the root plus repeated scalar divides; basis normalization has the same pattern.

**Risks:** Q latency and transfer overhead may exceed savings. Guard near-zero inputs and preserve facing threshold; error close to .05 can change admitted halos. Coexist with existing VU0 users and interrupt policy.

**Reversible experiment:** EE inline VU0 kernel for lamps only, strict near-threshold legacy fallback, record max coordinate/color error and actual physical full-loop timing.

## 10. Affine STQ via finite differences on the unique lattice — medium-low confidence

**Location:** Scene pool STQ loop at `src/game_templates.inc:15435`, `goboST` at 14488, headlight lattice at `src/templates.cpp:12946`.

**Mechanism:** Evaluate the affine projection rows on each unique grid height. Uniform X/Z spacing permits `STQ[i+1]=STQ[i]+rowX*dx+rowY*(height[i+1]-height[i])`; advance rows similarly. Expand the 25/16 results into the existing triangles. This keeps EE/VU packet language unchanged while replacing multiplication-heavy evaluation with recurrence.

**Work removed:** At most 96/54 repeated STQ evaluations become 25/16 unique evaluations; recurring interior points need vector additions and one height-delta multiply rather than three full dot products.

**Risks:** Accumulated roundoff can alter texels; restart each row or compute exact row seeds. The Q clamp needs per-point handling. Static cached STQ offers no recurring saving, so target moving patches.

**Reversible experiment:** Torch/headlight positive-Q patch only, compare UV-derived raster and source float errors. Gate off if exactness or own-source timing fails.

## 11. Move photometric modulation into a GS-rendered gobo atlas — low-to-medium confidence

**Location:** `src/game_templates.inc:13950` pool colors, scene pool shared `flashGoboTex`, `RendererCorePostFx` textured/flat sprite blend helpers.

**Mechanism:** Render a small per-light tile containing gobo×current RGB×brightness in VRAM using GS textured sprites. Pools sample their assigned atlas tile with constant neutral vertex colors. Brightness becomes a few atlas draw commands rather than a 96-vertex CPU color rewrite; the GS performs multiplication over a small tile. This spends pixel work to eliminate EE vector data work.

**Work removed:** Removes per-vertex color replication/modulation; one small textured sprite per changed light replaces a large color stream. Can regress from raster redirection, texture flushes or tile-resolution changes.

**Risks:** Quantization order, bilinear seams, CLAMP/atlas bounds, additive saturation and dither differ; atlas memory may be scarce. Never treat a VRAM arithmetic target as emulator-only verified. Shared sampler tile placement must preserve gobo border.

**Reversible experiment:** One light, small fixed VRAM tile, all other pools legacy. Compare dark borders and subthreshold pixels on physical raster before timing. Reject if required tile resolution/state cost erases benefit.

## 12. Shadow endpoints as a VU0 matrix/ray kernel — low-to-medium confidence

**Location:** `src/game_templates.inc:3225` (`emitMeshShadowVolume`), `:3366` (`emitBoxShadowVolume`), `:3557` dispatcher.

**Mechanism:** Keep EE topology/silhouette decisions, but transform unique mesh endpoints and their extruded far points in a VU0 block. Positions form columns; object-space transform and light-ray extrusion are vector stages. Reuse transformed near/far endpoints wherever neighboring volume faces reference the same endpoint; emit exactly the original front/back triangle list afterward.

**Work removed:** Reduces duplicated point transforms and normalizations on shared silhouette endpoints. Actual count depends on the selected caster mesh; no traffic or milliseconds inferred without an endpoint census.

**Risks:** Front/back facing, caps and eye-inside policy are essential to z-pass counting. Changed endpoint rounding can produce count cracks. VU0 call setup may dominate small boxes; keep their scalar path.

**Reversible experiment:** One mesh caster, topology decisions held identical, compare near/far endpoint arrays and shadow mask raster, then full-loop timing.

## 13. Six frustum planes as two lane-matrix evaluations — low-to-medium confidence

**Location:** Corona sphere-frustum loop in `src/game_templates.inc:17712`; `vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_spot_bounds.hpp` for conservative bound policy.

**Mechanism:** Transpose four plane normals/distances into VU0 rows and evaluate `[x,y,z,1]` into four signed distances, then the remaining two. Alternatively four lamp centers occupy lanes and each plane is broadcast. Compare against negative radii with vector masks. This treats a small repeated plane loop as dense matrix work.

**Work removed:** Removes six scalar dot/branch sequences per lamp; early rejected scalar cases may already be cheaper, so use only dense visible sets. No change to package classification or exact clipping.

**Risks:** Plane normalization and radius units, boundary floating-point order, early-out loss, VU0 contention. Use conservative tolerance and retain scalar ambiguous cases.

**Reversible experiment:** Beam visibility oracle only; compare every inclusion result and retain the exact old output order. Price high/low lamp-count scenes separately.

## 14. GS-ready colors through compact VIF UNPACK — low confidence

**Location:** `vendor/tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1_program.cpp`, corresponding clip/billboard program writers; `stapip_qbuffer_renderer.cpp:248`; VCL `FixColor`.

**Mechanism:** For deliberately unlit additive effects, convert final RGBA to the exact GS integer representation on EE and transmit V4_8 or V4_16 data, then use a specialized VU path that does not repeat float FixColor. This changes the stream alphabet rather than caching float payload. Keep lit geometry on its normal path.

**Work removed:** Color input may fall 16→4 bytes per vertex with V4_8, and VU conversion instructions disappear. EE conversion/packing is added; it must be performed where the final color already exists. Values outside unsigned 8-bit range need a wider format or fallback.

**Risks:** Exact float conversion semantics, saturation/overflow, VIF sign/unsigned extension and unpack destination stride, baked-stream compatibility and QWC/NLOOP accounting. A size error can hang GIF/VIF.

**Reversible experiment:** One unlit corona batch with known in-range colors, decode actual target packet bytes and compare integer GS color output. Do not alter general engine color ABI initially.

## 15. Spot landing as interval algebra rather than 0.3-step surface probing — low confidence

**Location:** `src/game_templates.inc:15377` scene spotlight landing march, `projSurfaceAt`, terrain/road surface functions.

**Mechanism:** Trace the cone axis through surface cells/triangle intervals. On a triangle plane, `lightY+dy*t - height(lightX+dx*t,lightZ+dz*t)` is affine in t; solve its sign interval. For bilinear heightfield cells it is quadratic along a line. Find the earliest *existing sampled* t satisfying the inequality, snapping to the original 0.3-step lattice, rather than evaluating surface search at every step. This preserves the sampled landing semantics rather than silently switching to an exact geometric hit.

**Work removed:** Replaces up to roughly range/.3 full surface queries per changed light pose with cell traversal plus interval solves. Static lamps already reuse landing, so ordinary warm-night benefit may be zero; dynamic lights/torch are the intended case.

**Risks:** Overlapping receivers, nonmonotone terrain, numerical step accumulation, cell edges, road-over-terrain max and origin-inside surfaces. `ceil(t/.3)` alone is insufficient because the original float march accumulates rounding. Use legacy fallback around ambiguous sample boundaries.

**Reversible experiment:** Offline compare the first accepted legacy sample across pose/range sweeps; then one moving spot with fallback and actual activation counter. Reject if measured ordinary workload rarely changes its key.

## Acceptance discipline

Run candidates independently. Preserve unchanged-quality full ordinary night and same-ELF reversible switches, sample both orders, and keep observers/export/readback outside clock windows where possible. Demand positive target activation during representative timed work. Separate exact GS raster equality, human physical appearance, flip-return cadence and inclusive nonpacing timing. None substitutes for the others. Keep clip-boundary fallbacks and DMA owners alive through genuine completion. Do not sum individual ablations or claim a pure EE/VU/GS cost from whole-loop contrasts.

