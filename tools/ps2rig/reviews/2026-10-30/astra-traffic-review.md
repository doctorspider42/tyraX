# Independent traffic and vehicle performance review

Configured reviewer model: **GPT-6 Astra**. Review date: 2026-10-30.

This is a read-only architectural review. No edits to code, builds, new tests, hardware runs, commits or PR comments were performed. This Markdown artifact is the narrowly authorized output exception. Historical measurements below are repository evidence, not measurements executed by this reviewer. Proposed improvements are hypotheses until qualified.

Reviewed checkout: `F:/Projects/tyra-editor/.tmp/worktrees/ps2-collision`, HEAD `eb7a95ce0ed0175afe629b8a08010ee4a29af527`. Reviewed PR #285 head: `8fc9f33a78e4bdabfe443b5354f7ee660c01a321`, branch `claude/road-intersection-generation-477426`, fetched as `origin/pr-285`. The PR is separate from, and conflicting with, the current branch. PR features must not be assumed present in the collision checkout. Relevant editor, engine, performance and vehicle-testing skills were read.

## Conclusion

60 FPS with five detailed racers plus five to eight civilian cars is a plausible engineering target, but neither PR #285 nor the collision experiments demonstrate it. The highest-value next work is a faithful traffic fixture and whole-frame attribution, followed by controlling per-car rendering rebuilds and eliminating redundant dynamic-collider scans. Another isolated VU0 SAT optimization should not lead the effort.

## What the evidence establishes

PR `docs/traffic.md`, “What it costs (PCSX2)”, explicitly describes an emulator benchmark:

- Frozen camera 16 units above a junction, NTSC, `interleavePasses` off.
- Six ambient cars, with three to five taking the cheap far path.
- Vehicle step 0.46–0.77 ms; traffic core 0.05–0.07 ms.
- Render-cost totals vary with the number of visible cars; the document admits this variation.
- Approximately 0.37 MB of private EE geometry per car in that fixture.

This is not five detailed cars racing close together, and not six fully simulated cars on physical hardware.

The same PR's `docs/roads.md:2142–2183` reports its first physical PS2 test:

| Hardware arm | Synchronized render-cost Total |
|---|---:|
| Rain, lamps, rails, six traffic cars | 18.5 ms |
| Dry, lamps off | 15.9–16.1 ms |
| Additionally no traffic | 15.0–15.2 ms |

The full hardware pose ran at **30 FPS**. The document reports traffic core about 0.3 ms and vehicle step 1.0–2.6 ms, versus much smaller emulator numbers. These remain historical profiler measurements, not a clean normal-clock acceptance run for the requested target.

There is also instrumentation overlap: PR `src/roadlanes_gen.cpp:789–795` starts `tfStepT0_` before `trafficFrame()` and ends after the whole vehicle step. Core time separately includes `trafficFrame()` and `trafficDrive()` (`:446–553`, `:612–635`). **Core is nested inside vehicle-step time; adding them double-counts work.** The five-second log window is flushed inside `trafficFrame`, before the current full step is added. Define measurement windows and exclusive/inclusive labels before interpreting precise sums.

## Verified implementation and risks

### Vehicles and physics

Current `src/templates.cpp:15835–15852` divides a frame into one to four vehicle steps. Approximately 30 FPS can trigger two model steps. This creates feedback risk: missing the presentation deadline increases next-frame simulation work.

Current `:15481–15824` already has cached collider geometry, a static grid and frame/substep reuse. These are existing optimizations, not new proposals. However, `:15817` appends every dynamic collider to each vehicle query. Thus 100 physical props can still produce approximately cars-times-dynamic-props candidate walking even when separated.

Current `:16618–16730` gathers collision data and preserves old ordering/caps: four nearby meshes, eight floor boxes and twelve wall boxes. The reuse oracle proves agreement with the old capped behavior, not that all physically relevant colliders survive those caps.

The ground block around `:16849` evaluates four tyre contacts; six body-clearance probes follow. PR equivalents around `src/templates.cpp:17800–17960` include bridge height filtering. Ground querying is a credible per-car cost, but must be measured.

Current production `:17953–18089` uses pairwise capsule-like car contacts with two discs per car, repeated trigonometry inside the pair loop and partial positional separation. The private shared-contact rig is materially different. Neither its timings nor correctness gates can be silently attributed to production.

### Traffic

PR `src/roadlanes_gen.cpp:642–700` chooses the far kinematic path by camera distance, at `max(trafficDistance + 30, 45)`. It skips tyres, suspension and walls, follows lane height, and clears lateral/vertical/attitude dynamics.

