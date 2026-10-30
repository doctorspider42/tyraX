I couldn't write the plan file or call ExitPlanMode because this session only has Read, Glob and Grep, so the full report is below. Nothing was edited, built or run on the console.

# Traffic FPS and the 100-prop road fixture: independent Opus review

## 0. Bottom line

- **The PR's traffic is full vehicles, and the cheap path for far cars is chosen only by distance from the camera.** In a Burnout-like chase scene almost every car is within the 45-unit gate, so each one runs the whole vehicle sim and draws the whole set of per-car parts.
- **Two exact, low-risk EE fixes are visible in the code:**
  - the vehicle collider gather scans every entry of `procColliders` once per car per substep, with no grid;
  - several per-object vehicle lookups are linear scans over all vehicles.
- **The far (kinematic) path throws away most of a collision response.** Position, sideways speed and spin from a hit are overwritten by the lane on the next step. It also never touches props.
- **On render, the per-car cost is mostly per-submit overhead, not triangles.** Measured parts: blob shadow 0.35 ms for 18 triangles, paint rebuild 0.38–0.44 ms per shining car while the camera turns, moving wheels 0.28 ms per car.
- **Rough estimate, not a measurement:** 5 HQ cars with every feature near the camera, plus Motor District's environment, cannot fit 16.67 ms. Motor District with rain, lamps and 6 traffic cars already measured **18.5 ms on the PS2**. The 60 FPS target needs a scene budget and tiered car features, and must be proven on the console.
- **The box penetration is real and has several likely causes.** Each is testable; none of them is a NaN. The solver removes at most 50% of remaining overlap per frame. Contacts are detected once per frame. The CCD is linear only. Sleeping partners may act as immovable. The car-to-box mass ratio is large.

## 1. Code map (PR #285 head 8fc9f33a, read from `pr-source`)

### Traffic

| What | Where | Note |
|---|---|---|
| Once-a-frame core (signals, obstacles, recycle, spawn) | `src/roadlanes_gen.cpp:446-560` | Timed into `tfCoreTicks_`, about 0.06 ms in PCSX2. |
| Pedals from the lane core | `roadlanes_gen.cpp:614-635` | Called per car per substep. |
| Far kinematic path | `roadlanes_gen.cpp:642-698` | Gate at `:646`: camera distance ≥ max(trafficDistance+30, 45). |
| Kinematic pose taken from the lane | `roadlanes_gen.cpp:654-679` | Overwrites `pos`, zeroes `lateral`, `pitch`, `roll`; only `v.speed` survives. |
| Hook placement | `roadlanes_gen.cpp:809-820` | The kinematic check comes after the sleep gate and before ground, gather, rig and walls. |
| Traffic cars never sleep | `roadlanes_gen.cpp:821-824` | A queued traffic car pays the full sim every frame. |
| Headlight pool off for traffic | `roadlanes_gen.cpp:825-829` | A pool per car was measured at 1.1–1.6 ms and rejected. |
| Telemetry line | `roadlanes_gen.cpp:547-553` | `vehicles us/frame` includes the player's car. |

### Vehicle simulation (`src/templates.cpp`)

- **`stepVehicles` (`:16833-16851`):** n = round(dt·50), clamped 1–4. At 60 FPS that is one substep; at 30 FPS it is two (+0.48–0.52 ms measured, `docs/backlog.md:687`).
- **Collider list:** built once per frame (`:17202`).
- **Per-car loop (`:17206`):**
  - **Ground (`:17824-17926`):** 4 wheels × (`terrainHeightAt` + `roadSurfaceAt`), plus 6 body-floor `groundSurfaceAt` probes. That is 14 height queries per car per substep.
  - **Per-wheel `collidePlayer` (`:17867-17876`):** runs whenever a mesh prop is near; noted at about 1.1 ms per call in PCSX2.
  - **Gather:** the static grid serves `vehColliders_` (`:17761`), but `procColliders` is scanned linearly per car (`:17796`, `:17799`, and `:17752` on the reuse-recording path). Road furniture, poles, bridge parapets and piers all live there. The same linear scan exists in this worktree (`templates.cpp:16717/16807/16810`).
  - **Physics-body pushes:** at most 8 (`:17606`, `:18484`).
