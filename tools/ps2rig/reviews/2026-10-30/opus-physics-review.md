# Independent physics review: PS2 contact pipeline, VU0 work split and dense-pile penetration

The changes below are proposals I haven't tested. Every gain is a hypothesis that needs physical-console A/B testing before it counts. Measured figures come from `tools/ps2rig/README.md`, the `results/*.json` files and the docs. I worked read-only and only read code. As you asked, I didn't read the PR #285 traffic code or the other reviewers' output, so this report says nothing about traffic rendering beyond where the boundary sits.

---

## 0. Summary

1. **The visible interpenetration is mostly a solver and model problem, not load.** In the measured path, warm starting is off and islands are off (`game_physics.gen.cpp:1509-1510`). That leaves 6 cold velocity passes and 3 split passes at β = 0.5 and slop = 0.005 (`game_runtime.gen.hpp:4913-4919`). On top of that:
   - Box–box contacts come only from corners found inside the other hull. When no corner is inside, the fallback is a single SAT contact point.
   - Sleeping bodies are immovable, and a body can fall asleep while it still overlaps something.
   
   Each of these gives persistent overlap with no NaN and no missed hit. Section 2 lists exact corrections and a factorial test that tells the causes apart.
2. **VU0 gains little today because of where the EE work sits, not because of transfers.** Measured transfer and fence costs are small: upload 0.18 ms, readback 0.075 ms, finish wait 0.006–0.024 ms per frame. The big EE costs are the corner test (2.0 ms), preparation (1.06 ms) and canonical reconstruction (0.74 ms) (`README.md:1158-1168`).
   - VU0 currently only receives pairs where the EE corner test found zero corners (`game_physics.gen.cpp:2419`).
   - Every result that isn't a proven negative then reruns the full 21-axis reference on the EE (`vu0_shared_packet.hpp:61-79`).
   
   Proposal: **a fused corner + SAT VU0 job with certified error margins.** VU0 classifies every corner and every axis as certain or ambiguous. The EE then recomputes canonically only the flagged corners and the near-minimum axes. Contacts stay bitwise canonical, but the EE confirmation work shrinks.
3. **The best VU0 solver job is small independent islands solved entirely in micro mode (all 6+3 passes on VU0), while the EE solves the large islands in scalar at the same time.** This is not the rejected macro solver; §3.3 explains the difference.
4. **The cheapest traffic-relevant gain has nothing to do with VU0: skip the object pipeline when only cars are awake.** This targets the measured +0.79/+1.09 ms no-interaction overhead (`README.md:502-511`). It's provably coverage-preserving and can be checked against the existing bitwise isolation gate.

---

## 1. Code map: what the pipeline actually does

| Stage | Evidence | Notes |
|---|---|---|
| Body prediction | `game_physics.gen.cpp:1635-1729` | Sleepers get `invMass=0`, `invIw=0` and `x1=x0` (1647, 1663-1666). Cars come from `VehicleContacts::cars` with yaw-only `invIw[4]` (1651-1655). |
| Car representation | `vehicle_contacts_members.inc:35-37, 56-68, 70-98` | 8-corner box with drive mass and yaw inertia. The correction is applied once in `finishVehicleContact`. |
| Order of work | `vehicle_contacts_members.inc:100-113` | Vehicles step first, then the prediction capture, then **one** `updateObjectPhysics()`. |
| Hull–hull contacts | `game_physics.gen.cpp:2385-2411` | Corners of A inside B and B inside A. The face for each corner is chosen by minimum depth (`rbInsideHull`, `game_runtime.gen.hpp:4900-4911`). |
| SAT fallback | `game_physics.gen.cpp:2449-2459`; `convex_collision.hpp:493-514` | Runs only if n == 0. Emits **one point**: the average of the two support-feature centroids. |
| CCD contact | `convex_collision.hpp:516-541` | Fixed-orientation linear sweep. Depth is measured at the end pose. |
| VU0 dispatch | `game_physics.gen.cpp:2419-2447` | Only when n == 0, the pair is box/box, and the EE hint test did not separate it. |
| VU0 consume | `game_physics.gen.cpp:2237-2292`; `vu0_shared_packet.hpp:61-79` | Either a proven negative, or a full `scalarMotionContactReference`. |
| Order preservation | `game_physics.gen.cpp:2442-2443, 2467-2475` | Placeholder plus stable compaction (one contact per placeholder). |
| Solver | `game_physics.gen.cpp:2593-2712` (legacy), `contact_solver.hpp:212-274` (prepared) | 6 velocity passes (normal then friction), then 3 split-position passes with `target = β·max(0, remaining − slop)`. |
| Sleep | `game_physics.gen.cpp:2769-2793` | Checks only grounded, \|v\|² < 0.00025 and \|ω\|² < 0.00017. **Remaining depth is not checked.** |
| Waking a partner | `game_physics.gen.cpp:2208-2221` | Only when approach speed > `wakeSpeed`, or when riding a body that moves away. Takes effect **next frame**. |
| Islands (available) | `contact_history.hpp:102-146` | Deterministic union-find plus a stable `arrange`. Static and sleeping partners never merge islands. |

