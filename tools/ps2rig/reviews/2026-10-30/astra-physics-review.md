# Independent general physics architecture review

Configured reviewer model: **GPT-6 Astra**. Review date: 2026-10-30.

This is source-based consultation, not performance qualification. No code changes, builds, new tests, hardware runs, commits or PR comments were performed. This Markdown output is the narrowly authorized write exception. Historical numbers are repository evidence. Proposed architectures and explanations explicitly remain hypotheses until qualified.

Reviewed checkout: `F:/Projects/tyra-editor/.tmp/worktrees/ps2-collision`, HEAD `eb7a95ce0ed0175afe629b8a08010ee4a29af527`. PR context: #285 at `8fc9f33a78e4bdabfe443b5354f7ee660c01a321`, fetched as `origin/pr-285`. Private source inspected under `F:/Projects/tyra-editor/.tmp/tests/vu0-sat/stream`, especially `district/src/gen/game_physics.gen.cpp`, `district/inc/game_runtime.gen.hpp`, and `engine/engine/inc/debug/vu0_shared_{packet,batch}.hpp`. Vendor prepared solver and historical experiment reports were inspected too.

## Conclusion

The strongest next step is contact/sleep correctness diagnosis plus an identical-input solver benchmark. For VU0, the most promising fresh experiment is a resident small-island solver, followed by asynchronous contact preparation. Repeating the current SAT classifier under another schedule is unlikely to help much while canonical EE confirmation still owns positives and exposed waits are small.

## 1. Current implementation

The private shared candidate begins around `game_physics.gen.cpp:1509`:

- Warm starting and islands are explicitly disabled at `:1509–1510`.
- World/body contacts are staged separately, then restored to intended order.
- `emit` at `:1775` applies `rbReduce`; the global contact reserve is not a cap.
- `wakePartner` at `:2208` resets a sleeper's rest counter, but its already-created step stays immovable until next frame.
- The micro interval at `:2223` forbids Vec4/Matrix4/render/callback COP2 use.
- `launchMicro` at `:2302–2318` finishes the old packet, launches the next, then confirms old results. Useful chaining is already implemented.
- Contact preparation at `:2593–2621`, six velocity passes at `:2646`, and three split-position passes at `:2686` remain sequential EE work.
- Final pose uses `x0 + v + vp`, and orientation integrates `w + wp`, around `:2747`.
- Sleep at `:2773` considers real motion/grounding, not residual penetration or pseudo correction.

Current VU data layout:

| Item | Size/capacity |
|---|---:|
| Shared body | 224 bytes |
| Bodies | 12 |
| Pair job | 16 bytes |
| Jobs | 24 |
| Result | 32 bytes |
| Occupied VU data | 242 quadwords / 3,872 bytes |
| Maximum input/output traffic | About 3,104 / 768 bytes |

Uploader transfers use EE `lq/sq` to VU memory, not free background DMA. Two packet objects are host staging, not two 4 KB VU banks. `finishVu0Shared` accepts a negative only after an EE separating-axis proof; everything else calls `scalarMotionContactReference`. This protects the corrected positive-contact regression, but leaves canonical positive geometry and all solving on EE.

For each contact:

```text
u = (va + wa cross ra) - (vb + wb cross rb)
ga = Ia^-1 (ra cross n)
gb = Ib^-1 (rb cross n)
kn = invMa + invMb + n dot (ga cross ra + gb cross rb)
lambdaNnew = max(0, lambdaN + (bounceTarget - n dot u) / kn)
```

Each impulse immediately changes bodies read by later contacts. This Gauss–Seidel dependency is fundamental. Solving coupled contacts from stale velocities changes the algorithm.

Runtime velocities are frame displacement-like quantities: prediction adds `v` directly, gravity is converted per frame. Thus `depth - (vnNow - vnPred)` is not automatically a dimensional error. Preserve that convention or deliberately convert the full interface.

## 2. Why prior experiments did not establish a general win

**Prepared scalar/macro:** `docs/prepared-contact-solver.md` records solver improvements in some cases, not a consistent whole-frame gain. Eager preparation paid for matrices on contacts without nonzero friction. Lazy preparation improved that, but packing/copy-back and repeated body traffic remained. Changed contact populations erased savings in dense piles. Prepared mathematics must be assessed inclusively, not dismissed or promoted from arithmetic counts alone.