That is an ambient-world approximation, not a safe interaction criterion. A camera can be distant from a car involved in a collision, or a loose prop can enter its path. The ordinary car–car loop still follows the per-car loop, but the next kinematic step can overwrite the resulting state.

PR `:469–484` feeds traffic AI nontraffic vehicles and the on-foot player as obstacles. It does not include arbitrary physical road props. Near cars still physically collide, but “AI avoids the 100 obstacles” is not established.

`src/traffic_core.inl:195–225` already limits path projection around the previous arc position. Repeated scans at `:498`, `:557`, `:726`, `:739` concern vehicles/conflicts; at thirteen cars, replacing all of them with elaborate structures is not automatically worthwhile. `laneLengthNear` around `:893` and the global red-light-line walk in `roadlanes_gen.cpp:574` deserve spike attribution on large maps.

### Rendering

Existing current implementation includes distance tiers/hysteresis (`src/templates.cpp:18467`), shine selection/budget (`:18413`), distance-sensitive paint rebuilding (`:18380`), conservative wheel visibility tests and immutable local-space moving wheels (`:18934` onward), far-tier wheel suppression, translucent glass and cached light geometry.

The moving-wheel comment around `:19105` records a historical physical-console reduction from about 1.05 to 0.28 ms for one driven car. “Move wheels to matrices” is already implemented.

**A concrete scaling defect exists in the paint scheduler.** `vehiclePaintGate`, current `:18380–18404`, claims one non-driver rebuild per frame, but the slot conflict only defers cars whose `paintDeferred` is false. With several deferred cars, all can bypass that conflict next frame. This is two-car fairness, not a hard multi-car budget. Five turning racers can create synchronized colour-rebuild spikes.

PR `docs/instance-sharing.md:169` excludes vehicles from generic shared geometry. Its `:216–223` records why `std::shared_ptr` must not be introduced with this EE toolchain: its lock policy consumes kernel semaphores.

## Ranked proposals

### 1. Fix paint scheduling before reducing visual quality

Maintain a fixed-size pending list keyed by vehicle/part, requested colour-state generation, first-pending frame and estimated vertex count. Prioritize the driver, then oldest eligible visible requests. Bound non-driver recomputation by vertices/bytes, not a boolean that deferred cars bypass.

First allocation, repair and LOD change can invalidate colours; those cannot display incompatible stale data. Prewarm arrays or account for separate forced work. Record forced-work cost and maximum request age. Preserve distance-dependent error thresholds. Accept against moving highlights, transitions and p95, not parked averages.

### 2. Avoid redundant legacy gather, then index remaining dynamic queries

The focused follow-up verified a lower-risk first step in the private unified path. In `.../stream/district/src/gen/game_vehicles.gen.cpp:4097`, `kind == 2` only radius-tests and appends `pushIdx`, then returns. At `:4976–4977`, both its candidate counter and sole old bumper response are disabled under `VehicleContacts::enabled`. Thus an early skip in the unified vehicle-physics gather, including its `recordCol` cache path around `:4221`, avoids dead work without removing shared-body contacts.

**Do not remove these entries from global `vehColliders_`:** the camera sweep at `:3400–3404` processes every `kind != 0` via `sweepObject`, including dynamic props. Use a separate vehicle-physics candidate view or explicit query mode. This is source-supported but untested; preserve legacy mode and verify exact shared contact/body states in an A/B.

For remaining dynamic queries, keep the static index and add compact dynamic records: stable body id, generation, swept AABB and flags. Use contiguous cell offsets/body indices or an incremental cell structure after measuring update cost. Sleeping props retain valid entries.

Query cells intersecting swept chassis bounds, including rotation expansion and accepted-interval motion; deduplicate with integer stamps. Keep stable pair ordering. Large bodies need an overflow list. Capacity overflow falls back to complete querying, never drops candidates. Reuse candidates for AI lookahead while keeping AI anticipation distinct from collision authority.

### 3. Cache exact road candidates, not approximate heights

Retain triangle id, chunk generation and indexed-cell coverage per probe. Re-evaluate height/barycentrics at the current point. A containing triangle alone is insufficient where roads overlap: preserve highest eligible surface selection below the bridge cap.

Share local candidate sets among nearby tyre/clearance probes when bounds permit, while retaining per-probe acceptance. Invalidate on stream generation, surface changes, cell exit or ambiguity. If reconstruction dominates, bake XZ bounds/plane coefficients into compact arrays. Preserve grip/cover/material semantics and compare against the full oracle at edges, junctions and bridge levels.

### 4. Separate simulation authority from visual LOD

Five racers retain full collision participation regardless of visibility. Civilian route control can be cheaper, but promotion must consider conservative interaction bounds with all active physical participants, not camera distance.