- **Car-to-car pass (`:18945-19079`):** O(N²). Each car is modelled as two discs. Separation corrects 60% per frame (`:19055-19063`), so some overlap is allowed by design. It includes kinematic cars, whose position change is then lost (see above).
- **Stage timers already exist:** `VEH_LAP` → `ftrig::vehLap[7]` → `FTVEH input/gather/rig/walls/bodies/smoke/sound` (`game_templates.inc:2241, 2460`). They do not split by car class and do not cover the car-to-car pass.

### Vehicle rendering

- **Shine budget (`templates.cpp:19411-19447`):** only cars within 35 units and not on a far tier, picked by distance. The player's car ranks first; nothing else gets priority.
- **Paint pass on the EE (`game_templates.inc:26506-26600`):** quantised hysteresis. Moving it to VU1 was **measured slower** (a dead end).
- **LOD tier (`templates.cpp:19459-19488`):** uses `trafficDistance` for every car the player is not in, with hysteresis. The far tier carries baked wheels, so it is one submit (`:19991-20003`). Wheels stop drawing beyond 70 units (`:20007`).
- **Moving wheels (`:20092-20122`):** a matrix per wheel, with the baked stream replayed (0.28 ms instead of 1.05 ms for an EE rebuild). That is 4 submits per moving car.
- **Per-object linear scans:**
  - `vehicleLodTier` (`templates.cpp:19459`) is called from the LOD block of every LOD-eligible model (`game_templates.inc:26907`);
  - `vehicleLodHide` (`templates.cpp:19496`);
  - `vehicleShineOn` (`templates.cpp:19511`).
- **Per car:** 0.37 MB of EE RAM, and **cars do not share a mesh** (dents and paint are per car; `docs/traffic.md:271-273`).

### Measured facts vs claims still owed