**Storage drift:** private README records derived-geometry differences with identical local inputs. A common compiled transform restored the layout comparison's equality. Moving buffers can change compiler output; C++ expression equivalence is insufficient.

**Replay drift:** extracted compact/full kernels agreed with each other but differed slightly from caller-inline production. Use one compiled baseline kernel for both arms, then qualify that new common baseline explicitly.

**World/SAT overlap:** the documented diagnostic control exposed about 0.015–0.021 ms finish wait per frame. Hiding that cannot repay 0.17–0.27 ms schedule overhead. New hypotheses must reduce total work, traffic or replay, not merely witness simultaneous busy units.

**Dense positives:** body sharing reduces uploads, but canonical replay retains decisive geometry work and contacts feed a sequential solver. Rejection acceleration naturally helps less here.

## 3. Four concrete alternatives

All proposed layouts below are new ABIs, not implemented code. The assembled program must also fit VU0's separate 4 KB instruction memory. Data fit does not prove program fit.

### A. VU0 micro prepares canonical contacts; EE solves

Potential: moderate in contact-heavy scenes. Risk: moderate. First measure how much inclusive time is preparation versus repeated friction response.

Keep detection authoritative on EE. Once point, normal and poses are final, offload regular preparation. For a point-space impulse p:

```text
deltaU = K p
K = (invMa + invMb) I
    + skew(ra)^T Ia^-1 skew(ra)
    + skew(rb)^T Ib^-1 skew(rb)
```

Store six independent coefficients. Do not invert a general 3x3 matrix per contact. Preserve current slip-direction friction:

```text
t = tangentVelocity / length(tangentVelocity)
kt = t^T K t
fNew = projectToDisk(f - t * length(tangentVelocity) / kt,
                    radius = mu * lambdaN)
```

Proposed layout:

| Record | Bytes | Fields |
|---|---:|---|
| Body | 96 | position/inverse mass; v; w; three inverse-inertia columns |
| Contact input | 48 | normal/depth; point/restitution; ids, friction, threshold, flags |
| Contact output | 96 | ra/vnPred; rb/depth; angularA/kn; angularB/mu; six K coefficients and accumulators |
| Header/constants | 64 | counts, mode, generation, tolerances |

Twelve bodies and sixteen contacts use `12*96 + 16*(48+96) + 64 = 3520` bytes. Maximum input/output transfer is approximately 1984/1536 bytes. Active inputs must not be overwritten.

Test two variants independently:

1. Normal preparation only; keep friction K lazy on EE.
2. Normal plus K for a measured high-reuse class, such as persistent sliding contacts. Do not assume every contact needs it.

**Launch/overlap:** this image cannot run simultaneously with SAT micro. Either use canonical EE detection plus micro preparation, or finish the SAT epoch before preparation. In the first arrangement, launch after the first finalized world-contact block while EE executes scalar pair detection, sphere contacts, feature reconstruction and integer bookkeeping. Predicted poses/inertia must stay immutable. In the second, overlap batch packing, integer island construction, stable contact indexing/history lookups; less independent work exists.

Initially fence once before the full solver, not per contact. Macro solving starts only after micro completion/readback. Preserve constrained yaw-only vehicle inertia, static zero mass/inertia, clamp/restitution thresholds. Near-threshold branch-driving scalars may need EE evaluation. Symmetric K must be positive semidefinite within tolerance; materially negative `t^T K t` is a failure.

**Experiment:** captured immutable pre-solver states through common scalar preparation, micro normal-only, and micro normal-plus-K, followed by the same scalar solve/order. Measure pack/upload/launch/wait/readback/preparation/whole solve. Sweep 4/8/16 contacts, static/dynamic ratio and friction-active ratio. Reject if inclusive savings disappear or later solve cost erases them. Unlike the old serial prepared path, this hypothesis relies on autonomous preparation overlapping genuinely independent EE work.

### B. Certified SAT/CCD plus canonical feature reconstruction

Potential: moderate to high when positive replay is expensive. Risk: high. The saved late-contact regression makes this a correctness project first.

Keep two contracts distinct: preserving existing canonical geometry, versus adopting a new independently qualified contact algorithm. Never reintroduce approximate centroid recovery under the first contract.

Return a certificate, not just hit/normal:

