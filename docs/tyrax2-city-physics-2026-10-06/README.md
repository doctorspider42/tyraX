# Daylight vehicle substep attribution preparation

The native ELF and5400-loop emulator qualification are complete. **Physical PS2
measurement is pending a fresh power-cycle/ps2link confirmation.** This page does
not establish that vehicle physics causes the previously observed daylight
19–22 ms work spikes, and contains no emulator-derived hardware price.

The [daylight representative-view experiment](../tyrax2-city-day-drive-2026-10-06/README.md)
reproduced a moving47.36 Hz chunk and two parked59.94 Hz windows. The production
vehicle wrapper rounds `dt * 50` to its substep count; a roughly33 ms preceding
frame therefore causes two vehicle-update calls. This experiment records actual
calls and scopes the wrapper to distinguish trigger from subsequent feedback.

## Exact observer contract

The fixture derives from the unchanged private noon/All-receiver day-drive
postimage. It preserves the city, route, midpoint selection, one-time cold car
relocation, near-stationary parked view and normal physics/logic. No production
feature or timestep behavior changes. The only physics-source additions are
begin/planned/executed/end observer calls around the existing wrapper and its
unchanged update loop.

All384 ordinary raw loops (900–1027 in each phase) retain dt bits, planned
substeps and the actual number of `updateVehicles` invocations. The probe has
128 reused24-byte rows (3072 bytes), plus small shared counters/state. Metadata,
branch, counter and resident-buffer costs remain common and unpriced.

When enabled, exactly two additional COP0 Count reads surround `stepVehicles`,
including its cache invalidation, all update calls and `updateVehicleTutorial`.
The scope excludes separately called smoke/skids/debris updates, other game
logic and renderer work. Counter overhead inside the wrapper is included in
the scoped elapsed value; elapsed time can also include thread preemption.
There is no new synchronization, GPU/VIF drain, hot output or host polling.
FrameProfile and HardwareTrace stay compiled out.

The ordinary NightSampler is byte-identical to the parent and retains its
262 reads per phase. The added scope reads are accounted **separately**:

| Phase | Scope timer schedule over128 raw loops | Additional Count reads |
|---|---|---:|
| Driving | ON128 | 256 |
| Parked first | OFF40 / ON48 / OFF40 | 96 |
| Parked repeat | ON40 / OFF48 / ON40 | 160 |
| Total | | 512 |

Exports occur at1200–1327, outside priced windows. The export checks every
sample's exact frame, completed call count, and128 captured loops. Enabled
scope intervals must fit within the corresponding non-pacing whole-loop work;
disabled intervals must be zero. The existing cold pose/camera guards remain.

The parked forward/reverse contrasts compare the active timing path against
the disabled path at the same fixed camera, with the parent's reported tiny
vehicle creep. Forty/forty-eight-loop cohorts are short, so net differences,
outer spread and differing signs must be reported honestly. They do not price
the common metadata footprint, and are not a uniform correction to subtract
from all physics intervals or moving loops.

## Qualification reached

- [Native source/object/ELF binding](preparation/vehicle/native-proof.json)
  includes the actual compiled vehicle translation unit and new header in its
  dependency closure. Linked probe rows/counters have one final symbol instance.
  ELF SHA256 `7438bf29233a25b846921376b61f1e9e9a932b95b77206b10bede886b41b483c`.
- [Primary source/asset release](preparation/root-review.json) preserves the
  no-added-wait review and ordinary sampler identity. A changed header does
  not rely on a previous engine archive; actual native mirrors/objects are bound.
- [Completed emulator protocol](emulator/strict-result.json) validates5400 loops,
  all384 scope/metadata records, three128-row capture totals,512 extra reads,
  unchanged route motion, selected parked camera and scope bounds. The normal
  emulator run executes one substep in its raw samples; it is **not** a runtime
  qualification of a two-substep cohort. The parked raster was inspected.
  Exact owned PID12019 was stopped and terminal `/proc/12019` absence checked;
  Xvfb remains. None of these elapsed values prices PS2 performance.
- [Twelve replay parser controls](preparation/parser-controls-f32.json) reject
  missing execution, wrong plan/dt/timer/read count, missing captures,
  out-of-loop scope, nonzero disabled scope and duplicate records. A synthetic
  two-step schema row tests parser acceptance only, not actual two-step execution.
  The [current parser](preparation/analyze-physics-f32.py) rounds intermediate
  single-precision arithmetic when predicting the source's planned count from
  dt bits; a threshold-edge schema control supplements the earlier11 controls.
  Its completed emulator reparse equals the original result. Earlier parser
  artifacts remain pinned rather than rewritten.

Initial private preparation expected an outdated include name and refused
before any build/device run. The preserved failed v1 clone is in the local lab;
the corrected v2 uses the actual generated runtime include. The archived
preparation script describes v2, not that rejected partial postimage.

## Physical interpretation still required

For each driving sample, compare scope time and whole-loop non-pacing work,
grouped by **executed** substeps. The loop-entry period belongs to the preceding
entry interval; inspect the preceding measured loop when a one-to-two transition
occurs. A heavy one-step loop before a two-step loop would show that doubled
physics did not initiate that particular slowdown. Compare the residual work
outside the vehicle wrapper, without attributing it automatically to rendering.

Executed-step cohorts differ in dt, world pose and changing view: their mean
difference is not a causal extra-substep price. Physical activation, scoped
cost, parked observer contrasts and transition ordering remain unmeasured.
Do not change correct timestep semantics based only on this preparation.

The prior physical serving client PID44564 was stopped with exact executable
and UTC start-time ownership checks. No new PS2 ELF has been launched for this
probe; a physical reset question is pending.