A reduced mode needs a swept corridor proven clear through the next update horizon, valid road residency and no active contact/damage/airborne state. Uncertain cases promote before movement. Preserve velocity/contact state through transition; no lane snap after impact. Route planning can be staggered if measured useful; immediate obstacle response and collision remain active on every accepted interval.

### 5. Share immutable vehicle geometry and allocate damage data lazily

Per definition/LOD/material: immutable positions, normals, UVs, strip metadata and rest geometry with scene lifetime. Per instance: transform, small material state and optional deformation/colour arrays.

On first dent, acquire a private preallocated buffer and copy the affected representation. Preserve original rest geometry for later dents/repair. Prefer per-part allocation before complex per-range ownership. First benefit is RAM/predictable allocation; frame-time improvement is not guaranteed. Measure first-impact spikes and cache effects. VIF REF data must survive until retirement; CPU submission completion is not permission to recycle it.

### 6. Precompute body invariants and shorten hot walks

Cache yaw basis, disc centres/chassis axes, inverse mass/inertia and bounds per state generation, instead of repeated trigonometry per pair. Positional corrections invalidate affected geometry. Separate collision state from audio, strings, geometry owners and damage visuals. Choose compact aligned structures by measured cache behavior, not by a blanket SoA rule.

### 7. Further VU work follows frame attribution

The schedule is predict on EE, build immutable records, launch conservative packets, run independent COP2-free work, harvest/confirm, solve, publish. EE scalar FPU remains in the EE instruction stream; it is not a third thread. VU0 macro is not an autonomous worker.

Audit helpers for implicit Vec4/M4x4 COP2 use. `vendor/tyra/engine/src/math/vec4.cpp:11` points to inline VU operators; `m4x4.cpp` contains assembly. Matrix publication, rendering setup and other owners force fences. VU1 already serves rendering. Keep production micro off until inclusive packing, transfer, waits, EE proofs and solve improve complete frames at unchanged coverage. See the separate physics report for concrete new architectures.

## Hardware measurement matrix

Use one fully populated, hash-verified release-equivalent fixture, normal clock, deterministic simulation-time input and fixed seeds. Capture correctness images in a separate diagnostic arm. Disable host polling during timing. Verify profile-level-2 transparency against an uninstrumented boot.

| Population | Purpose |
|---|---|
| Road + 100 separated props, no cars | World/streaming/idle-body baseline |
| 1 HQ racer | Marginal player cost |
| 5 HQ racers | Close-racing target |
| 5 HQ + 5 lowpoly civilians | Lower traffic target |
| 5 HQ + 8 lowpoly civilians | Upper traffic target |

Exercise straight motion without contacts; all five racers in chase view; visible traffic; camera turns/LOD crossings; junction queues; single-prop, car–car and short chain impacts; streaming boundaries. Start day/dry, then night, then wet features.

Place the 100 props along the driven route with verified nonoverlapping collision bounds. Record nearby active bodies and candidates. Keep a separate small stack fixture for penetration; a dense 128-body heap is not the road target.

Capture median/p95/p99/max frame period, deadline and consecutive misses; EE pre/update/work and present idle; substep counts; candidate, narrowphase, CCD and contact counts; solver iterations/correction. Per-car scopes distinguish route AI, ground, gathering, integration, audio/effects, transform publication, paint/damage and rendering.

Count submitted packages/vertices/passes for body, wheels, glass, env, lamps, shadows and debris; uploads/evictions; DMA bytes/waits. Do not sum nested scopes or infer EE boundedness from short final drain. Diagnostic suppression of one render component with identical simulation identifies cost, but is not the delivered quality preset. Retest the complete picture.

## Acceptance and limits

- A 60 Hz claim needs sustained normal-clock presentation at the actual refresh interval, not a render scope below 16.7 ms. Headroom targets are design budgets, not evidence.
- Reject improvements below repeatability or paid back elsewhere/in p95.
- Reject reduced collision coverage, silent overflow, off-screen collision omission, unsafe far-mode re-entry or late promotion.
- Reject first-impact allocations or clustered LOD/paint work that creates visible hitches.
- VU/DMA changes require physical-hardware lifetime and stale-result validation.
- Penetration requires depth/duration measurements, not merely zero NaNs/missed broadphase hits. Distinguish solver residual, manifold, timestep, inertia, collider/render mismatch and transform lag.

Five 2,400-triangle bodies can cost much more than their triangle count once reflections, interiors, glass, wheels and shadows are included. Measure that exact five-car picture. A Burnout-like result requires coordinated asset, rendering and simulation budgets; no coprocessor switch supplies the missing frame budget alone.