**Measured stage costs (same ELF, nominal step, drive window; `vehicle-contacts-impact-timing-2026-10-30.json:1337-1420`):**

| Arm | Predict | World | Body | Solver | Writeback |
|---|---|---|---|---|---|
| 128 legacy | 0.89 ms | 1.59 ms | 9.51 ms | 5.18 ms | 0.23 ms |
| 128 shared | 1.15 ms | 0.53 ms | 10.09 ms | 6.26 ms | 0.31 ms |

The body stage is the largest cost and the solver is second.

**Measurement-scope caveat:** every number above is a nominal-step, same-ELF figure. None of them is normal-clock FPS, and none qualifies a production default.

---

## 2. Penetration: likely causes and fixes

### 2.1 Likely causes, from most to least likely (hypotheses until the §2.3 matrix runs)

**P1 – Cold Gauss-Seidel with fixed 6 passes.**
- With no warm start, a stack H bodies high needs roughly O(H) passes to carry the weight down.
- Whatever velocity error is left over becomes per-frame sinking. The split pass, at β = 0.5 with only 3 passes, then reaches only about 3 contacts deep per frame.
- Expected signature: penetration grows with stack layer and with island size, and is independent of contact source.

**P2 – Inconsistent box–box manifold.**
- Corners pick their own minimum-depth face, so one pair can mix up and side normals. For a deep corner, the closest face can be a side face, which slides the bodies laterally into each other.
- Edge-crossing and rotated face–face cases (for example equal boxes rotated 45°) have no corner inside either box. They get the single averaged SAT point, which can't resist rocking, and the bodies rotate into overlap.
- Signature: depth concentrated in SAT-sourced pairs and in pairs whose normals disagree.

**P3 – Sleep and immovable partners.**
- A body sleeps when it is slow, even if its depth is greater than slop. After that it is immovable (`invMass=0`).
- An awake body squeezed between two immovable sleepers can't be resolved. A slowly sinking body never reaches `wakeSpeed`.
- Signature: frozen overlap involving `asleep` partners, which stays constant across frames.

**P4 – Linear-only CCD and end-pose depth.**
- Rotation-induced tunnelling isn't covered. A CCD contact's depth comes from the end pose, and β = 0.5 removes it over several frames.
- Signature: transient overlap right after impacts, from CCD-sourced contacts.

**P5 – Car/prop mass ratio.**
- With a mass ratio around 10²–10³, 6 GS passes leave a prop pinned between the car and the ground or other props under-resolved.
- Signature: depth spikes only in contacts that touch a car.

### 2.2 Corrections

These are model corrections, not hidden limits. Each one needs a new baseline and quality gates, not bitwise equality with the old trajectory.

**C1 – Sleep depth gate** (cost ≈ 0).
- In the split loop, record each body's post-solve residual: `resid = remaining − (vp·n)` after the last pass.
- Only advance `restFrames` when `maxResid(body) ≤ 2·PHYS_SLOP`.
- Also wake a sleeper partner when the candidate depth is greater than 2·slop, regardless of `vn` (`wakePartner`, 2208).