- Winning axis id/orientation.
- Winning depth/TOI bounds.
- Margin to competing axes, or ambiguity.
- Support feature masks for each body.
- Degenerate/near-parallel/small-denominator flags.
- Classification certainty.

EE reconstructs the selected axis and support sum in canonical vertex order. Uncertified axis/feature decisions invoke the complete canonical routine. An arbitrary epsilon around a VU answer is not proof that alternatives cannot win.

For canonical support decisions, preserve actual transformed vertices: recomputed centre-plus-half-axis corners may round differently. Add eight 16-byte world vertices to the existing 224-byte body:

| Record | Bytes |
|---|---:|
| Body with canonical vertices | 352 |
| Job | 16 |
| Result certificate | 96 |
| Header | 64 |

Eight bodies/eight jobs use `8*352 + 8*(16+96) + 64 = 3776` bytes; input/output about 3008/768 bytes. This sacrifices existing 12-body/24-job density and must save enough replay to justify it.

Easy negatives certify separation over the accepted translation interval. Unambiguous overlaps certify minimum axis/support masks, with canonical final values on EE. CCD certifies interval ordering and support feature; ties, parallel axes and boundary times fall back. Rotational motion remains outside linear CCD's guarantee.

Before changing acceptance, collect certificate statistics with unchanged decisions: certifiable fractions by class, fallback reasons, estimated replay avoided and extra bytes.

**Scheduling:** launch a ready candidate batch while EE performs audited scalar world queries or previous-result reconstruction. Preserve next-launch-before-old-consume. Copy outputs before VU memory reuse. SAT, preparation and solver images have one owner; they do not execute concurrently.

**Validation:** mandatory saved late positive capture, near-tied axes/support vertices, large-coordinate small boxes, near-parallel edges, tiny/common motion, TOI near 0/1 and one-ULP perturbations. EE/VU rounding, fused operations, exceptional-value behavior and reciprocal/sqrt differences must be covered by the executed sequence's error envelope and input domain. Until then keep EE proof.

**Experiment:** same captured jobs, canonical ordered contacts and body states; compare current classifier/replay to certificate/selective reconstruction. Reject if certified fraction is too low or transfers cost more than replay saved. Eight-vertex support work is cheap; avoiding repeated axis traversal is the valuable part.

### C. Resident small-island micro solver, EE fallback, optional fused macro

Potential: strongest fresh VU hypothesis for dense small components. Risk: high implementation effort and moderate-to-high numerical risk. The distinguishing change is keeping bodies resident through all nine passes.

Existing compact representations are 112-byte `SolverBody` and 128-byte `SolverContact`. Eight bodies/twenty contacts plus 64-byte header use `8*112 + 20*128 + 64 = 3520` bytes. Twelve/twenty use 3968 bytes, with little headroom; start with eight. Static ground can use an explicit zero-body representation.

```text
load prepared contacts and bodies
repeat 6:
  for contact in stable island order:
    read CURRENT body state
    solve accumulated normal impulse; immediately write both bodies
    solve current slip friction; immediately write both bodies
repeat 3:
  for contact in stable island order:
    solve split constraint; immediately write pseudo states
return body state and required accumulators
```

Do not SIMD coupled contacts from stale state. Input is at most 3520 bytes. Returning v/w/vp/wp for eight bodies costs 512 bytes; optional history accumulators can use 32 bytes/contact, another 640 bytes.

**Independence:** islands share no writable dynamic body; static ground does not union all bodies. EE union-find operates on canonical contact ids. Preserve within-island order. Disjoint-component reordering is algebraically safe; changing internal order is a separate solver variant.

While micro solves island A, EE scalar/FPU can solve disjoint island B, perform integer bookkeeping or prepare the next island, with no COP2 helpers. Launch only after the contact set is known complete. A later connecting contact invalidates provisional independence. Conservative broadphase components can close earlier but may be too large.

**Oversized components:** initially fall back for the whole island. Do not split coupled islands into independently solved chunks and call them equivalent. Graph coloring makes each color's contacts body-disjoint, but colors execute serially and reordered Gauss–Seidel behavior changes. Per-color launches may erase gains; qualify convergence separately.

**Micro-to-macro transfer:** wait for completion, copy results, mark owner idle, enter macro epoch, and prohibit the next micro until macro completion. The existing busy check is not a complete ownership API.