| Fact | Source | Scope |
|---|---|---|
| Motor District, rain + lamps + 6 traffic: Total 18.5 ms (30 FPS) | `docs/roads.md:2142-2181` | **Physical PS2**, debug over ps2link. |
| Traffic rendering ≈ 0.9 ms; traffic core + vehicle sim 0.3 + 1.0–2.6 ms | same | Physical PS2, debug build. |
| 6 cars at 60 FPS, ≈0.25 ms per fully simulated car | `docs/traffic.md:257-273` | **PCSX2 only**, under-reads road features 3–10×. |
| Per-bag submit ≈12 µs, dominated by D-cache misses | `docs/ee-submission-rearchitecture.md:1353-1359` | Physical PS2. |
| Blob 0.35 ms for 18 triangles; lamps 0.25 ms; paint 0.38–0.44 ms per shining car | `docs/backlog.md:672-686` | Physical PS2 (the later cuts' console price is still owed). |
| Chase view doubles StaPip calls, 46,096 primitives | `.agents/.../performance.md:29` | Physical PS2. |
| "Not measured on a physical PS2" | `docs/traffic.md:385` | PR's own limit. |

**Claims that need console verification:** every traffic number in `traffic.md`, the far path's saving, and any 5 HQ + 8 civilian frame time. None of these exist yet.

## 2. Likely bottlenecks (hypotheses ranked by evidence)

1. **Per-car submit overhead (EE) for nearby cars.** A near car on tier 0 submits:
   - body parts (lamps, body, matte),
   - glass at the translucent tail,
   - the env/shine overlay (an extra pass when the car is in the shine budget),
   - 4 wheel matrices,
   - a blob,
   - corona and lamp submits at night,
   - wet streaks in rain.

   That is roughly 8–12 bags at ≈12 µs plus package cost each, before VU1 or GS work. Lowpoly civilians pay almost the same as HQ cars here, because triangles are not the driver.
2. **HQ geometry (VU1/GS).** The CC96 body is 4,212 strip vertices / 57 packages, plus the env pass over the same vertices. This scales with triangles and screen area. It must be split from item 1 by experiment (section 4).
3. **Full sim for every car within 45 units of the camera.** 14 height queries, the gather, the rig and the walls per car. The `procColliders` scan grows with cars × furniture boxes. Traffic cars also cannot sleep.
4. **Environment.** Roads, furniture, lamps and rain already exceed the budget without HQ cars.
5. **Physics props.** These are separate. With the shared prototype, cost is +0.8–1.1 ms with no interaction and about +2 ms on impact (different trajectories).

## 3. Ranked implementations

### A. Exact EE fixes (same output, oracle-checked)

**A1. Grid for `procColliders` in the vehicle gather.**
- Add the boxes to the static-grid machinery that already exists (`vehColCandidates`, `templates.cpp:~16790-16826`), or build a second grid. Return candidates in ascending index order, which is what keeps the full walk's order and caps identical.
- Rebuild when streaming adds or removes boxes (`roadstream.cpp:315/543`, `roadfurniture.cpp:1195`, `roadbridge.cpp:692`). Breakable boxes are already moved out of reach rather than erased, so indices stay stable.
- Extend `TYRA_VEH_COLLIDER_GRID_VERIFY` to compare the proc lists byte for byte.
- **Reject if** any mismatch appears, or if `FTVEH gather` does not fall.

**A2. Reverse index from object to vehicle.**
- Add `int16 vehOfObject_[objectCount]`, filled in `setupVehicles`, `trafficPlace` and `trafficRemove`.
- Use it in `vehicleLodTier`, `vehicleLodHide`, `vehicleShineOn`, `vehiclePaintFor` and `vehicleBlobTextureFor`.
- This removes O(visible models × vehicles) scans of a large `VehicleRt` stride.
- It is exact, so prove it with pixel equality on frozen poses.

**A3. Let stopped traffic cars sleep.**
- Allow a car waiting at a red light, or queued with speed 0, to take the sleep path. Wake it when `tf_.drive` asks for throttle > 0, or when another car or the player comes within a swept radius.
- This changes behaviour (the reason for the exclusion was "never pulls away"), so the wake condition must be explicit.
- Gate on `--vehicle-check` "road traffic": 0 overlaps, 0 red-light entries, no deadlock.

### B. Traffic simulation tiers that keep nearby collisions meaningful

**Existing state:** two states, FULL and KINEMATIC, chosen by camera distance. KINEMATIC does not collide with props or furniture, and loses everything but the speed change from car-to-car hits. So **a cheap path that preserves nearby collision does not exist yet.**

**B1 (correctness, do first). Promote on contact.**
- In the car-to-car pass and in the prop push, mark any kinematic traffic car involved in a contact as `tfPromote`.
- `trafficKinematic` must return false while `tfHoldFrames > 0` (for example 90 frames after the last contact). The car then keeps its lateral speed and spin and runs the full sim.
- Hand back to the lane core only when lateral error < 0.5, heading error < 10° and the car is back on the lane.

**B2. A LANE tier, based on threat instead of camera distance.**
- A traffic car may take the cheap path whenever it is **not threatened**. Threatened means its box, inflated by 0.5 s of travel, intersects the swept box of the player, an HQ racer, an awake prop or a FULL car — regardless of camera distance.
- LANE cars stay in the car-to-car pass and, when the shared prototype lands, in the body registry's pair grid as kinematic OBBs. The first contact is detected **the same frame** and handled with finite drive mass by the shared solver, which already returns linear and yaw impulse without integrating motion twice. The car becomes FULL from the next frame.
- **Visual cost:** a LANE car has no suspension. Add a cheap fake: pitch from the lane slope (two samples, already in `tf_.g.at`) and lean from the commanded acceleration. Accept it only if frozen-pose captures near 15 units are not obviously flat.
- **Reject if** the host traffic simulation shows any overlap, if promotion latency is above 0 frames, or if a hit-response histogram (Δv, spin) differs from the FULL-only arm by more than an agreed tolerance on the same scripted crash.

**B3. Stage-major loop for FULL cars (to measure, not promise).**
- The per-car body of `updateVehicles` is thousands of lines, larger than the EE's 16 KB I-cache.
- Split it into stage loops: all cars input → all ground → all gather → all rig/walls → all bodies. Keep car order within each stage so push order and impulses stay identical.
- **Accept only** if the end-state is bit-identical on a recorded drive and `FTVEH` falls on the console. PCSX2 has no cache model, so it cannot show this gain.

### C. Render tiers for civilians (visual tradeoffs are explicit)

**C1. Shine budget by class.**
- Add `VehicleDef::shinePriority`, or give AI racers (waypoint cars) priority over traffic. Make `VEHICLE_SHINE_BUDGET` a scene setting, for example 2–3.
- Civilians lose the paint and env pass. This saves one submit plus 0.38–0.44 ms of paint rebuild per shining car while turning.

**C2. Civilians use the existing far-tier mechanism.**
- Author lowpoly civilians as their own definitions: the base mesh is lowpoly, `farModel` is lower still, and `trafficDistance` is small (12–20 units).
- This already exists (`vehicleLodTier`); it is content and settings only.
- Tradeoff: wheels are baked and do not spin or steer on tier ≥1.

**C3. One-submit traffic tier.**
- Lamps go into the body part as emissive vertex colours, rewritten only when the lamp state changes.
- `glassOpacity` 1, so there is no translucent glass part.
- The blob joins **one shared traffic blob batch** with one generic mask texture. The backlog says a shared bag needs one texture for every car, which is acceptable for civilians.
- Expected: about 8–10 bags down to 2–3 per car. Measure it.

**C4. Shared immutable geometry for civilians that cannot be damaged.**
- With `damageVisual` off, cars of one definition reference one local array per part. Each car is a matrix plus a baked-stream replay, the precedent being `wheelGeoFor` and the moving-wheel matrix path.
- Also saves about 0.37 MB of RAM per car, which matters in Big City (`bad_alloc`).
- Cost: no dents or per-car paint (body colour can stay as a per-bag colour if the engine allows; check). This is bigger engineering, so do it last.

### D. Do not reintroduce (dead ends)

- Paint rebuild on VU1.
- Rebuilding moving-wheel vertices on the EE.
- Headlight pool per traffic car.
- Aligning only `BagArray` bases to 128 bytes.
- Bumping `bboxVersion` every frame.
- `shared_ptr` per instance.
- Interleaving two full object updates with vehicle substeps (43 vs 22 ms).
- Contact or axis caps (384).
- Traffic U-turns at dead ends.
- Two-ELF A/B of game code.
- Treating VU0 as free: micro owns COP2, macro cannot run alongside it, and the FPU shares the EE's instruction stream. Traffic logic is branchy scalar code and stays on the EE.

## 4. Console A/B matrix and instrumentation

**Fixture: "burnout-track".**
- A closed road with a fixed environment. Motor District is a separate arm, not the base.
- Player, 5 HQ waypoint racers and 8 traffic civilians.
- Camera driven by a frame index; one ELF, arms selected by a `bin/traffic-arms.cfg` read at boot.
- `TYRA_FRAME_PROFILE 2`, quiet profile, transparency check, both orders, at least 1,200 frames per arm.

**Instrumentation to add:**
1. `FTVEH` split by class (player / HQ / civilian-FULL / LANE / sleep), plus a car-to-car lap and a traffic-core lap.
2. A per-car submit counter: bags and packages per vehicle per frame, through `FTPKT`/`FTCLIP` attribution.
3. Extend the `TRAFFIC` line with `full / lane / sleep / promoted / promotionLatency`.
4. **Record and replay poses:** a FULL-sim run writes per-frame (pos, yaw, pitch, roll, wheelY, steer, spin) for every car. The replay arm disables the sim and draws the same poses, which gives render cost alone.
5. A hidden arm: sim on, cars invisible, which gives simulation cost alone.

**Separating HQ geometry from per-car logic:**

| Arm | Isolates |
|---|---|
| Same instances, model HQ ↔ lowpoly, same bag set | Triangle / VU1 / GS cost |
| Replay vs hidden vs full | Render vs simulation |
| Features one at a time: shine 0, glass, blob, lamps, moving wheels | Per-feature submit cost |
| Cars in frustum but far / tiny vs near / large | VU1 transform vs GS fill |
| Reduced-resolution (BLSS) arm | Whether the frame is GS bound |
| Camera facing away (cars culled) | Simulation plus culling floor |

**Scaling:** total cars 1/3/5/10/13, as (HQ, civilian) = (1,0), (3,0), (5,0), (5,5), (5,8). Fit `frame = base + a·HQ_near + b·civ_full + c·lane`, and report p50, p95, p99 and missed fields.

**Conditions:** day / night (lamps and coronas) / rain; chase / hood / overview cameras; speed-feel blur and FOV on and off. Mirrors and other views re-submit the same bags (`game_templates.inc:26870`), so list them per arm.

**Stage budget for this scene (a target to test, not a promise):**

| Stage | Budget |
|---|---|
| World / roads | 6.0 ms |
| 5 HQ cars render | 4.0 ms |
| 8 civilians render | 1.5 ms |
| Vehicle sim + AI + traffic | 2.0 ms |
| Prop physics | 1.5 ms |
| Effects / HUD | 1.0 ms |
| Slack | 0.67 ms |

Each stage must hold at **p95**, not mean.

## 5. 100 separated road props and the penetration diagnosis

**Layout (new `--layout road100` in `vehicle_contacts_fixture.py` / `collision_stress.py`):**
- Deterministic seed. 20 clusters of 5 along about 1 km of road, 40 units apart.
- Mix: 60 crates of 0.9, 25 boxes of 0.5, 15 thin 2×0.5×0.5 barriers (to stress CCD).
- Each prop sits on `groundSurfaceAt` + half height + 0.002. Centres are at least r_i + r_j + 0.25 apart, where r is the half-diagonal.
- **Load gate:** an exhaustive EE canonical pair check must show 0 prop-to-prop contacts and prop-to-world depth ≤ slop. Props start asleep.

**Runs:**
- Scripted drives at 15, 25 and 35 units/s, a weave, and later with 5 HQ + 8 traffic.
- Arms: production scalar, and the shared-contact prototype.
- Run each in both orders.

**Per-frame diagnostics (admission mode only):**
- awake bodies, contacts, newly created pairs;
- depth before the solve;
- **residual depth after the solve**, from a diagnostic re-detection after integration;
- histograms at 0.01 / 0.05 / 0.1 / 0.2 units and as % of the smaller half-extent;
- persistence in frames;
- class: box-box, box-car, box-world, box-sleeping;
- CCD time-of-impact hits;
- a tunnelling detector: the sign of separation along the normal flips with no contact;
- FCR31 and VU0 flags.

**Hypotheses and how to separate them:**

1. **Convergence.** Baumgarte 0.5, 3 split-position passes, slop 0.005 (`game_templates.inc:4892-4897`, `:25550-25579`).
   - Test: dump the worst islands and replay them on the host through the extracted solve loop with 3/6/12/24 passes.
   - If the residual falls, try extra position passes **only for islands still above a threshold**. That costs nothing for coverage.
2. **Detection lag.** Contacts are created by solve motion but detected only next frame.
   - Test: new-pair depth much greater than persistent-pair depth at high speed.
   - Fix candidate: re-run narrow phase only on pairs whose bodies moved more than a fraction of their size. This is not a second full pass; price it.
3. **Sleeping or immovable partners.**
   - Test: deep box-sleeping contacts.
   - Fix: wake neighbours along the contact chain before the solve.
4. **Mass ratio between car and box.**
   - Test: residual concentrated in box-between-car-and-box sandwiches.
   - Fix candidate: car-first ordering in the position passes only.
5. **Angular motion missed by linear CCD.**
   - Test: tunnelling events from fast-spinning boxes.
   - Fix: inflate the broad phase by |ω|·r.
6. **Visual vs collider mismatch.** The chassis box is controlled in yaw only while the rig pitches the body; render order can also show a pose a frame late.
   - Test: wireframe collider overlay on captures at the worst-residual frame.
7. **Numerics.** Unlikely: float ulp is about 6e-5 at 700 units, far below slop. Keep FCR31 monitoring.

## 6. Order, rejection criteria and limits

1. **Console run 1 (instrumentation only, PR head as-is, PR conflicts with main):** the baseline matrix subset (scaling × day/night × replay/hidden) and road100 admission with residual-depth diagnostics.
2. **A1–A3:** exact or gated changes, oracles, one-ELF A/B. **Reject** any change that does not beat the two-boot noise floor in its own bracket, or that changes pixels or end-state where it claims to be exact.
3. **B1, then B2:** host traffic simulation (0 overlaps, 0 promotion latency, hit-response tolerance), then console.
4. **C1 → C2 → C3:** settings and content first. Use LOD-step pixel metrics for popping, and accept per-arm visual tradeoffs explicitly.
5. **Penetration fixes:** only after host replay names the cause. **Reject** anything that drops contacts or axes, or caps bodies or contacts.
6. **C4 (shared geometry):** only if RAM or submit cost still blocks the target.
7. **Certification:** 5 HQ + 8 civilians, day and night, p95 per stage within the budget table, over at least 3 boots.

**Practical limits:**
- 13 FULL-sim cars, all with shine, glass and moving wheels, near the camera, in Motor District with rain and lamps, is not credible at 60 FPS given the measured 18.5 ms before any HQ cars.
- A dedicated race scene with shine on 2–3 cars, civilians on one or two submits plus the LANE tier, and prop physics in the low milliseconds, is plausible. It is not promised until the matrix above measures it.
- Dense 128-box piles remain out of scope for 60 FPS. The 100 separated road props are the right realistic gate.