**C2 – Box–box clipped manifold** (for `s.box` pairs only; other hulls keep the existing path).
- Find the minimum-penetration axis with the existing 21-axis SAT (no axes dropped).
- **Face axis:** the reference face is the face whose normal best matches the axis. The incident face is the other box's face most anti-parallel to it. Clip the incident face's 4 vertices against the reference face's 4 side planes (Sutherland–Hodgman, at most 8 points). Keep points where `depth = d_ref − n·p > −margin`, then reduce to 4 with the existing `rbReduce`. All points in the pair share one normal.
- **Edge–edge axis:** use the closest points of the two support edges, giving 1 point at the true contact.
- Corner contacts are no longer used for box–box. Every box–box pair now goes through SAT, which is exactly what makes the fused VU0 job in §3.2 worth it.

**C3 – Warm starting with feature-ID matching**, decoupled from coordinated island sleep.
- The earlier warm-start arm was rejected on cost: 22.73 vs 19.14 µs per contact, plus island wake (`temporal-physics.md:373-394`). That cost came from anchor-distance hash matching (`contact_history.hpp:65-97`).
- With C2, every point gets a stable ID: (reference face, incident face, clip-edge index). Store it in a per-pair cache slot, giving O(1) matching.
- Seed `jn` and `ft` only. Never carry `jp` (split impulses already aren't carried).
- Keep 6+3 passes. Lowering the pass count is an **optional, clearly labelled** follow-up, allowed only if the quality gates show parity.

**C4 – Optional, labelled: mass-ratio handling.**
- For contacts between a car and a prop, run one extra normal pass in which the car is treated as infinite mass. This is shock propagation for that set only.
- It changes the physics model, so it's off by default and has its own gate.

**C5 – Do not change β or slop blindly.** Measure first (§2.3).

### 2.3 Diagnostic fixture (shadow-only, before any optimization)

After writeback, recompute every contact pair's depth at the **final** poses with the canonical EE reference. Diagnostics only; disable it for timing. Histogram it by:
- source (corner / SAT / CCD / world / car);
- partner state (awake / sleeping / car);
- stack layer (count of supporting contacts down to the ground);
- island size.

Also record:
- the velocity residual `min(0, vn)` after pass 6;
- the position residual after pass 3;
- "slept with residual > slop" events;
- "body with ≥ 2 immovable partners on opposing normals".

**Scenes:**
- stacks of 2, 4 and 8;
- a 15-box pyramid;
- 4 equal boxes rotated 45° on top of each other (forces the SAT single-point path);
- the 64 and 128 random piles;
- a car plowing into a 3-deep wall.

**Arms, on the same ELF:**

| Arm | Change |
|---|---|
| A | Baseline |
| B | 12+6 passes (diagnostic only, not a proposal) |
| C | Warm start (C3) |
| D | Clipped manifold (C2) |
| E | Sleep gate (C1) |
| F | C + D + E |

**How to read the result:**
- B fixes it → P1 dominates.
- Only D fixes the rotated stack → P2.
- E removes the frozen overlaps → P3.

**Acceptance for F:**
- p99 final-pose depth ≤ 2·slop in settled stacks (8 layers) after 120 frames;
- no "slept with residual > slop" events;
- the existing finite-mass momentum budget passes (`vehicle_contacts_probe.hpp:42-55`);
- energy doesn't increase in undriven settle phases;
- zero missed canonical hits.

**Rejection:** any NaN, any VU0/FPU status O or D flag, or depth getting worse in any scene class.

---

## 3. EE / VU0 architecture

### 3.1 Hardware rules the plan follows

- **Only VU0 micro mode and DMA make independent progress.** EE integer, FPU and COP2 macro instructions all issue from one EE instruction stream.
- **While a micro program runs, VU0 belongs to it.** The EE must avoid COP2 macro instructions, including Tyra `Vec4`/`Matrix4` helpers. The existing audit (`README.md:1040-1054`) shows that later geometry and matrix updates reach COP2, so they stay after the fence.
- **Memory budget.**
  - VU0 data memory is 256 qwords.
  - VU0 micro memory is 4 KB (512 instruction pairs).
  - The current SAT image is 536–552 words (about 270 pairs). A solver image won't fit alongside it, so the plan uses **one program swap per frame**, after the collision fence. That fits the existing `vu0MicroOwner` mechanism (`vu0_shared_batch.hpp:17-24`).
- **Upload cost.** Measured EE upload is about 11 µs per 194-qword packet, so roughly 17 cycles per qword (derived from 0.176 ms over ~15.4 batches per frame — hypothesis-grade). Transfer isn't the bottleneck; EE packing and confirmation are.

### 3.2 Job A: fused box-pair corner + SAT micro job with certified margins

**Record per box: 5 qwords uploaded, 8 resident.**
- q0 = centre.xyz | magnitude M = ‖c‖∞ + Σ‖h_i‖∞
- q1 = h0, q2 = h1, q3 = h2 (half-axes)
- q4 = motion.xyz
- In a per-packet prologue, VU0 derives the unit axes `u_i = h_i·rsqrt(h_i·h_i)` into 3 resident qwords.

All 21 axes are still evaluated: ±u for both boxes plus 9 cross products.

**Job: 1 qword** = (body A address, body B address, common-motion flag, 0).

**Result: 3 qwords per job.**
- r0: minimum depth d*, index of the first minimum, gap to the second-best candidate, status bits.
- r1: corner classes, 2 bits per corner (OUT / IN / AMBIGUOUS), 16 bits for A-in-B and 16 for B-in-A, written with ISW.
- r2: the first certain separating axis f; an ambiguity mask for axes below f (21 bits split across two 16-bit VI values); CCD enter and leave.

**Capacity:** 2 + J + 8·NB + 3·J ≤ 256. **NB = 16 bodies and J = 30 jobs fits (250 qwords)**, compared with today's 12 and 24. A full packet is flushed and the same pair retried, so there is still no truncation.

**VU0 corner test, for corner p of A against B:**
- `l = Uᵀ(p − c_B)`: one SUB plus a 3-instruction MULA/MADDA/MADD.
- `s_i = |h_Bi| − |l_i|`.
- OUT if any `s_i < −ε`; IN if all `s_i > ε`; otherwise AMBIGUOUS.
- Margin: `ε = K·2⁻²³·(M_A + M_B + 1)`. Start with K = 64 and prove it on the host with an exact-arithmetic oracle.
- The EE reruns the canonical `rbInsideHull` code (lines 2387-2410) **only for IN and AMBIGUOUS corners, in index order**. A certified OUT corner has a plane with `d < 0` on the EE too, so the canonical code would have skipped it. The emitted list is therefore identical.

**SAT certification**, used only if the canonical corner count is 0:
- The EE first runs the canonical hint test (moved from pre-queue to consume, keeping the canonical order: corners, then hint, then SAT).
- Then it evaluates the canonical `axis()` only for:
  - the ambiguous axes below f, in order;
  - axis f itself, so the separating hint written to `axisHistory` is bitwise canonical;
  - or, for overlap, every axis with VU depth ≤ d* + 2ε.
- Why this stays exact:
  - An axis that isn't flagged is certainly overlapping and certainly has depth greater than the canonical minimum, so the strict `<` first-index tie-break (`convex_collision.hpp:450`) can't pick it.
  - For CCD-only hits, the same rule applies to t0 near the maximum and t1 near the minimum.
  - Near-degenerate cross products (l2 within the margin band of 1e-10) are always flagged.
- The support points (16 dot products) stay on the EE.

**Order with multiple contacts per pair:**
- Placeholder entries point into a per-pending side buffer holding up to 4 contacts.
- The final stable expansion replaces the existing single-slot compaction at 2471-2474.

**Why this cuts duplicate confirmation without losing coverage:**
- Today, every positive costs 21 canonical axes, and every pair with corners never reaches VU0 at all.
- With this job, a typical resting box pair costs the EE 0–4 corner checks plus 1–3 axes.
- A dead filter fails the falsifiers listed under Gates.

**Gains (hypotheses):**
- Corners: 2.0 → about 0.6 ms in dense 128.
- Reconstruction: 0.74 → about 0.25 ms.
- Preparation and packing shrink because records go from 14 to 5 qwords.
- Net about **1–2.5 ms** in dense piles. Only about **0.1–0.4 ms** in the 100-prop road scene, where few pairs exist.

**Gates:**
- Host exact-arithmetic oracle with 10⁶ pairs, including resting depth ≈ slop, 45° stacks and near-parallel edges.
- Bitwise canonical contact equality and `axisHistory` equality.
- Falsifiers that must fail:
  - K = 0 must produce mismatches;
  - zeroing the corner classification must be detected;
  - forcing all AMBIGUOUS must give identical results at higher cost (proves the fallback path).
- Physical: both images, the 50,000-case precision corpus, the saved late-case replay, a zero-miss exhaustive shadow, and the measured rate of ambiguous corners and axes (reject if > 15% of corners are ambiguous in resting piles).

### 3.3 Job B: small-island micro solver (VU0 runs all 6+3 passes; EE solves large islands at the same time)

**How islands are formed:**
- Use `ContactIslands` (`contact_history.hpp:102-146`) without coordinated sleep and wake.
- Static and sleeping partners have zero mass, so islands share no writable state.
- Reordering contacts **between** islands is therefore bitwise identical on the EE. The GS order **within** an island is preserved by `arrange`.

**Routing rule (no truncation):** an island goes to VU0 if `7B + 9C ≤ 237`; otherwise it goes to the EE.

**Packet layout:**

| Content | Size |
|---|---|
| Header | 1 qword |
| Constants: slop, β, 1e-9, 1e-14 | 1 qword |
| Spare | 2 qwords |
| Island table (first contact, count, first body, count), up to 8 islands | 1 qword each |
| Zero body | 7 qwords |
| Each body | upload 5 qw: v\|invMass, w, R rows with local inverse-inertia diagonal in w. VU0 builds `invIw = R D Rᵀ` and zeroes vp/wp. 7 qw resident. |
| Each contact | upload 4 qw: n\|e, ra\|μ, rb\|depth, index qword (ILW-readable addresses). VU0 expands in place to the 8-qword `SolverContact` layout (`contact_solver.hpp:61-69`) plus the index qword = 9 qw. |
| Readback | 4 qw per body (v, w, vp, wp); jn/ft too if C3 is on |

Examples: a single prop on the ground (B = 1, C = 4) is 43 qwords, so 5 such islands fit in one packet. The largest single island that fits is B = 4, C = 23.

**What VU0 computes (same math as `prepareSolverContact` and `solvePrepared`):**
- Preparation:
  - `angA = I_A⁻¹(r_A × n)`;
  - `kn = m_A⁻¹ + m_B⁻¹ + (angA × r_A)·n + (angB × r_B)·n`;
  - `vnPred`;
  - bounce target;
  - `μ = 0.05 + 0.95f`;
  - K's six coefficients (`contact_solver.hpp:101-118`), computed eagerly.
- Normal step:
  - `j' = max(0, jn + (b − vn)·kn⁻¹)`;
  - then apply `dj·invM·n` and `dj·ang`.
- Friction: dynamic slip direction, `kt = tᵀKt`, and the same circular Coulomb clamp. It's branch-free: the skip conditions become 0/1 multipliers, and SQRT/DIV run in the lower pipeline.
- Split step: `target = β·max(0, remaining − slop)`, which is exactly equal to the branch form.

**Hiding FMAC latency without changing GS order:** interleave contact k of island X with contact k of island Y, using register banks VF1–15 and VF16–30. Coloring isn't needed because independent islands already give the independence.

**Concurrent EE work:**
- Launch the first small-island packet immediately after partition.
- While it runs, the EE packs the next packet and solves large islands with the existing scalar code.
- Any other overlap must be EE code proven COP2-free by `ee_pipeline_audit.py`.
- Integration and writeback happen after the final fence.

**How this differs from the rejected prepared-VU0 macro solver** (`prepared-contact-solver.md:86-114`):
1. Micro mode, not per-contact macro, so there are no EE↔COP2 round trips and no EE-side branches per contact.
2. Latency is hidden by interleaving independent islands.
3. There is real overlap: the EE works on large islands while VU0 runs.
4. The comparison is designed so trajectory changes can't hide the backend effect: per island, a shadow `solvePrepared<false>` runs on a copy of the **same prepared input in the same order**, and outputs are compared per island before any trajectory test.

**Gains (hypotheses):**
- The EE currently costs about 2.1 µs inclusive per contact visit (8.49 ms / ~450 contacts / 9 passes, `temporal-physics.md:387-391`).
- VU0 might manage about 80–120 cycles per contact visit when interleaved.
- In the drive-through scene (~20–40 awake props in small islands), the solver might drop by **0.7–1.5 ms**.
- In dense piles, the gain is small because the pile is one big island and stays on the EE.

**Gates:**
- Per-island shadow, with relative error ≤ 1e-4·(|v| + g·dt) per component and jn ≥ 0.
- VU0 sticky status flags O and D read after every packet; any O or D fails (VU0 saturates rather than producing NaN).
- §2.3 depth gates and momentum budgets.
- Routing counters must account for every contact exactly once.

### 3.4 Other VU0 candidates

| Candidate | Verdict | Why |
|---|---|---|
| Graph coloring of dense islands on VU0 | Not now | 128 × 7 qwords of bodies doesn't fit in 256 qwords. It would need per-color-per-pass round trips (about 10 colors × 9 passes) and changes the GS order. Revisit only after C3, and only on the EE scalar path first. |
| Pose transforms or prediction on VU0 | Rejected | Macro version measured 43.6% / 12.2% slower (`physics.md:224-230`), and canonical poses are needed anyway. |
| World inverse inertia `R D Rᵀ` and prepared Jacobians | Yes, inside Job B | They're independent per body and per contact, and their output stays resident in VU0 memory, so nothing is read back. |
| Integration and writeback | Low value | 0.2–0.3 ms total, and Euler/atan2 work stays on the EE. |
| VIF0 DMA upload instead of EE copies | Optional microbenchmark | Do first: a hardware test that VIF0 UNPACK into a disjoint half of VU0 memory while a micro program runs is legal and gives correct results. Gain is at most about 0.1–0.2 ms per frame. |
| Macro mode anywhere in this plan | No | VU0 micro owns COP2 during the windows that matter. |

**Precision rules:**
- Compile the canonical EE routines as single `noinline` kernels with a fixed contraction policy (`-ffp-contract=off`, or audit `madd.s` usage). The 2.98e-8 / 5.96e-8 differences between inline callers (`README.md:953-985`) show that contraction or inlining changes results.
- Certified margins must cover VU0 error plus EE error relative to exact arithmetic, scaled by M, because VU0 multiply isn't exact truncation.
- On VU0, using a reciprocal and then multiplying is fine because it's tolerance-gated.

---

## 4. Common car/prop solver (one full pass)

**Keep what works today:**
- Vehicle control, suspension, world collision and car–car response run first.
- Cars are excluded from world contacts and from car–car pairs (2342).
- Inverse inertia is yaw-only: `invIw[4] = invYaw`.
- The correction is returned once.

**Data organization:**
- The car body record uses the same 7-qword format as props.
- Contacts that touch a car are tagged so the C4 extra pass and the diagnostics can find them.
- An island containing a car that fits the routing rule can go to VU0; the car is just a body.
- `finishVehicleContact` keeps converting the result into `speed`, `lateral`, `velY` and `yaw`.

**New: car-only early-out.**
- After `captureVehicleContactPrediction`, any prop inside a car's swept sphere has already been woken (`vehicle_contacts_members.inc:41-51`).
- So if no non-car body is awake, no car–prop contact is possible. Skip `updateObjectPhysics` entirely, and keep the existing `anyAwake` early return semantics by ignoring cars.
- **Gate:** the existing isolation admission (all 750 end-state fingerprints bitwise equal) plus the exhaustive car/prop shadow with zero reference hits.
- **Gain (hypothesis):** most of the +0.79/+1.09 ms no-interaction overhead in **every** frame without a prop interaction, which is most traffic frames. It should scale with up to 13 cars.

---

## 5. Hardware A/B matrix (no console runs until both consultations are in)

**Scenes:**

| Scene | Description |
|---|---|
| S1 | 100 separated road props: spacing ≥ 2r + 0.5, settled 300 frames with zero awake bodies required. |
| S2 | Same as S1, plus 1 car clipping 10 props. |
| S3 | 5 cars plowing about 40 props. |
| S4 | 5 HQ + 8 low-poly traffic through the prop line. |
| S5 | Dense 64 and 128 piles (secondary). |
| Scaling | 1, 3, 5, 10 and 13 cars, both isolated and in S1. |
| Lighting | Day and night as a whole-frame control only. Physics changes should leave VU1/GIF time unchanged; check with `gputl`. |

**Per-frame stage columns:** predict, world, body (split into corner, SAT, confirm, pack, upload, wait), partition, VU0-island wait, EE large-island solve, writeback, and the combined boundary.

**Report:** mean, p95 and max; the count of frames over 16.67 ms; EE work vs frame period (CLD probe) to tell EE-bound from VU1/GS-bound frames.

**Order of runs:** nominal-step same-ELF admission first, then unshadowed timing on the same ELF, then normal-clock confirmation.

---

## 6. Ranked experiments

| # | Experiment | Whole-frame gain (hypothesis) | Validation cost | Accept | Reject |
|---|---|---|---|---|---|
| 1 | Car-only early-out (§4) | 0.8–1.1 ms in most traffic frames | Low (existing bitwise gate) | Isolation fingerprints bitwise equal; zero shadow hits | Any fingerprint difference |
| 2 | Penetration diagnostics (§2.3) | 0 (prerequisite) | Low | Classifies the dominant cause | — |
| 3 | C1 sleep depth gate | About 0; fixes frozen overlap | Low | No "slept with residual" events; settle time within +20% | More awake bodies in S1 after 600 frames |
| 4 | C2 clipped manifold + C3 feature-ID warm start | ±0.5 ms; main visual fix | Medium (new baseline) | §2.3 F gates met | Depth worse in any class, or solver cost per contact +25% |
| 5 | Job B small-island VU0 solver | 0.7–1.5 ms in S2/S3 | Medium–high | Per-island shadow within tolerance; no O/D flags; S2/S3 p95 better in both run orders | Any routing omission, or no p95 gain |
| 6 | Job A fused certified pair job | 1–2.5 ms dense, 0.1–0.4 ms road | High (proof plus corpora) | Bitwise canonical; falsifiers fail as designed; ambiguity ≤ 15% | Any miss; ambiguity too high |
| 7 | VIF0 upload microbenchmark | ≤ 0.2 ms | Low | Disjoint-region write is legal and faster than the EE copy | Corruption, or no gain |
| — | Dense coloring on VU0; pose transforms on VU0; world/VU interleave; double object passes; macro solver | — | — | **Don't revisit** (measured dead ends: `README.md:375-389, 1122-1126`; `physics.md:224-230`) | — |

---

## 7. Limits

- This plan doesn't promise 60 FPS. In the dense 128 pile, physics alone still costs 18–20 ms (nominal step), above the whole-frame budget.
- A realistic target to measure against is the road scene: physics plus vehicle simulation p95 ≤ about 3–4 ms, leaving the rest for rendering and packet building. That's a hypothesis budget, not a measured one.
- Physics savings only raise FPS in EE-bound frames. VU1/GS-bound traffic frames need the rendering review.
- Contact counts, axes and passes are all preserved in the plan. The only changes to the physics model are C2 and C3 (the main corrections) and C4 (optional); each is labelled and gated.