A useful intermediate experiment is a fused macro contact update that retains normal/friction body vectors across more arithmetic instead of invoking macro helpers per impulse. EE branches/readbacks can negate it; price the entire contact including scalar crossings. Resident micro has the clearer goal: remove repeated main-memory body traffic across nine passes.

**Experiment:** identical captured islands through a common scalar island kernel, fused macro kernel and resident micro kernel in one ELF. Check final state, nonnegative normal impulse, friction disk, momentum, energy and pseudo corrections. Measure real-scene eligibility. Establish a minimum-work crossover and fall back below it without dropping contacts. If most contacts belong to oversized piles, this architecture cannot solve that workload; separated road objects may yield many eligible small components.

### D. Batched prediction/inertia/bounds with EE broadphase/control

Potential: moderate for many awake bodies, small for mostly sleeping road props. Risk: moderate-to-high because this changes earliest trajectory-defining arithmetic.

Compute vector prediction, quaternion integration/normalization and world inertia:

```text
vPred = v + acceleration * dt  // respect existing displacement convention
qPred = normalize(q + current_rbIntegrate_delta(w, q))
invIworld = R(qPred) * invIlocal * transpose(R(qPred))
compute conservative current/swept bounds
```

Preserve actual integrator/clamps; do not change integrator and processor simultaneously.

| Record | Bytes | Fields |
|---|---:|---|
| Input | 144 | pose, linear/angular state, local inertia, shape bounds/centre, forces, flags |
| Output | 176 | predicted position/quaternion, rotation, inverse inertia, swept bounds, state |
| Header | 64 | frame convention, clamps, counts |

Twelve bodies use `12*(144+176)+64 = 3904` bytes. Hull vertices are a later stage or require a smaller batch.

**Launch/overlap:** launch after all forces, vehicle predictions and necessary wake decisions for that body are final. EE can pack the next batch, inspect immutable static-index metadata, build integer mappings/active lists and perform independent route bookkeeping. Current broadphase needs predicted bounds; querying an incompletely populated index can miss bodies. Finish population before queries unless a complete partitioned-query proof exists.

**Correctness:** tiny transform differences already caused layout drift. Establish a common reference and explicit tolerance contract. Conservative bounds must include arithmetic error and rotational swept extent; this protects admission, not bit-identical contact geometry.

**Experiment:** replay identical awake snapshots, sleepers, yaw-only vehicles and irregular hulls. Measure inclusive prediction, geometry, index and full physics. Reject if EE must rebuild canonical poses anyway: that duplicates the offloaded work.

## 4. Resting interpenetration diagnosis

### Fixed pseudo target is not three geometric corrections

Private `game_physics.gen.cpp:2686–2712` computes:

```text
remaining = depth - (vnNow - vnPred)
target = 0.5 * max(remaining - 0.005, 0)
```

All position iterations solve pseudo velocities against this same target. They do not recompute geometry, normals, offsets or depths.

An isolated converged linear contact receives approximately one 50% correction above slop, not `1 - 0.5^3 = 87.5%`. More iterations improve coupled convergence toward that target. They do not multiply the depenetration fraction. Across frames, a stationary isolated pair should approximately follow `dNext - slop = (1-beta)*(d-slop)`.

### Sleep can freeze residual penetration

The sleep test ignores vp/wp and post-solve residual. A quiet body can receive correction, reach its rest counter and freeze while overlapping.

Proposed rule: sleep an island only when real velocity, pseudo correction and measured residual are small and support partners have not changed/moved. Compare settling time, wake propagation and cost. Do not disable sleep globally.

### Near-boundary corner manifolds can select an unsuitable face

`game_runtime.gen.hpp:4900–4911` rejects a corner if ANY plane depth is `<=0`. Exact lateral-face equality therefore rejects the corner and can permit SAT fallback; it does not create a zero-depth horizontal contact.

A slightly inset/rotated corner may instead choose a tiny positive lateral depth as its minimum. Any corner contact suppresses `if (!n ...)` SAT fallback. This could yield lateral constraints where a vertical support manifold is needed. This remains a hypothesis requiring an epsilon-offset/rotation sweep.

`rbReduce:4855–4897` selects deepest, farthest, maximum area and a fourth spread point. It does not cluster normals/reference faces; n<=4 passes directly.

A robust alternative is SAT-selected reference/incident face clipping, a stable normal and up to four spread points; edge–edge cases remain separate. This is a contact-algorithm change, not merely acceleration, and needs independent qualification.

### Single-point fallback and frozen linearization

Scalar convex fallback averages support features into one point. This may suit some edges but does not provide a four-point face support manifold. It can leave angular modes underconstrained or rediscover changing support points. Record actual pair cardinality/lever arms before diagnosing it.

Position passes use predicted offsets/inertia throughout. Large angular correction invalidates linearization. Stack coupling, order and mass ratios can slow convergence. Controlled alternatives include stable bottom-up order, alternating sweep direction, geometric residual refresh, block normal solve for a face manifold, and stable-feature warm starting with timestep scaling. These alter behavior; compare residual at matched cost and cost at matched residual, not just iteration counts.

### Late wake and CCD limitations

`wakePartner` changes rest state, not the current step's zero inverse mass/inertia. A sleeper is effectively infinitely heavy until next frame. Conservative connected wake before prediction is more coherent; any late wake needs an explicit policy, not partially changed records.

Zero missed canonical hits does not prove adequate manifolds, convergence, rotational CCD, acceptable residuals or render/collider synchronization. Inspect actual collision bounds and final rendered transforms; measure overlap after writeback, not only predicted contact depth.

## 5. Small analytical and physical fixture plan

These are proposed tests, not executed tests.

1. One box/static plane, gravity off, known penetration, sleep off. Verify 50% residual decay above slop. Compare one/three/six position passes; do not pretend each pass halves depth again.
2. Same with sleep on. Log real motion, pseudo correction, residual and rest counter; determine whether sleep freezes overlap.
3. Two equal boxes, face contact. Sweep exact alignment, tiny signed lateral offsets and yaw around zero. Record normals, corner/SAT route, manifold size and final depth.
4. Three/five-box stacks: equal masses, then 1:10/1:100, original/reversed/proposed stable order. Log per-level residual and pass convergence.
5. Support removal and wake: lateral/vertical support motion. Verify finite-mass state for every affected member before contact solve.
6. Car/one box: centred/offset impacts, resting push and fast crossing. Verify opposite impulses, yaw restriction, no old bumper response, one full object pass.
7. One long interval versus two short intervals, preserving intended pass policy. Diagnose timestep sensitivity; do not call it an FPS comparison.

Initial proposed quality gates, to agree as design targets:

- No sleep while residual exceeds twice `PHYS_SLOP`.
- Resting residual p95 at or below twice slop.
- Transient residual above four times slop decays below that threshold within a stated short interval, initially 0.25 s for small fixtures.
- No monotonic sinking, nonfinite state, hidden contact loss or correction-driven launch.
- Report relative depth against smaller extent too; absolute slop alone fails across scales.

Save complete pre-step/final state for a failing pair. Screenshots identify symptoms; replayable state identifies causes.

## 6. Recommended sequence and practical target

1. Diagnose fixed correction target, sleep residual and near-boundary manifolds.
2. Establish one common compiled scalar solver and immutable captured corpus.
3. Measure preparation, normal, friction, split solve, copy-back and detection separately.
4. Prototype resident small-island solving and normal-only micro preparation independently.
5. Attempt certified positive SAT acceptance only after measuring certifiability and preserving the late-case regression.
6. Consider prediction offload last because it moves the earliest trajectory arithmetic.

For the road target, 100 separated props should mostly remain sleeping, indexed and untouched until interaction, while retaining collision presence. For dense active piles, better manifolds/convergence may remove more work than faster arithmetic and must precede any claim that a faster version of visible interpenetration is acceptable.

## 7. Additional low-risk unified-path finding

The follow-up source check in `district/src/gen/game_vehicles.gen.cpp` confirms `kind == 2` at `:4097` only fills `pushIdx` then returns; its counter and bumper-response consumer at `:4976–4977` are disabled when `VehicleContacts::enabled`. Skipping this dead gather in unified vehicle physics can avoid redundant per-car prop walking without removing any shared-grid/SAT contacts. Include the `recordCol` substep-cache path at `:4221`.

Do not delete kind-2 entries from global `vehColliders_`: camera sweep `:3400–3404` still calls `sweepObject` for them. Use a query-specific view/mode and preserve legacy behavior. No implementation or timing was performed; qualify with identical ordered contacts, complete body state, unchanged camera collision and one-full-pass accounting.
